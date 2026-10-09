#include "MainWindow.h"

#include "TitleBar.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QSplitter>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

// 布局常量（04-UI设计.md §1.3 / §3；原版默认窗体 1270×950）
constexpr int kDefaultWindowWidth = 1270;
constexpr int kDefaultWindowHeight = 950;
constexpr int kMinWindowWidth = 1000;
constexpr int kMinWindowHeight = 680;
constexpr int kPanelSpacing = 8;
constexpr int kPhotoListHeight = 176;

#if defined(Q_OS_WIN)
// 窗口边缘的拖拽调整命中带（逻辑像素）
constexpr int kResizeBorderLogical = 8;
#endif

// 通用卡片：32 高标题行（12 SemiBold）+ 内容区（04-UI设计.md §3.2 面板结构）
QFrame *createCard(const QString &title, const QString &countHint, QWidget *content)
{
    auto *card = new QFrame;
    card->setObjectName("card");
    auto *layout = new QVBoxLayout(card);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    auto *header = new QWidget(card);
    header->setObjectName("panelHeader");
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 12, 0);
    auto *titleLabel = new QLabel(title, header);
    titleLabel->setObjectName("panelTitle");
    headerLayout->addWidget(titleLabel);
    headerLayout->addStretch();
    if (!countHint.isEmpty()) {
        auto *count = new QLabel(countHint, header);
        count->setObjectName("panelCount");
        headerLayout->addWidget(count);
    }
    layout->addWidget(header);
    if (content)
        layout->addWidget(content, 1);
    return card;
}

// 居中的灰字提示（空态引导文案）
QLabel *createHintLabel(const QString &text, const char *objectName = "emptyHint")
{
    auto *label = new QLabel(text);
    label->setObjectName(QString::fromLatin1(objectName));
    label->setAlignment(Qt::AlignCenter);
    label->setWordWrap(true);
    return label;
}

} // namespace

MainWindow::MainWindow()
{
    // Qt 6.11 无边框定式（0xC000041D 二分实测结论）：
    //  1) 必须用 Qt::FramelessWindowHint；不得拦截 WM_NCCALCSIZE / WM_NCHITTEST
    //     （Qt 已内部接管非客户区消息，应用层拦截即崩溃）；
    //  2) showEvent 回补 THICKFRAME|MAXIMIZEBOX|MINIMIZEBOX|SYSMENU（不含 WS_CAPTION），
    //     获得键盘 Win+方向键 Snap 与任务栏动画；
    //  3) 拖拽/双击最大化/边缘调整用 Qt 层手势（startSystemMove/Resize）。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    resize(kDefaultWindowWidth, kDefaultWindowHeight);
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);
    buildUi();

#if defined(Q_OS_WIN)
    // 应用级事件过滤器：边缘调整 + 可交互控件手型光标，全局生效
    qApp->installEventFilter(this);
#endif
}

void MainWindow::buildUi()
{
    auto *central = new QWidget(this);
    central->setObjectName("centralArea");
    setCentralWidget(central);

    auto *rootLayout = new QVBoxLayout(central);
    // 留 1px 让出 #centralArea 的 QSS 边框：子控件若从 (0,0) 铺开会用底色盖掉边框
    // （普通 QWidget 的 QSS 边框不会自动收缩布局可用区，标题栏顶部边框曾因此不可见）
    rootLayout->setContentsMargins(1, 1, 1, 1);
    rootLayout->setSpacing(0);

    // 标题栏：三按钮 + 语言菜单
    m_titleBar = new TitleBar(central);
    connect(m_titleBar, &TitleBar::minimizeClicked, this, &QWidget::showMinimized);
    connect(m_titleBar, &TitleBar::maximizeClicked, this, [this] {
        isMaximized() ? showNormal() : showMaximized();
    });
    connect(m_titleBar, &TitleBar::closeClicked, this, &QWidget::close);
    rootLayout->addWidget(m_titleBar);

    // 中部三栏（QSplitter，可拖动调宽）
    auto *mainSplitter = new QSplitter(Qt::Horizontal, central);
    mainSplitter->setObjectName("mainSplitter");
    mainSplitter->setChildrenCollapsible(false);
    mainSplitter->setHandleWidth(kPanelSpacing);
    mainSplitter->addWidget(buildLeftPanel());
    mainSplitter->addWidget(buildCenterPanel());
    mainSplitter->addWidget(buildRightPanel());
    mainSplitter->setStretchFactor(0, 0);
    mainSplitter->setStretchFactor(1, 1);
    mainSplitter->setStretchFactor(2, 0);
    mainSplitter->setSizes({ 260, 714, 288 });

    // 上：三栏工作区；下：照片列表
    auto *rootSplitter = new QSplitter(Qt::Vertical, central);
    rootSplitter->setObjectName("rootSplitter");
    rootSplitter->setChildrenCollapsible(false);
    rootSplitter->setHandleWidth(kPanelSpacing);
    rootSplitter->addWidget(mainSplitter);
    rootSplitter->addWidget(buildBottomPanel());
    rootSplitter->setStretchFactor(0, 1);
    rootSplitter->setStretchFactor(1, 0);
    rootSplitter->setSizes({ 770, kPhotoListHeight });

    rootLayout->addWidget(rootSplitter, 1);

    setWindowTitle("FrameXT");
}

