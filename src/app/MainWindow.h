#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QSet>
#include <QUuid>

#include "export/ExportEngine.h"

class QComboBox;
class QSlider;
class QSplitter;
class QTimer;
class QToolButton;

namespace fxt {
enum class ApplyScope : int;   // ProjectController.h 定义（批量应用范围）
class BasicInfoPanel;
class ExportController;
class ExportPanel;
class ImportController;
class PhotoListPanel;
class PreviewCanvas;
class ProjectController;
class TitleBar;
class WatermarkPanel;

// 主窗口（04-UI设计.md §3）：恒定主界面三态（编辑/无图空态/纯看图）。
// 布局 = 标题栏 + 三栏 QSplitter（左：属性/导入导出；中：预览画布；
// 右：模板/水印内容）+ 底部照片列表。无照片时各区域就地禁用。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void changeEvent(QEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

#if defined(Q_OS_WIN)
    void showEvent(QShowEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    // 计算窗口坐标 pos 落在哪个边缘调整区（8px 命中带）
    Qt::Edges resizeEdgesAt(const QPoint &pos) const;
    // 根据边缘区切换全局调整光标形状
    void updateResizeCursor(Qt::Edges edges);
    // 清除调整光标
    void clearResizeCursor();
    // 可交互控件 hover 手型光标
    void updateInteractiveCursor(QWidget *widget);

    Qt::Edges m_activeEdges;
    bool m_resizeCursorActive = false;
#endif

private:
    void buildUi();
    QWidget *buildLeftPanel();
    QWidget *buildCenterPanel();
    QWidget *buildRightPanel();
    QWidget *buildBottomPanel();
    void importPaths(const QStringList &paths);
    void refreshBasicInfo();
    void refreshWatermarkPanel();
    void syncTemplateCombo();   // 模板下拉回同步（切照片/撤销后）
    void syncTemplateSliders();   // 高频模板参数滑杆回同步（M5.1）
    void runBatchApply(ApplyScope scope);   // §5.5 确认框+应用+toast
    void showElementContextMenu(const QUuid &elementId, const QPoint &globalPos);
    void showToast(const QString &text);
    void showAbout();
    void restoreWindowState();   // QSettings：几何/分割器/最近目录（3.10）
    void saveWindowState();
    void updateEmptyState();
    void confirmClear();

    // —— 导出（M4，§3.3/§5.2/§5.3）——
    void startExport(const ExportOptions &options);
    void showExportFinished(const QList<ExportEngine::ItemResult> &results,
                            bool canceled, qint64 elapsedMs);
    void showExportFailures(const QList<ExportEngine::ItemResult> &failedItems);
    void saveDiagnostics(const QList<ExportEngine::ItemResult> &failedItems);
    void restoreExportPrefs();   // QSettings：导出预设（§4.8）
    void saveExportPrefs(const ExportOptions &options);

    TitleBar *m_titleBar = nullptr;
    QSplitter *m_mainSplitter = nullptr;
    QSplitter *m_rootSplitter = nullptr;   // 上三栏+下列表（3.10 持久化）

    // 控制器
    ProjectController *m_project = nullptr;
    ImportController *m_import = nullptr;
    ExportController *m_export = nullptr;
    int m_exportDone = 0;   // 进度缓存（取消确认「已导出 n 张将保留」）

    // 面板
    BasicInfoPanel *m_basicInfo = nullptr;
    PhotoListPanel *m_photoList = nullptr;
    WatermarkPanel *m_watermarkPanel = nullptr;
    PreviewCanvas *m_canvas = nullptr;
    QToolButton *m_undoButton = nullptr;   // 画布工具条撤销（用户指令：不在列表模块）
    QLabel *m_canvasHint = nullptr;      // 底部工具条提示
    QComboBox *m_templateCombo = nullptr;
    QSlider *m_bandSlider = nullptr;      // 高频参数：信息带高度（bottom/around）
    QSlider *m_cornerSlider = nullptr;    // 高频参数：圆角（blur）
    QSlider *m_blurSlider = nullptr;      // 高频参数：模糊强度（blur）
    QLabel *m_bandValue = nullptr;
    QLabel *m_cornerValue = nullptr;
    QLabel *m_blurValue = nullptr;
    QLabel *m_importSummary = nullptr;   // 左栏导入状态行
    ExportPanel *m_exportPanel = nullptr;   // 导出表单（§3.3）
    QLabel *m_toast = nullptr;           // 批量操作结果提示（3s 自动消失）
    QTimer *m_toastTimer = nullptr;
    QString m_lastDir;                   // 最近导入目录（3.10 持久化）
    bool m_exitConfirmed = false;        // 语言切换自重启路径跳过退出确认
    // 切换照片后待适配：新预览渲染完成时画布重置为适配居中（用户指令），
    // 同文档重渲染（防抖/编辑）不重置、保持用户当前缩放平移
    bool m_pendingFitView = false;
};

} // namespace fxt
