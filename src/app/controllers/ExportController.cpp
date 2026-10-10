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
        return;   // 取消后未派发的项保持默认 result（onFinished 统一补标记）
    job.result = ExportEngine::exportSingle(job.req);
    const int done = doneCount.fetch_add(1) + 1;
    // 跨线程 emit 由 Qt 的 queued connection 排队到主线程
    progress(done, total, QFileInfo(job.req.doc.sourcePath).fileName());
}

// Windows 文件系统大小写不敏感：批内占用检测必须折叠大小写，
// 否则 a.jpg / A.jpg 绕过检测双写同一文件（评审 P1-2）
bool nameOccupied(const QSet<QString> &assigned, const QString &name)
{
    for (const QString &taken : assigned) {
        if (taken.compare(name, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

} // namespace

ExportController::ExportController(QObject *parent)
    : QObject(parent)
{
}

ExportController::~ExportController()
{
    // 生命周期兜底（评审 P0-1 实锤 UAF）：worker 持 this/m_jobs 引用，
    // 而 QFutureWatcher 析构不等待 future——析构函数体内成员尚存活，
    // 阻塞汇合后再放行成员析构。单 worker 上界为一张的渲染+编码时长。
    if (m_watcher && m_watcher->isRunning()) {
        m_cancelRequested = true;
        m_watcher->future().waitForFinished();
    }
}

int ExportController::workerCount()
{
    const int cores = QThread::idealThreadCount();
    return qBound(2, cores - 1, 6);
}

ExportController::PreplanResult
ExportController::preplan(const QList<PhotoDocument> &documents,
                          const QList<FieldValueResolver> &resolvers,
                          const QList<QImage> &logos,
                          const ExportOptions &options,
                          const ConflictResolver &onConflict)
{
    PreplanResult plan;
    Q_ASSERT(resolvers.size() == documents.size()
             && logos.size() == documents.size());   // 三数组等长契约

    const QDir dir(options.outputDir);
    if (!dir.exists()) {
        plan.error = QStringLiteral("导出目录不存在：%1").arg(options.outputDir);
        return plan;
    }

    // —— 预扫：主线程串行预分配全部输出名（冲突消解，§5.3）——
    // AskEach 弹窗嵌套事件循环期间可能重入：闸门由调用方（start/runPrepared）
    // 的 m_busy 检查保证；本函数只读入参，无成员写。
    QSet<QString> assigned;      // 本批内已占用的目标名（大小写折叠比对）
    QHash<QString, int> baseSeq; // 同基名组内序号（命名引擎语义，见 buildFileName）
    for (int i = 0; i < documents.size(); ++i) {
        const PhotoDocument &doc = documents.at(i);
        if (doc.meta.pixelSize.isEmpty())
            continue;   // 解码失败的导入残件不可导（无尺寸即无画布）

        // AskEach 串行弹窗期间用户取消：立即停止询问，余下按 Skip 收尾
        if (m_cancelRequested.load(std::memory_order_relaxed)) {
            ExportEngine::ItemResult skipped;
            skipped.skipped = true;
            skipped.sourcePath = doc.sourcePath;
            plan.skipped.append(skipped);
            continue;
        }

        const QString baseKey = QFileInfo(doc.sourcePath).completeBaseName();
        const int seq = baseSeq.value(baseKey, 0);
        baseSeq.insert(baseKey, seq + 1);
        QString name = ExportEngine::buildFileName(doc, options, seq);
        const QString target = dir.filePath(name);
        const bool onDisk = QFileInfo::exists(target);
        const bool inBatch = nameOccupied(assigned, name);

        ConflictPolicy policy = options.conflict;
        if ((onDisk || inBatch) && policy == ConflictPolicy::AskEach) {
            if (onConflict) {
                const auto choice = onConflict(name);
                if (choice == ConflictChoice::Overwrite)
                    policy = ConflictPolicy::Overwrite;
                else if (choice == ConflictChoice::Skip)
                    policy = ConflictPolicy::Skip;
                else
                    policy = ConflictPolicy::AutoRename;
            } else {
                policy = ConflictPolicy::AutoRename;   // 无裁决者回落
            }
        }

        if ((onDisk || inBatch) && policy == ConflictPolicy::Skip) {
            ExportEngine::ItemResult skipped;
            skipped.ok = false;
            skipped.skipped = true;
            skipped.sourcePath = doc.sourcePath;
            skipped.outputPath = target;
            plan.skipped.append(skipped);
            continue;
        }
        if (policy == ConflictPolicy::Overwrite) {
            if (inBatch) {
                // 批内同目标双写 = 静默丢文件 + 虚报成功（评审 A2 实测
                // 复现）——Overwrite 只对磁盘已有文件生效，批内让位
                policy = ConflictPolicy::AutoRename;
            } else if (onDisk) {
                plan.overwriteHits.append(name);   // 破坏性操作待 UI 确认
            }
        }
        if (policy == ConflictPolicy::AutoRename
            && (onDisk || inBatch)) {
            // name(n)：从 (1) 起找空位（§5.3 自动重命名语义）
            const QString stem = name.left(name.size()
                                           - options.outputSuffix().size());
            for (int n = 1;; ++n) {
                name = QStringLiteral("%1(%2)%3")
                           .arg(stem).arg(n)
                           .arg(options.outputSuffix());
                if (!QFileInfo::exists(dir.filePath(name))
                    && !nameOccupied(assigned, name))
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
        plan.jobs.append(job);
    }
    return plan;
}

void ExportController::start(const QList<PhotoDocument> &documents,
                             const QList<FieldValueResolver> &resolvers,
                             const QList<QImage> &logos,
                             const ExportOptions &options,
                             const ConflictResolver &onConflict)
{
    if (m_busy)
        return;

    // Overwrite 的破坏性确认由 UI 层在 preplan 后、runPrepared 前完成
    // （§5.3）；本组合入口保持「策略即执行」语义，供离屏测试使用。
    PreplanResult plan = preplan(documents, resolvers, logos, options,
                                 onConflict);
    if (!plan.error.isEmpty()) {
        QList<ExportEngine::ItemResult> empty;
        emit finished(empty, false, 0);
        return;
    }
    runPrepared(std::move(plan));
}

void ExportController::runPrepared(PreplanResult &&plan)
{
    if (m_busy)
        return;

    m_results = plan.skipped;
    m_jobs = std::move(plan.jobs);
    if (m_jobs.isEmpty()) {
        emit finished(m_results, false, 0);
        return;
    }

    // 闸门在派发前置位：AskEach 弹窗嵌套事件循环期间重入的 start()
    // 必须被挡住（评审 P1-1）
    m_busy = true;
    m_cancelRequested = false;
    m_doneCount = 0;
    m_timer.start();

    // jobs 必须活到 map 结束（QtConcurrent::map 按引用原地改容器），
    // 已移入成员
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
    for (ExportJob &job : m_jobs) {
        if (!job.result.ok && !job.result.skipped && !job.result.canceled
            && canceled && job.result.stage.isEmpty()) {
            // 未派发项（取消窗口内跳过）与真实失败不可混淆——API 语义
            // 地雷排除（评审 A6/P2-3）：消费方按 !ok 统计不会再重跑
            job.result.canceled = true;
        }
        m_results.append(job.result);
    }
    m_jobs.clear();
    emit finished(m_results, canceled, m_timer.elapsed());
}

} // namespace fxt