QWidget *MainWindow::buildLeftPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("sidePanel");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(kPanelSpacing, kPanelSpacing, 0, 0);
    layout->setSpacing(kPanelSpacing);

    // 基础属性卡（M3 接入真实字段。注意：空态禁用只能作用于控件，
    // 不能 setEnabled(false) 到布局——布局被禁用后会停止排列子项，
    // 提示文字将停留在左上角，M0 复现过该问题）
    auto *basicContent = new QWidget;
    basicContent->setObjectName("cardBody");
    auto *basicLayout = new QVBoxLayout(basicContent);
    basicLayout->setContentsMargins(0, 0, 0, 0);
    basicLayout->addWidget(createHintLabel(
        tr("导入照片后，这里显示拍摄参数\n（机型 / 镜头 / 光圈快门 / 焦距 等）")));
    layout->addWidget(createCard(tr("基础属性"), QString(), basicContent), 1);

    // 导入导出卡
    auto *ioContent = new QWidget;
    ioContent->setObjectName("cardBody");
    auto *ioLayout = new QVBoxLayout(ioContent);
    ioLayout->setContentsMargins(12, 10, 12, 12);
    ioLayout->setSpacing(8);
    auto *importButton = new QPushButton(tr("导入照片…"), ioContent);
    importButton->setObjectName("primaryButton");
    ioLayout->addWidget(importButton);
    ioLayout->addWidget(new QPushButton(tr("导出"), ioContent));
    ioLayout->addStretch();
    ioContent->setEnabled(false);
    layout->addWidget(createCard(tr("导入导出"), QString(), ioContent));

    return panel;
}

QWidget *MainWindow::buildCenterPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("centerPanel");
    auto *layout = new QVBoxLayout(panel);
    // 中栏四边不设贴边距：与左右栏的间距由 splitter 手柄(8px)负责，
    // 仅保留顶部间距对齐左右栏（间距体系见 04-UI设计.md 8px 网格）
    layout->setContentsMargins(0, kPanelSpacing, 0, 0);

    // 预览画布（M1/M3 接入 QGraphicsView；当前为空态引导）
    auto *canvas = new QFrame(panel);
    canvas->setObjectName("previewCanvas");
    auto *canvasLayout = new QVBoxLayout(canvas);
    canvasLayout->setContentsMargins(24, 0, 24, 0);

    auto *mainHint = new QLabel(tr("将照片拖入此处开始"), canvas);
    mainHint->setObjectName("emptyHintPrimary");
    mainHint->setAlignment(Qt::AlignCenter);
    QFont hintFont = mainHint->font();
    hintFont.setPixelSize(15);
    hintFont.setWeight(QFont::DemiBold);
    mainHint->setFont(hintFont);

    canvasLayout->addStretch();
    canvasLayout->addWidget(mainHint);
    canvasLayout->addWidget(createHintLabel(
        tr("或点击左侧「导入照片」 · 支持 JPG / PNG / WebP")));
    canvasLayout->addStretch();

    layout->addWidget(canvas, 1);
    return panel;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("sidePanel");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, kPanelSpacing, kPanelSpacing, 0);
    layout->setSpacing(kPanelSpacing);

    // 模板卡（M3 接入参数控件、M5 模板参数化；空态时控件级禁用）
    auto *templateContent = new QWidget;
    templateContent->setObjectName("cardBody");
    auto *templateLayout = new QVBoxLayout(templateContent);
    templateLayout->setContentsMargins(12, 10, 12, 12);
    templateLayout->setSpacing(8);
    auto *templateCombo = new QComboBox(templateContent);
    templateCombo->addItems({ tr("底部边框"), tr("环绕边框"), tr("高斯模糊") });
    templateLayout->addWidget(templateCombo);
    auto *templateHint = createHintLabel(tr("选择照片后可调整模板参数"));
    templateHint->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    templateLayout->addWidget(templateHint);
    templateLayout->addStretch();
    templateContent->setEnabled(false);
    layout->addWidget(createCard(tr("模板"), QString(), templateContent));

    // 水印内容卡：开关行列表（M3 落地，回应 R1）
    auto *watermarkContent = new QWidget;
    watermarkContent->setObjectName("cardBody");
    auto *watermarkLayout = new QVBoxLayout(watermarkContent);
    watermarkLayout->setContentsMargins(0, 0, 0, 0);
    watermarkLayout->addWidget(createHintLabel(
        tr("导入照片后，可在此逐项开启\n机型 / 镜头 / 参数 / 自定义文本等水印内容")));
    layout->addWidget(createCard(tr("水印内容"), QString(), watermarkContent), 1);

    return panel;
}

