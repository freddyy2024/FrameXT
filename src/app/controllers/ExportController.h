#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QObject>

#include <atomic>

#include "export/ExportEngine.h"

namespace fxt {

// map 工作项：请求 + 原地写回的结果（QtConcurrent::map 直接改容器元素；
// 仿 ImportController::ImportItem 的头文件工作项先例）
struct ExportJob
{
    ExportEngine::ItemRequest req;
    ExportEngine::ItemResult result;
};

// 批量导出调度（开发计划 4.1 / 02-技术设计 §4.7）：
// QtConcurrent::map 并行（每张独立 PhotoDocument 快照，无共享可变状态），
// 支持取消（已导出保留）、写失败重试 1 次、进度上报与失败清单收集。
//
// 冲突消解在本类主线程预分配（preplan）：worker 拿到的 outputPath 已保证
// 按 ConflictPolicy 处置完毕，文件系统上不存在「两个 worker 写同名」竞态。
class ExportController : public QObject
{
    Q_OBJECT

public:
    // AskEach 的单文件裁决（§5.3「每个文件询问」）：preplan 在主线程串行
    // 遇到同名时回调；返回值决定该文件去向。UI 层弹窗实现，测试可注桩。
    enum class ConflictChoice { Rename, Overwrite, Skip };
    using ConflictResolver =
        std::function<ConflictChoice(const QString &fileName)>;

    explicit ExportController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    // 并行度：idealThreadCount-1，钳位 [2,6]——全尺寸 45MP 渲染单 worker
    // 峰值 ~450MB（画布 QImage + 编码缓冲），无上限并行会击穿 8GB 机器
    static int workerCount();

public slots:
    // 预扫 + 派发。目录不存在/无可导项时发出 finished(0 汇总)。
    // 同名冲突策略在此执行：
    //   AutoRename → name(n).jpg 递增找空位；Overwrite → 直接用原名；
    //   Skip → 命中同名即标 skipped（不派发 worker）；
    //   AskEach → 逐个回调 resolver（未提供时回落 AutoRename）。
    void start(const QList<PhotoDocument> &documents,
               const QList<FieldValueResolver> &resolvers,
               const QList<QImage> &logos, const ExportOptions &options,
               const ConflictResolver &onConflict = {});

    // 取消：已导出的保留（§5.2 取消确认文案「已导出 n 张将保留」）
    void cancel();

signals:
    // done 为已完成数（含失败）；currentFile 为正在处理的源文件名
    void progressChanged(int done, int total, const QString &currentFile);
    // canceled=true 表示用户取消（非全部完成）
    void finished(const QList<fxt::ExportEngine::ItemResult> &results,
                  bool canceled, qint64 elapsedMs);

private:
    void onFinished();

    QList<ExportEngine::ItemResult> m_results;
    QList<ExportJob> m_jobs;   // map 容器（必须活到 future 结束）
    QFutureWatcher<void> *m_watcher = nullptr;
    QElapsedTimer m_timer;
    std::atomic<int> m_doneCount { 0 };
    std::atomic<bool> m_cancelRequested { false };
    bool m_busy = false;
};

} // namespace fxt

// ItemResult 经 queued signal（worker 无 emit，此处仅主线程 → 主线程
// watcher 回调）传递，无需 metatype 注册。
