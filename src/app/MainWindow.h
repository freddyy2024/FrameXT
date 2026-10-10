#pragma once

#include <QLabel>
#include <QMainWindow>
#include <QSet>
#include <QUuid>

class QComboBox;
class QSplitter;
class QTimer;
class QToolButton;

namespace fxt {
class BasicInfoPanel;
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
    void showElementContextMenu(const QUuid &elementId, const QPoint &globalPos);
    void showToast(const QString &text);
    void showAbout();
    void restoreWindowState();   // QSettings：几何/分割器/最近目录（3.10）
    void saveWindowState();
    void updateEmptyState();
    void confirmClear();

    TitleBar *m_titleBar = nullptr;
    QSplitter *m_mainSplitter = nullptr;

    // 控制器
    ProjectController *m_project = nullptr;
    ImportController *m_import = nullptr;

    // 面板
    BasicInfoPanel *m_basicInfo = nullptr;
    PhotoListPanel *m_photoList = nullptr;
    WatermarkPanel *m_watermarkPanel = nullptr;
    PreviewCanvas *m_canvas = nullptr;
    QLabel *m_canvasHint = nullptr;      // 底部工具条提示
    QComboBox *m_templateCombo = nullptr;
    QToolButton *m_compareButton = nullptr;
    QLabel *m_importSummary = nullptr;   // 左栏导入状态行
    QLabel *m_toast = nullptr;           // 批量操作结果提示（3s 自动消失）
    QTimer *m_toastTimer = nullptr;
    QString m_lastDir;                   // 最近导入目录（3.10 持久化）
};

} // namespace fxt