QWidget *MainWindow::buildBottomPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("bottomPanel");
    auto *layout = new QVBoxLayout(panel);
    // 顶部间距由 rootSplitter 手柄(8px)负责，避免与面板边距叠加成 16px
    layout->setContentsMargins(kPanelSpacing, 0, kPanelSpacing, kPanelSpacing);
    layout->setSpacing(0);

    auto *listArea = new QWidget;
    listArea->setObjectName("cardBody");
    auto *listLayout = new QVBoxLayout(listArea);
    listLayout->setContentsMargins(0, 0, 0, 0);

    auto *photoList = new QListWidget(listArea);
    photoList->setObjectName("photoList");
    photoList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    photoList->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);

    // 空态提示与列表互斥；M3 随照片数量切换
    photoList->setVisible(false);
    listLayout->addWidget(photoList);
    listLayout->addWidget(createHintLabel(
        tr("尚未导入照片 · 拖入照片或点击「导入照片」开始")));

    layout->addWidget(createCard(tr("照片列表"), tr("0 张"), listArea));
    return panel;
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::WindowStateChange && m_titleBar)
        m_titleBar->updateMaximizeButton(isMaximized());
}

#if defined(Q_OS_WIN)

void MainWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);

    // 无边框窗口回补可调尺寸样式（不含 WS_CAPTION —— 与 FramelessWindowHint 冲突）：
    // 获得键盘 Win+方向键 Snap、任务栏最小化/还原动画等系统行为。
    const HWND hwnd = reinterpret_cast<HWND>(winId());
    const LONG_PTR style = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
    constexpr LONG_PTR kWantedStyles =
        WS_MAXIMIZEBOX | WS_MINIMIZEBOX | WS_THICKFRAME | WS_SYSMENU;
    if ((style & kWantedStyles) != kWantedStyles) {
        ::SetWindowLongPtrW(hwnd, GWL_STYLE, style | kWantedStyles);
        ::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                       SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
                           | SWP_FRAMECHANGED);
    }

    // Win11 系统级窗口圆角（资源管理器同款）：半径由系统决定（≈8px，不可自定义），
    // 附带原生抗锯齿与阴影；Win10 无此属性，调用静默失败保持直角。
    // 注：精确 5px 圆角需 WA_TranslucentBackground 自绘，会牺牲阴影/键盘贴靠，不采用。
    constexpr DWORD kDwmwaWindowCornerPreference = 33;
    constexpr DWORD kDwmwcpRound = 2;
    ::DwmSetWindowAttribute(hwnd, kDwmwaWindowCornerPreference,
                            &kDwmwcpRound, sizeof(kDwmwcpRound));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::HoverEnter:
    case QEvent::HoverMove: {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget->window() != this)
            break;

        // 边缘调整区优先：8px 命中带内显示调整光标
        const Qt::Edges edges = isMaximized() || isMinimized()
                                    ? Qt::Edges{}
                                    : resizeEdgesAt(mapFromGlobal(QCursor::pos()));
        if (edges) {
            updateResizeCursor(edges);
            break;
        }
        clearResizeCursor();
        updateInteractiveCursor(widget);
        break;
    }
    case QEvent::HoverLeave: {
        // 仅回收我们设置过的光标，控件自带的特殊光标（如分割条手柄）不动
        if (auto *widget = qobject_cast<QWidget *>(watched)) {
            const auto shape = widget->cursor().shape();
            if (shape == Qt::ArrowCursor || shape == Qt::PointingHandCursor)
                widget->unsetCursor();
        }
        break;
    }
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        auto *widget = qobject_cast<QWidget *>(watched);
        if (!widget || widget->window() != this)
            break;

        const QPoint global =
            static_cast<QMouseEvent *>(event)->globalPosition().toPoint();
        const Qt::Edges edges = resizeEdgesAt(mapFromGlobal(global));

        if (event->type() == QEvent::MouseButtonPress
            && edges
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            // 命中边缘 8px 带 → 进入原生尺寸调整循环并吞掉该次按下
            if (windowHandle() && windowHandle()->startSystemResize(edges))
                return true;
        } else if (event->type() == QEvent::MouseMove) {
            updateResizeCursor(edges);
        } else if (event->type() == QEvent::MouseButtonRelease) {
            clearResizeCursor();
        }
        break;
    }
    default:
        break;
    }
    return QMainWindow::eventFilter(watched, event);
}

