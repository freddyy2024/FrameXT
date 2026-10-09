#pragma once

#include <QMainWindow>

class TitleBar;

// 主窗口（04-UI设计.md §3）：恒定主界面三态设计（编辑 / 无图空态 / 纯看图）——
// 框架为 frameless 自绘标题栏 + 左（属性/导入导出）+ 中（预览画布）+ 右（模板/水印内容）
// + 底部照片列表。无照片时各区域就地禁用，不切换界面。
//
// 无边框窗口方案（Qt 6.11）：FramelessWindowHint + 纯 Qt 手势
// （startSystemMove/startSystemResize），不拦截非客户区原生消息 —— 详见构造函数注释。
class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow();

protected:
    void changeEvent(QEvent *event) override;

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
    // 可交互控件 hover 手型光标（M3 起新增交互控件自动覆盖）
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

    TitleBar *m_titleBar = nullptr;
};
