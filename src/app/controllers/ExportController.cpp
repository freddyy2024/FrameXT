#include "controllers/ExportController.h"

#include <QDir>
#include <QFileInfo>
#include <QSet>
#include <QThread>
#include <QtConcurrent/QtConcurrentMap>

#include <atomic>
#include <functional>

namespace fxt {

namespace {

// 单张导出 worker（线程池内）：无共享可变状态；取消检查在入口。
// 写失败重试 1 次在 ExportEngine::exportSingle 内部。
void runJob(ExportJob &job, const std::atomic<bool> &cancelFlag,
            std::atomic<int> &doneCount, int total,
            const std::function<void(int, int, const QString &)> &progress)
{
    if (cancelFlag.load(std::memory_order_relaxed))
        return;   // 取消后未派发的项保持 ok=false（不计失败）
    job.result = ExportEngine::exportSingle(job.req);
    const int done = doneCount.fetch_add(1) + 1;
    // 跨线程 emit 由 Qt 的 queued connection 排队到主线程
    progress(done, total, QFileInfo(job.req.doc.sourcePath).fileName());
}

} // namespace

ExportController::ExportController(QObject *parent)
    : QObject(parent)
{
}

int ExportController::workerCount()
{
    const int cores = QThread::idealThreadCount();
    return qBound(2, cores - 1, 6);
}

void ExportController::start(const QList<PhotoDocument> &documents,
                             const QList<FieldValueResolver> &resolvers,
                             const QList<QImage> &logos,
                             const ExportOptions &options,
                             const ConflictResolver &onConflict)
{
    if (m_busy)
        return;

    m_results.clear();
    m_jobs.clear();
    m_cancelRequested = false;
    m_doneCount = 0;
    m_timer.start();

    const QDir dir(options.outputDir);
    if (!dir.exists()) {
        QList<ExportEngine::ItemResult> empty;
        emit finished(empty, false, 0);
        return;
    }

    // —— 预扫：主线程串行预分配全部输出名（冲突消解，§5.3）——
    QSet<QString> assigned;      // 本批内已占用的目标名
    QHash<QString, int> baseSeq; // 同基名组内序号（命名引擎语义，见 buildFileName）
    QList<ExportJob> jobs;
    for (int i = 0; i < documents.size(); ++i) {
        const PhotoDocument &doc = documents.at(i);
        if (doc.meta.pixelSize.isEmpty())
            continue;   // 解码失败的导入残件不可导（无尺寸即无画布）

        const QString baseKey = QFileInfo(doc.sourcePath).completeBaseName();
        const int seq = baseSeq.value(baseKey, 0);
        baseSeq.insert(baseKey, seq + 1);
        QString name = ExportEngine::buildFileName(doc, options, seq);
        const QString target = dir.filePath(name);
        const bool exists = QFileInfo::exists(target)
                            || assigned.contains(name);

        // AskEach：逐个裁决（主线程串行，弹窗不阻塞已派发 worker）
        ConflictPolicy policy = options.conflict;
        if (exists && policy == ConflictPolicy::AskEach) {
            if (onConflict) {
                const auto choice = onConflict(name);
                if (choice == ConflictChoice::Overwrite)
                    policy = ConflictPolicy::Overwrite;
                else if (choice == ConflictChoice::Skip) {
                    ExportEngine::ItemResult skipped;
                    skipped.ok = false;
                    skipped.skipped = true;
                    skipped.sourcePath = doc.sourcePath;
                    skipped.outputPath = target;
                    m_results.append(skipped);
                    continue;
                } else {
                    policy = ConflictPolicy::AutoRename;
                }
            } else {
                policy = ConflictPolicy::AutoRename;   // 无裁决者回落
            }
        }

        if (exists && policy == ConflictPolicy::Skip) {
            ExportEngine::ItemResult skipped;
            skipped.ok = false;
            skipped.skipped = true;
            skipped.sourcePath = doc.sourcePath;
            skipped.outputPath = target;
            m_results.append(skipped);
            continue;
        }
        if (policy == ConflictPolicy::AutoRename
            && (exists || assigned.contains(name))) {
            // name(n)：从 (1) 起找空位（含批内撞名——预扫串行保证无竞态）
            const QString stem = name.left(name.size()
                                           - options.outputSuffix().size());
            for (int n = 1;; ++n) {
                name = QStringLiteral("%1(%2)%3")
                           .arg(stem).arg(n)
                           .arg(options.outputSuffix());
                if (!QFileInfo::exists(dir.filePath(name))
                    && !assigned.contains(name))
                    break;
            }
        }
        assigned.insert(name);

        ExportJob job;
        job.req.doc = doc;
        job.req.options = options;
        job.req.outputPath = dir.filePath(name);
        job.req.index = seq;   // 与 preplan 命名同源的组内序号（诊断/日志用）
        job.req.resolver = resolvers.at(i);
        job.req.logo = logos.at(i);
        jobs.append(job);
    }

    if (jobs.isEmpty()) {
        emit finished(m_results, false, m_timer.elapsed());
        return;
    }

    m_busy = true;

    // jobs 必须活到 map 结束（QtConcurrent::map 按引用原地改容器），
    // 先移入成员再发起
    m_jobs = std::move(jobs);

    // map 原地写 job.result；functor 捕获原子量与主线程回调
    auto *watcher = new QFutureWatcher<void>(this);
    m_watcher = watcher;
    connect(watcher, &QFutureWatcher<void>::finished, this,
            &ExportController::onFinished);
    const int total = m_jobs.size();
    auto progressCb = [this](int done, int totalNow, const QString &file) {
        emit progressChanged(done, totalNow, file);
    };
    const QFuture<void> future = QtConcurrent::map(
        m_jobs,
        [this, total, progressCb](ExportJob &job) {
            runJob(job, m_cancelRequested, m_doneCount, total, progressCb);
        });
    watcher->setFuture(future);
}

void ExportController::cancel()
{
    m_cancelRequested = true;
    if (m_watcher)
        m_watcher->cancel();
}

void ExportController::onFinished()
{
    m_busy = false;
    const bool canceled = m_cancelRequested.load();
    if (m_watcher) {
        m_watcher->deleteLater();
        m_watcher = nullptr;
    }
    for (const ExportJob &job : std::as_const(m_jobs))
        m_results.append(job.result);
    m_jobs.clear();
    emit finished(m_results, canceled, m_timer.elapsed());
}

} // namespace fxt