// 可交互控件 hover 手型光标（按钮/下拉/复选/单选；M3 起新增交互控件自动覆盖）
void MainWindow::updateInteractiveCursor(QWidget *widget)
{
    const bool interactive = widget->isEnabled()
        && (qobject_cast<QPushButton *>(widget)
            || qobject_cast<QToolButton *>(widget)
            || qobject_cast<QComboBox *>(widget)
            || qobject_cast<QCheckBox *>(widget)
            || qobject_cast<QRadioButton *>(widget));
    const Qt::CursorShape want = interactive ? Qt::PointingHandCursor
                                             : Qt::ArrowCursor;
    const auto current = widget->cursor().shape();
    // 分割条手柄等控件自带的特殊光标不覆盖；只管理手型/箭头切换
    if (current != Qt::PointingHandCursor && current != Qt::ArrowCursor)
        return;
    if (current != want)
        widget->setCursor(want);
}

Qt::Edges MainWindow::resizeEdgesAt(const QPoint &pos) const
{
    if (isMaximized() || isMinimized())
        return {};

    const int border = kResizeBorderLogical;
    Qt::Edges edges;
    if (pos.y() < border)
        edges.setFlag(Qt::TopEdge);
    if (pos.y() >= height() - border)
        edges.setFlag(Qt::BottomEdge);
    if (pos.x() < border)
        edges.setFlag(Qt::LeftEdge);
    if (pos.x() >= width() - border)
        edges.setFlag(Qt::RightEdge);
    return edges;
}

void MainWindow::updateResizeCursor(Qt::Edges edges)
{
    if (edges == m_activeEdges)
        return;
    m_activeEdges = edges;

    Qt::CursorShape shape = Qt::ArrowCursor;
    if ((edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::TopEdge))
        || (edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::BottomEdge)))
        shape = Qt::SizeFDiagCursor;
    else if ((edges.testFlag(Qt::RightEdge) && edges.testFlag(Qt::TopEdge))
             || (edges.testFlag(Qt::LeftEdge) && edges.testFlag(Qt::BottomEdge)))
        shape = Qt::SizeBDiagCursor;
    else if (edges.testFlag(Qt::LeftEdge) || edges.testFlag(Qt::RightEdge))
        shape = Qt::SizeHorCursor;
    else if (edges.testFlag(Qt::TopEdge) || edges.testFlag(Qt::BottomEdge))
        shape = Qt::SizeVerCursor;

    if (m_resizeCursorActive) {
        QApplication::restoreOverrideCursor();
        m_resizeCursorActive = false;
    }
    if (shape != Qt::ArrowCursor) {
        QApplication::setOverrideCursor(QCursor(shape));
        m_resizeCursorActive = true;
    }
}

void MainWindow::clearResizeCursor()
{
    m_activeEdges = {};
    if (m_resizeCursorActive) {
        QApplication::restoreOverrideCursor();
        m_resizeCursorActive = false;
    }
}

#endif // Q_OS_WIN
