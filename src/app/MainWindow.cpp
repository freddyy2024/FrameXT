#include "MainWindow.h"

#include "AppInfo.h"
#include "Theme.h"
#include "TitleBar.h"
#include "canvas/PreviewCanvas.h"
#include "controllers/ExportController.h"
#include "controllers/ImportController.h"
#include "controllers/ProjectController.h"
#include "panels/BasicInfoPanel.h"
#include "panels/ExportPanel.h"
#include "panels/PhotoListPanel.h"
#include "panels/WatermarkPanel.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDialog>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileDialog>
#include <QFrame>
#include <QDesktopServices>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QDate>
#include <QDir>
#include <QHeaderView>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTextStream>
#include <QProcess>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSettings>
#include <QShortcut>
#include <QSlider>
#include <QSplitter>
#include <QTimer>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWindow>

#if defined(Q_OS_WIN)
#include <dwmapi.h>
#include <windows.h>
#endif

namespace {

// 布局常量（04-UI设计.md §1.3 / §3；原版默认窗体 1270×950，
// 最小 960×600 为设计稿 §3 承诺值——1366×768 小屏全功能可达）
constexpr int kDefaultWindowWidth = 1270;
constexpr int kDefaultWindowHeight = 950;
constexpr int kMinWindowWidth = 960;
constexpr int kMinWindowHeight = 600;
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

namespace fxt {

MainWindow::MainWindow()
{
    // Qt 6.11 无边框定式（0xC000041D 二分实测结论）：
    //  1) 必须用 Qt::FramelessWindowHint；不得拦截 WM_NCCALCSIZE / WM_NCHITTEST
    //     （Qt 已内部接管非客户区消息，应用层拦截即崩溃）；
    //  2) showEvent 回补 THICKFRAME|MAXIMIZEBOX|MINIMIZEBOX|SYSMENU（不含 WS_CAPTION），
    //     获得键盘 Win+方向键 Snap 与任务栏动画；
    //  3) 拖拽/双击最大化/边缘调整用 Qt 层手势（startSystemMove/Resize）。
    setWindowFlags(windowFlags() | Qt::FramelessWindowHint);
    // 初始尺寸按主屏可用几何收敛：1366×768 屏直接给 1270×950 会底部出屏，
    // 左栏导出面板不可见（反馈源 R9 高频痛点）
    if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        const QRect avail = screen->availableGeometry();
        resize(qMin(kDefaultWindowWidth, avail.width()),
               qMin(kDefaultWindowHeight, avail.height()));
    } else {
        resize(kDefaultWindowWidth, kDefaultWindowHeight);
    }
    setMinimumSize(kMinWindowWidth, kMinWindowHeight);
    buildUi();

#if defined(Q_OS_WIN)
    // 应用级事件过滤器：边缘调整 + 可交互控件手型光标，全局生效
    qApp->installEventFilter(this);
#endif
}

void MainWindow::buildUi()
{
    // —— 控制器（装配层：UI 只持指针，业务状态全在 controller）——
    m_project = new ProjectController(this);
    m_import = new ImportController(this);
    m_export = new ExportController(this);

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
    // 纯看图开关（👁，§3.4 第三态入口）
    connect(m_titleBar, &TitleBar::watermarkEyeToggled, m_project,
            &ProjectController::setWatermarkHidden);
    connect(m_titleBar, &TitleBar::languageSelected, this,
            [this](const QString &code) {
                // i18n（3.9）：写持久化 key，重启后生效（自重启最可靠，
                // 避免遍历 retranslate 全部自绘控件）
                QSettings settings;
                const QString current =
                    settings.value(QStringLiteral("app/language"),
                                   QStringLiteral("zh-CN")).toString();
                if (current == code)
                    return;
                settings.setValue(QStringLiteral("app/language"), code);
                const auto answer = QMessageBox::question(
                    this, tr("切换界面语言"),
                    tr("语言将在重启应用后生效。立即重启吗？"));
                if (answer != QMessageBox::Yes) {
                    // 不重启则回滚 key（避免下次启动语言「突变」）
                    settings.setValue(QStringLiteral("app/language"),
                                      current);
                    return;
                }
                QProcess::startDetached(QApplication::applicationFilePath());
                m_exitConfirmed = true;   // 自重启不经退出确认
                close();
            });
    rootLayout->addWidget(m_titleBar);

    // 中部三栏（QSplitter，可拖动调宽）
    m_mainSplitter = new QSplitter(Qt::Horizontal, central);
    m_mainSplitter->setObjectName("mainSplitter");
    m_mainSplitter->setChildrenCollapsible(false);
    m_mainSplitter->setHandleWidth(kPanelSpacing);
    m_mainSplitter->addWidget(buildLeftPanel());
    m_mainSplitter->addWidget(buildCenterPanel());
    m_mainSplitter->addWidget(buildRightPanel());
    m_mainSplitter->setStretchFactor(0, 0);
    m_mainSplitter->setStretchFactor(1, 1);
    m_mainSplitter->setStretchFactor(2, 0);
    m_mainSplitter->setSizes({ 260, 714, 288 });

    // 上：三栏工作区；下：照片列表
    m_rootSplitter = new QSplitter(Qt::Vertical, central);
    m_rootSplitter->setObjectName("rootSplitter");
    m_rootSplitter->setChildrenCollapsible(false);
    m_rootSplitter->setHandleWidth(kPanelSpacing);
    m_rootSplitter->addWidget(m_mainSplitter);
    m_rootSplitter->addWidget(buildBottomPanel());
    m_rootSplitter->setStretchFactor(0, 1);
    m_rootSplitter->setStretchFactor(1, 0);
    m_rootSplitter->setSizes({ 770, kPhotoListHeight });

    rootLayout->addWidget(m_rootSplitter, 1);

    // —— 导入流水线接线 ——
    setAcceptDrops(true);
    connect(m_import, &ImportController::documentReady, this,
            [this](const PhotoDocument &doc, const QImage &thumbnail) {
                m_project->appendDocument(doc);
                const QString name = QFileInfo(doc.sourcePath).fileName();
                m_photoList->addPhoto(doc.id, name, thumbnail);
            });
    connect(m_import, &ImportController::importFinished, this,
            [this](int ok, const QList<ImportFailure> &failures, qint64 ms) {
                updateEmptyState();
                if (ok > 0)
                    refreshBasicInfo();
                if (!failures.isEmpty()) {
                    QStringList reasons;
                    for (const ImportFailure &f : failures)
                        reasons += QStringLiteral("%1（%2）")
                                       .arg(QFileInfo(f.path).fileName(),
                                            f.reason);
                    QMessageBox::warning(this, tr("导入完成"),
                                         tr("成功 %1 张 · 失败 %2 张：\n%3")
                                             .arg(ok)
                                             .arg(failures.size())
                                             .arg(reasons.join(QStringLiteral("\n"))));
                }
                Q_UNUSED(ms);
            });

    // —— 导出流水线接线（§3.3 表单 → §5.2 进度/完成 → §5.3 冲突）——
    connect(m_exportPanel, &ExportPanel::exportRequested, this,
            [this](const ExportOptions &options) { startExport(options); });
    connect(m_exportPanel, &ExportPanel::cancelRequested, this, [this] {
        // §5.2：取消二次确认——「已导出 n 张将保留」
        const auto choice = QMessageBox::question(
            this, tr("取消导出"),
            tr("已导出 %1 张将保留。确定取消剩余任务吗？").arg(m_exportDone),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (choice == QMessageBox::Yes)
            m_export->cancel();
    });
    connect(m_export, &ExportController::progressChanged, this,
            [this](int done, int total, const QString &file) {
                m_exportDone = done;
                m_exportPanel->updateProgress(done, total, file);
            });
    connect(m_export, &ExportController::finished, this,
            &MainWindow::showExportFinished);

    // —— 项目状态接线 ——
    connect(m_project, &ProjectController::currentChanged, this,
            [this](const QUuid &id) {
                m_photoList->setCurrent(id);
                refreshBasicInfo();
                refreshWatermarkPanel();
                m_canvas->setSelectedElement(QUuid());
            });
    connect(m_project, &ProjectController::previewUpdated, this,
            [this](const QImage &image) {
                m_canvas->setPreview(image);
                // 渲染完成同步元素布局（画布命中/拖动/就地编辑数据源）
                m_canvas->setElementLayouts(m_project->elementLayouts());
            });
    connect(m_project, &ProjectController::undoAvailabilityChanged, this,
            [this](bool avail) { m_photoList->setUndoEnabled(avail); });
    connect(m_project, &ProjectController::watermarkHiddenChanged, this,
            [this](bool hidden) {
                m_canvas->setWatermarkHiddenHint(hidden);
                m_titleBar->updateWatermarkEyeButton(hidden);
            });
    connect(m_project, &ProjectController::renderError, this,
            [this](const QString &message) { showToast(message); });
    // documentChanged：左栏属性 + 右栏面板同步（画布入口的隐藏/置顶/
    // 还原位置、模板切换换元素树——不刷新则面板行携旧 id 全部静默失效）
    connect(m_project, &ProjectController::documentChanged, this,
            [this](const QUuid &) {
                refreshBasicInfo();
                refreshWatermarkPanel();
                syncTemplateCombo();
            });

    // —— 列表交互接线 ——
    connect(m_photoList, &PhotoListPanel::currentRequested, m_project,
            &ProjectController::setCurrentId);
    connect(m_photoList, &PhotoListPanel::selectionRequested, this,
            [this](const QSet<QUuid> &ids) { m_project->setSelection(ids); });
    connect(m_photoList, &PhotoListPanel::removeRequested, this,
            [this](const QList<QUuid> &ids) {
                for (const QUuid &id : ids)
                    m_photoList->removePhoto(id);
                m_project->removeDocuments(ids);
                updateEmptyState();
            });
    connect(m_photoList, &PhotoListPanel::removeUnselectedRequested, this,
            [this] {
                QList<QUuid> doomed;
                for (const auto &doc : m_project->documents()) {
                    if (!m_project->selection().contains(doc.id)
                        && doc.id != m_project->currentId())
                        doomed.append(doc.id);
                }
                for (const QUuid &id : doomed)
                    m_photoList->removePhoto(id);
                m_project->removeDocuments(doomed);
                updateEmptyState();
            });
    connect(m_photoList, &PhotoListPanel::clearRequested, this,
            &MainWindow::confirmClear);
    connect(m_photoList, &PhotoListPanel::undoRequested, this, [this] {
        m_project->undo();
        refreshWatermarkPanel();   // 撤销可能回退元素树结构
    });
    // 列表右键「应用当前设置到选中…」与右栏 applyRequested 同径（scope=Selected）
    connect(m_photoList, &PhotoListPanel::applyToSelectedRequested, this,
            [this] { runBatchApply(ApplyScope::Selected); });

    // —— 全局快捷键（§7.2：撤销/全选/列表 Del）——
    auto *undoShortcut = new QShortcut(QKeySequence::Undo, this);
    connect(undoShortcut, &QShortcut::activated, this, [this] {
        m_project->undo();
        refreshWatermarkPanel();
    });
    auto *selectAllShortcut = new QShortcut(QKeySequence::SelectAll, this);
    connect(selectAllShortcut, &QShortcut::activated, m_photoList,
            &PhotoListPanel::selectAll);

    // —— 画布交互接线 ——
    connect(m_canvas, &PreviewCanvas::importRequested, this, [this] {
        // 空态点击导入（文案承诺「点击选择」）
        const auto files = QFileDialog::getOpenFileNames(
            this, tr("选择照片"), m_lastDir,
            tr("照片文件 (%1)").arg(QStringLiteral("*.jpg *.jpeg *.png *.webp *.tif *.tiff")));
        if (!files.isEmpty()) {
            m_lastDir = QFileInfo(files.first()).absolutePath();
            importPaths(files);
        }
    });

    // —— 画布交互接线 ——
    connect(m_canvas, &PreviewCanvas::compareOriginalPressed, m_project,
            &ProjectController::setWatermarkHidden);
    connect(m_canvas, &PreviewCanvas::elementSelected, m_watermarkPanel,
            &WatermarkPanel::setSelected);
    connect(m_canvas, &PreviewCanvas::elementMoved, m_project,
            &ProjectController::nudgeElement);
    connect(m_canvas, &PreviewCanvas::elementTextEdited, this,
            [this](const QUuid &id, const QString &text) {
                // 自定义文本改 literal；绑定字段元素写覆盖层
                for (const Element &el :
                     m_project->current()->templateState.elements) {
                    if (el.id != id)
                        continue;
                    if (el.fieldKey.isEmpty())
                        m_project->setElementLiteral(id, text);
                    else
                        m_project->setOverride(el.fieldKey, text);
                    return;
                }
            });
    connect(m_canvas, &PreviewCanvas::elementDeleteRequested, this,
            [this](const QUuid &id) {
                // 自定义文本真删；模板元素隐藏（可从右栏勾选恢复）
                for (const Element &el :
                     m_project->current()->templateState.elements) {
                    if (el.id != id)
                        continue;
                    if (el.kind == ElementKind::Text && el.fieldKey.isEmpty())
                        m_project->removeElement(id);
                    else
                        m_project->setElementVisible(id, false);
                    return;
                }
            });
    connect(m_canvas, &PreviewCanvas::elementContextMenuRequested, this,
            &MainWindow::showElementContextMenu);
    connect(m_canvas, &PreviewCanvas::showWatermarkRequested, m_project,
            &ProjectController::toggleWatermarkHidden);

    // —— 水印内容面板接线 ——
    connect(m_watermarkPanel, &WatermarkPanel::elementVisibleToggled,
            m_project, &ProjectController::setElementVisible);
    connect(m_watermarkPanel, &WatermarkPanel::elementContentEdited, this,
            [this](const QUuid &id, const QString &text) {
                for (const Element &el :
                     m_project->current()->templateState.elements) {
                    if (el.id != id)
                        continue;
                    // 空文本 = 清除覆盖恢复原值；自定义文本走 literal
                    if (el.fieldKey.isEmpty())
                        m_project->setElementLiteral(id, text);
                    else if (text.isEmpty())
                        m_project->clearFieldOverride(el.fieldKey);
                    else
                        m_project->setOverride(el.fieldKey, text);
                    return;
                }
            });
    connect(m_watermarkPanel, &WatermarkPanel::elementSelected, m_canvas,
            &PreviewCanvas::setSelectedElement);
    connect(m_watermarkPanel, &WatermarkPanel::elementStyleChanged, m_project,
            &ProjectController::setElementStyle);
    connect(m_watermarkPanel, &WatermarkPanel::elementResetPositionRequested,
            m_project, &ProjectController::resetElementPosition);
    connect(m_watermarkPanel, &WatermarkPanel::addCustomTextRequested, this,
            [this] {
                const QUuid id = m_project->addCustomText();
                if (id.isNull()) {
                    showToast(tr("自定义文本最多 5 条"));
                    return;
                }
                refreshWatermarkPanel();
            });
    connect(m_watermarkPanel, &WatermarkPanel::removeCustomTextRequested,
            this, [this](const QUuid &id) {
                m_project->removeElement(id);
                refreshWatermarkPanel();
            });
    connect(m_watermarkPanel, &WatermarkPanel::applyRequested, this,
            [this](ApplyScope scope) { runBatchApply(scope); });

    updateEmptyState();
    restoreWindowState();
    setWindowTitle("FrameXT");
}

QWidget *MainWindow::buildLeftPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("sidePanel");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(kPanelSpacing, kPanelSpacing, 0, 0);
    layout->setSpacing(kPanelSpacing);

    // 基础属性（3.6 上半）：只读字段表 + 复制
    m_basicInfo = new BasicInfoPanel(panel);
    layout->addWidget(m_basicInfo, 1);

    // 导入导出卡（§3.3；导出执行 M4 接线，先就位表单）
    auto *ioContent = new QWidget;
    ioContent->setObjectName("cardBody");
    auto *ioLayout = new QVBoxLayout(ioContent);
    ioLayout->setContentsMargins(12, 10, 12, 12);
    ioLayout->setSpacing(6);

    auto *importButton = new QPushButton(tr("导入照片…"), ioContent);
    importButton->setObjectName("primaryButton");
    connect(importButton, &QPushButton::clicked, this, [this] {
        const auto files = QFileDialog::getOpenFileNames(
            this, tr("选择照片"), m_lastDir,
            tr("照片文件 (%1)").arg(QStringLiteral("*.jpg *.jpeg *.png *.webp *.tif *.tiff")));
        if (!files.isEmpty()) {
            m_lastDir = QFileInfo(files.first()).absolutePath();
            importPaths(files);
        }
    });
    ioLayout->addWidget(importButton);

    m_importSummary = new QLabel(tr("尚未导入照片"), ioContent);
    m_importSummary->setObjectName("panelCount");
    ioLayout->addWidget(m_importSummary);

    // 导出表单（§3.3 完整五项；M4 起接线 ExportController）
    m_exportPanel = new ExportPanel(ioContent);
    ioLayout->addWidget(m_exportPanel);

    auto *aboutButton = new QPushButton(tr("关于 FrameXT"), ioContent);
    aboutButton->setObjectName("secondaryButton");
    connect(aboutButton, &QPushButton::clicked, this,
            &MainWindow::showAbout);
    ioLayout->addWidget(aboutButton);

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
    layout->setSpacing(0);

    m_canvas = new PreviewCanvas(panel);
    layout->addWidget(m_canvas, 1);

    // 底部工具条（高 36：适配 | 100% | 缩放滑杆 | 对比原图）
    auto *toolbar = new QWidget(panel);
    toolbar->setObjectName("previewToolbar");
    toolbar->setFixedHeight(Theme::kToolstripHeight);
    auto *tb = new QHBoxLayout(toolbar);
    tb->setContentsMargins(12, 4, 12, 4);
    tb->setSpacing(8);

    auto *fitButton = new QPushButton(tr("适配"), toolbar);
    fitButton->setObjectName("secondaryButton");
    connect(fitButton, &QPushButton::clicked, m_canvas,
            &PreviewCanvas::fitToWindow);
    tb->addWidget(fitButton);

    auto *zoom100Button = new QPushButton("100%", toolbar);
    zoom100Button->setObjectName("secondaryButton");
    connect(zoom100Button, &QPushButton::clicked, m_canvas,
            &PreviewCanvas::setZoom100);
    tb->addWidget(zoom100Button);

    auto *zoomSlider = new QSlider(Qt::Horizontal, toolbar);
    zoomSlider->setRange(10, 800);   // 0.1–8.0×
    zoomSlider->setValue(100);
    zoomSlider->setMaximumWidth(140);
    connect(zoomSlider, &QSlider::valueChanged, m_canvas, [this](int v) {
        if (qFuzzyCompare(m_canvas->zoom(), qreal(v) / 100.0))
            return;
        m_canvas->setZoom(qreal(v) / 100.0);
    });
    connect(m_canvas, &PreviewCanvas::zoomChanged, zoomSlider, [zoomSlider](qreal z) {
        QSignalBlocker blocker(zoomSlider);
        zoomSlider->setValue(int(z * 100));
    });
    tb->addWidget(zoomSlider);

    auto *percent = new QLabel("100%", toolbar);
    percent->setObjectName("panelCount");
    connect(m_canvas, &PreviewCanvas::zoomChanged, percent,
            [percent](qreal z) {
                percent->setText(QString::number(int(z * 100)) + QStringLiteral("%"));
            });
    tb->addWidget(percent);

    m_compareButton = new QToolButton(toolbar);
    m_compareButton->setText(tr("对比原图"));
    m_compareButton->setObjectName("secondaryButton");
    m_compareButton->setToolTip(tr("按住临时隐藏边框水印查看原片"));
    m_compareButton->setCheckable(false);
    // 按住临时隐藏水印（§9-D）；画布内左键按下走同一控制器状态
    connect(m_compareButton, &QToolButton::pressed, m_project,
            [this] { m_project->setWatermarkHidden(true); });
    connect(m_compareButton, &QToolButton::released, m_project,
            [this] { m_project->setWatermarkHidden(false); });
    tb->addWidget(m_compareButton);
    tb->addStretch();
    m_canvasHint = new QLabel(tr("空格拖拽平移 · 滚轮缩放"), toolbar);
    m_canvasHint->setObjectName("panelCount");
    tb->addWidget(m_canvasHint);
    layout->addWidget(toolbar);

    return panel;
}

QWidget *MainWindow::buildRightPanel()
{
    auto *panel = new QWidget;
    panel->setObjectName("sidePanel");
    auto *layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, kPanelSpacing, kPanelSpacing, 0);
    layout->setSpacing(kPanelSpacing);

    // 模板卡（§3.5 面板一；参数滑杆/模板管理器 M5 接入）
    auto *templateContent = new QWidget;
    templateContent->setObjectName("cardBody");
    auto *templateLayout = new QVBoxLayout(templateContent);
    templateLayout->setContentsMargins(12, 10, 12, 12);
    templateLayout->setSpacing(8);
    m_templateCombo = new QComboBox(templateContent);
    m_templateCombo->addItem(tr("底部边框"), QStringLiteral("bottom"));
    m_templateCombo->addItem(tr("环绕边框"), QStringLiteral("around"));
    m_templateCombo->addItem(tr("高斯模糊"), QStringLiteral("blur"));
    connect(m_templateCombo, &QComboBox::activated, this, [this](int index) {
        const QString id = m_templateCombo->itemData(index).toString();
        if (!id.isEmpty())
            m_project->setTemplateId(id);
    });
    templateLayout->addWidget(m_templateCombo);
    templateLayout->addStretch();
    templateContent->setEnabled(false);
    layout->addWidget(createCard(tr("模板"), QString(), templateContent));

    // 水印内容面板（3.5 第二批：开关行 + 行内编辑 + 样式区 + 应用范围；
    // 面板自带标题行，不用 createCard 重复包标题）
    m_watermarkPanel = new WatermarkPanel(panel);
    layout->addWidget(m_watermarkPanel, 1);

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
    m_photoList = new PhotoListPanel(listArea);
    listLayout->addWidget(m_photoList);

    layout->addWidget(createCard(tr("照片列表"), QString(), listArea));
    return panel;
}

void MainWindow::importPaths(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    if (m_import->busy()) {
        showToast(tr("正在导入，请稍后再试"));
        return;
    }
    m_import->importFiles(paths);
}

void MainWindow::refreshBasicInfo()
{
    const PhotoDocument *doc = m_project->current();
    if (!doc) {
        m_basicInfo->clearFields();
        m_importSummary->setText(tr("尚未导入照片"));
        return;
    }
    // 覆盖层优先（与渲染 resolver 同语义）
    QHash<QString, QString> fields = doc->meta.exif.fields;
    for (auto it = doc->overrides.values.cbegin();
         it != doc->overrides.values.cend(); ++it)
        fields.insert(it.key(), it.value());
    m_basicInfo->setFields(fields, true);
    m_importSummary->setText(tr("已选 %1 张").arg(m_project->documents().size()));
}

void MainWindow::syncTemplateCombo()
{
    // 回同步：切照片/撤销后下拉必须反映当前文档的模板
    // （activated 只在用户改选时触发，同项点击不回发——回设不改状态）
    const PhotoDocument *doc = m_project->current();
    const QString id =
        doc ? doc->templateState.templateId : QStringLiteral("bottom");
    for (int i = 0; i < m_templateCombo->count(); ++i) {
        if (m_templateCombo->itemData(i).toString() == id) {
            QSignalBlocker blocker(m_templateCombo);
            m_templateCombo->setCurrentIndex(i);
            return;
        }
    }
}

void MainWindow::runBatchApply(ApplyScope scope)
{
    // 确认框数量 = 实际作用面（跳过源照片后的净数量，如实告知）
    const int targetCount = m_project->applyTargetCount(scope);
    if (targetCount <= 0)
        return;
    QMessageBox box(this);
    box.setWindowTitle(tr("批量应用"));
    box.setText(tr("将把当前照片的模板与水印设置应用到 %1 张照片？")
                    .arg(targetCount));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.button(QMessageBox::Yes)->setText(tr("应用"));
    auto *keepPos = new QCheckBox(tr("保留已手动调整的位置"), &box);
    keepPos->setChecked(true);
    box.setCheckBox(keepPos);
    if (box.exec() != QMessageBox::Yes)
        return;
    const int applied =
        m_project->applyCurrentTo(scope, keepPos->isChecked());
    refreshWatermarkPanel();
    showToast(applied > 0 ? tr("已应用到 %1 张照片 · 可撤销").arg(applied)
                          : tr("没有需要应用的照片"));
}

void MainWindow::refreshWatermarkPanel()
{
    const PhotoDocument *doc = m_project->current();
    if (!doc) {
        m_watermarkPanel->setElements({}, {}, 0);
        return;
    }
    // 行内编辑框当前值 = 渲染同源显示值（覆盖层已合并）
    const auto resolver = m_project->fieldResolver(*doc);
    QHash<QUuid, QString> values;
    for (const Element &el : doc->templateState.elements) {
        const QString v = el.fieldKey.isEmpty() ? el.literal
                                                : resolver(el.fieldKey);
        values.insert(el.id, v);
    }
    m_watermarkPanel->setElements(doc->templateState.elements, values,
                                  m_project->documents().size());
}

void MainWindow::showElementContextMenu(const QUuid &elementId,
                                        const QPoint &globalPos)
{
    if (elementId.isNull())
        return;
    QMenu menu(this);
    QAction *raiseAction =
        menu.addAction(tr("置顶"), this, [this, elementId] {
            m_project->raiseElement(elementId);
        });
    QAction *resetAction =
        menu.addAction(tr("还原位置"), this, [this, elementId] {
            m_project->resetElementPosition(elementId);
        });
    bool isCustom = false;
    bool atCustom = false;
    for (const Element &el : m_project->current()->templateState.elements) {
        if (el.id == elementId) {
            isCustom = el.kind == ElementKind::Text && el.fieldKey.isEmpty();
            atCustom = el.hasCustomPosition;
            break;
        }
    }
    resetAction->setEnabled(atCustom);
    menu.addSeparator();
    if (isCustom) {
        menu.addAction(tr("删除"), this, [this, elementId] {
            m_project->removeElement(elementId);
            refreshWatermarkPanel();
        });
    } else {
        menu.addAction(tr("隐藏"), this, [this, elementId] {
            m_project->setElementVisible(elementId, false);
        });
    }
    menu.exec(globalPos);
}

void MainWindow::showToast(const QString &text)
{
    if (!m_toast) {
        m_toast = new QLabel(this);
        m_toast->setObjectName("toast");
        m_toastTimer = new QTimer(this);
        m_toastTimer->setSingleShot(true);
        connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);
    }
    m_toast->setText(text);
    m_toast->adjustSize();
    m_toast->move((width() - m_toast->width()) / 2, height() - 64);
    m_toast->raise();
    m_toast->show();
    m_toastTimer->start(3000);
}

void MainWindow::restoreWindowState()
{
    // 3.10 持久化：窗口几何/分割器/最近目录（QSettings 由 main.cpp 注册
    // org/app 名；无保存值时走屏幕收敛的默认布局）
    QSettings settings;
    const QByteArray geo =
        settings.value(QStringLiteral("window/geometry")).toByteArray();
    if (!geo.isEmpty())
        restoreGeometry(geo);

    m_lastDir = settings.value(QStringLiteral("import/lastDir"))
                    .toString();

    const QByteArray mainSizes = settings
                                     .value(QStringLiteral("window/mainSplitter"))
                                     .toByteArray();
    if (!mainSizes.isEmpty() && m_mainSplitter)
        m_mainSplitter->restoreState(mainSizes);
    const QByteArray rootSizes = settings
                                     .value(QStringLiteral("window/rootSplitter"))
                                     .toByteArray();
    if (!rootSizes.isEmpty() && m_rootSplitter)
        m_rootSplitter->restoreState(rootSizes);

    restoreExportPrefs();   // M4：导出预设（§4.8）
}

void MainWindow::saveWindowState()
{
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/mainSplitter"),
                      m_mainSplitter ? m_mainSplitter->saveState()
                                     : QByteArray());
    settings.setValue(QStringLiteral("window/rootSplitter"),
                      m_rootSplitter ? m_rootSplitter->saveState()
                                     : QByteArray());
    settings.setValue(QStringLiteral("import/lastDir"), m_lastDir);
}

void MainWindow::showAbout()
{
    // §4 统一对话框规格：模态、圆角 5、顶栏 40 + 底部按钮区
    QDialog about(this);
    about.setWindowTitle(tr("关于 FrameXT"));
    about.setModal(true);
    about.setFixedWidth(420);
    auto *layout = new QVBoxLayout(&about);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(8);

    auto *title = new QLabel(
        QStringLiteral("FrameXT %1").arg(fxt::AppInfo::versionText()), &about);
    title->setObjectName("panelTitle");
    layout->addWidget(title);
    auto *desc = new QLabel(
        tr("本地照片边框水印工具 — 照片不会离开这台电脑。"), &about);
    desc->setWordWrap(true);
    layout->addWidget(desc);
    auto *home = new QLabel(
        QStringLiteral("<a href=\"%1\">%1</a>")
            .arg(QString::fromLatin1(fxt::AppInfo::kHomePage)), &about);
    home->setOpenExternalLinks(true);
    layout->addWidget(home);
    auto *license = new QLabel(
        tr("本软件使用了 Qt、exiv2、libpng、libjpeg、zlib 等开源组件，\n"
           "许可声明见安装目录 licenses/。"), &about);
    license->setWordWrap(true);
    license->setStyleSheet(QStringLiteral("color: %1")
                               .arg(Theme::kTextSecondary.name()));
    layout->addWidget(license);

    layout->addSpacing(8);
    auto *buttonRow = new QHBoxLayout();
    buttonRow->addStretch();
    auto *closeButton = new QPushButton(tr("关闭"), &about);
    closeButton->setObjectName("primaryButton");
    connect(closeButton, &QPushButton::clicked, &about, &QDialog::accept);
    buttonRow->addWidget(closeButton);
    layout->addLayout(buttonRow);

    about.exec();
}

// —— 导出（M4，§3.3/§5.2/§5.3）——

namespace {
// §5.3「每个文件询问」：逐文件三选（默认自动重命名）。
// 自由函数而非成员——MainWindow.h 仅前向声明 ExportController，
// 其嵌套枚举在头文件不可用。
ExportController::ConflictChoice askConflictDialog(QWidget *parent,
                                                   const QString &fileName)
{
    QMessageBox box(parent);
    box.setWindowTitle(QObject::tr("文件名冲突"));
    box.setText(QObject::tr("目标目录已有同名文件：\n%1").arg(fileName));
    auto *rename = box.addButton(QObject::tr("自动重命名"),
                                 QMessageBox::YesRole);
    auto *overwrite = box.addButton(QObject::tr("覆盖"),
                                    QMessageBox::DestructiveRole);
    auto *skip = box.addButton(QObject::tr("跳过"), QMessageBox::NoRole);
    Q_UNUSED(skip);
    box.setDefaultButton(rename);
    box.exec();
    if (box.clickedButton() == overwrite)
        return ExportController::ConflictChoice::Overwrite;
    if (box.clickedButton() == skip)
        return ExportController::ConflictChoice::Skip;
    return ExportController::ConflictChoice::Rename;
}
} // namespace

void MainWindow::startExport(const ExportOptions &options)
{
    if (m_export->busy())
        return;
    if (m_import->busy()) {
        // 技术设计 §4.7：导入中锁导出（快照不完整会造成统计失实）
        showToast(tr("正在导入照片，请等待完成后再导出"));
        return;
    }
    if (options.outputDir.isEmpty()) {
        showToast(tr("请先选择导出目录"));
        return;
    }
    if (!QDir(options.outputDir).exists()) {
        // 预扫失败不该静默（评审交叉项：目录失效无反馈）
        showToast(tr("导出目录不存在：%1").arg(options.outputDir));
        return;
    }

    // 快照全部可导项（导出不依赖预览历史，§4.2：模板+meta 即时解算）
    QList<PhotoDocument> docs;
    QList<FieldValueResolver> resolvers;
    QList<QImage> logos;
    for (const PhotoDocument &doc : m_project->documents()) {
        if (doc.meta.pixelSize.isEmpty())
            continue;   // 解码失败的导入残件不可导
        docs.append(doc);
        resolvers.append(m_project->fieldResolver(doc));
        logos.append(m_project->resolveLogo(doc));
    }
    if (docs.isEmpty()) {
        showToast(tr("没有可导出的照片"));
        return;
    }

    // 两段式：preplan 预分配（AskEach 在此逐个询问）→ Overwrite 破坏性
    // 确认（§7.3：覆盖类操作必经确认；评审 P0）→ 派发
    ExportController::PreplanResult plan = m_export->preplan(
        docs, resolvers, logos, options, [this](const QString &name) {
            return askConflictDialog(this, name);
        });
    if (!plan.error.isEmpty()) {
        showToast(plan.error);
        return;
    }
    if (plan.jobs.isEmpty() && plan.skipped.isEmpty()) {
        showToast(tr("没有可导出的照片"));
        return;
    }
    if (!plan.overwriteHits.isEmpty()) {
        // §5.3 汇总确认（列出将覆盖的文件 + 红色警示）
        QStringList shown = plan.overwriteHits.mid(0, 8);
        if (plan.overwriteHits.size() > 8)
            shown += QStringLiteral("…等共 %1 个")
                         .arg(plan.overwriteHits.size());
        const auto answer = QMessageBox::warning(
            this, tr("文件名冲突"),
            tr("目标目录已有 %1 个同名文件，将被覆盖：\n%2\n\n覆盖后无法恢复，"
               "确定继续吗？")
                .arg(plan.overwriteHits.size())
                .arg(shown.join(QStringLiteral("\n"))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes)
            return;
    }

    saveExportPrefs(options);   // 导出预设持久化（§4.8）
    m_exportDone = 0;
    m_exportPanel->setExporting(true);
    m_export->runPrepared(std::move(plan));
}

void MainWindow::showExportFinished(
    const QList<ExportEngine::ItemResult> &results, bool canceled,
    qint64 elapsedMs)
{
    m_exportPanel->setExporting(false);
    if (results.isEmpty())
        return;

    int ok = 0, skipped = 0, failed = 0;
    qint64 srcBytes = 0, outBytes = 0;
    QString outputDir;
    QList<ExportEngine::ItemResult> failedItems;
    for (const auto &r : results) {
        if (r.skipped)
            ++skipped;
        else if (r.ok) {
            ++ok;
            srcBytes += r.sourceBytes;
            outBytes += r.outputBytes;
            outputDir = QFileInfo(r.outputPath).absolutePath();
        } else {
            ++failed;
            failedItems.append(r);
        }
    }

    if (canceled) {
        showToast(tr("已取消导出，已导出 %1 张保留").arg(ok));
        return;
    }

    // §5.2 完成对话框：✓ + 统计 + 体积对比 + 打开文件夹
    QDialog done(this);
    done.setWindowTitle(tr("导出完成"));
    done.setModal(true);
    done.setFixedWidth(420);
    auto *layout = new QVBoxLayout(&done);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(8);

    auto *head = new QLabel(ok > 0 ? tr("✓ 导出完成") : tr("导出失败"),
                            &done);
    head->setObjectName("panelTitle");
    head->setStyleSheet(QStringLiteral("color: %1")
                            .arg(ok > 0 ? QStringLiteral("#2E9E5B")
                                        : QStringLiteral("#D9534F")));
    layout->addWidget(head);

    QString stats = tr("成功 %1 · 跳过 %2 · 失败 %3")
                        .arg(ok)
                        .arg(skipped)
                        .arg(failed);
    if (ok > 0) {
        // 预设注记（§5.2 示例含「视觉无损」语义标注）
        QString presetNote;
        switch (m_exportPanel->currentOptions().preset) {
        case QualityPreset::VisualLossless: presetNote = tr("视觉无损"); break;
        case QualityPreset::Balanced: presetNote = tr("平衡"); break;
        case QualityPreset::Custom: presetNote = tr("自定义"); break;
        }
        stats += QLatin1Char('\n')
                 + tr("体积：%1 → %2 MB（%3）")
                       .arg(srcBytes / 1048576.0, 0, 'f', 1)
                       .arg(outBytes / 1048576.0, 0, 'f', 1)
                       .arg(presetNote)
                 + tr("  ·  %1s")
                       .arg(elapsedMs / 1000.0, 0, 'f', 1);
    }
    auto *statLabel = new QLabel(stats, &done);
    statLabel->setWordWrap(true);
    layout->addWidget(statLabel);

    layout->addSpacing(8);
    auto *buttonRow = new QHBoxLayout();
    if (ok > 0) {
        auto *openFolder = new QPushButton(tr("打开所在文件夹"), &done);
        openFolder->setObjectName("secondaryButton");
        connect(openFolder, &QPushButton::clicked, &done, [outputDir] {
            QDesktopServices::openUrl(QUrl::fromLocalFile(outputDir));
        });
        buttonRow->addWidget(openFolder);
    }
    buttonRow->addStretch();
    if (failed > 0) {
        // §5.4 失败清单（R5 自助排查路径）
        auto *detail = new QPushButton(tr("失败详情…"), &done);
        detail->setObjectName("secondaryButton");
        connect(detail, &QPushButton::clicked, &done, [this, failedItems] {
            showExportFailures(failedItems);
        });
        buttonRow->addWidget(detail);
    }
    auto *okButton = new QPushButton(tr("确定"), &done);
    okButton->setObjectName("primaryButton");
    connect(okButton, &QPushButton::clicked, &done, &QDialog::accept);
    buttonRow->addWidget(okButton);
    layout->addLayout(buttonRow);

    done.exec();
}

void MainWindow::showExportFailures(
    const QList<ExportEngine::ItemResult> &failedItems)
{
    // §5.4 失败汇总对话框：表格（文件/阶段/原因）+ 重试 + 诊断包
    QDialog dialog(this);
    dialog.setWindowTitle(tr("导出失败清单"));
    dialog.setModal(true);
    dialog.setFixedWidth(560);
    dialog.setFixedHeight(360);
    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 12);
    layout->setSpacing(8);

    auto *table = new QTableWidget(failedItems.size(), 3, &dialog);
    table->setHorizontalHeaderLabels({ tr("文件"), tr("阶段"), tr("原因") });
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setStretchLastSection(true);
    for (int i = 0; i < failedItems.size(); ++i) {
        const auto &r = failedItems.at(i);
        table->setItem(i, 0, new QTableWidgetItem(
                                QFileInfo(r.sourcePath).fileName()));
        table->setItem(i, 1, new QTableWidgetItem(r.stage));
        table->setItem(i, 2, new QTableWidgetItem(r.error));
    }
    table->setColumnWidth(0, 200);
    table->setColumnWidth(1, 60);
    layout->addWidget(table, 1);

    auto *buttonRow = new QHBoxLayout();
    auto *retry = new QPushButton(tr("重试失败项"), &dialog);
    retry->setObjectName("primaryButton");
    connect(retry, &QPushButton::clicked, &dialog, [this, &dialog, failedItems] {
        // 按 sourcePath 找回文档快照重新导出（用户可能已改选项——沿用
        // 面板当前值；冲突策略沿用，重试项的输出名重新 preplan）
        QList<PhotoDocument> docs;
        QList<FieldValueResolver> resolvers;
        QList<QImage> logos;
        for (const auto &r : failedItems) {
            for (const PhotoDocument &doc : m_project->documents()) {
                if (doc.sourcePath != r.sourcePath
                    || doc.meta.pixelSize.isEmpty())
                    continue;
                docs.append(doc);
                resolvers.append(m_project->fieldResolver(doc));
                logos.append(m_project->resolveLogo(doc));
                break;
            }
        }
        if (docs.isEmpty())
            return;
        dialog.accept();
        m_exportDone = 0;
        m_exportPanel->setExporting(true);
        const ExportOptions options = m_exportPanel->currentOptions();
        m_export->start(docs, resolvers, logos, options,
                        [this](const QString &name) {
                            return askConflictDialog(this, name);
                        });
    });
    buttonRow->addWidget(retry);

    auto *diag = new QPushButton(tr("导出诊断包"), &dialog);
    diag->setObjectName("secondaryButton");
    connect(diag, &QPushButton::clicked, &dialog,
            [this, &dialog, failedItems] {
                saveDiagnostics(failedItems);
            });
    buttonRow->addWidget(diag);
    buttonRow->addStretch();

    auto *close = new QPushButton(tr("关闭"), &dialog);
    connect(close, &QPushButton::clicked, &dialog, &QDialog::reject);
    buttonRow->addWidget(close);
    layout->addLayout(buttonRow);

    dialog.exec();
}

void MainWindow::saveDiagnostics(
    const QList<ExportEngine::ItemResult> &failedItems)
{
    // 诊断包 = zip（失败文件副本 + 错误日志 + 版本信息），R5 场景：
    // 用户自助打包发 issue。zip 用系统自带 tar.exe（Windows 10 1803+
    // bsdtar；System32 绝对路径防 PATH 上 GNU tar 冒充——GNU tar 不认
    // zip 后缀直接失败，评审 P1-5）。
    QString target = QFileDialog::getSaveFileName(
        this, tr("保存诊断包"),
        QStringLiteral("FrameXT-诊断-%1.zip")
            .arg(QDate::currentDate().toString(QStringLiteral("yyyyMMdd"))),
        tr("诊断包 (*.zip)"));
    if (target.isEmpty())
        return;
    if (!target.endsWith(QStringLiteral(".zip"), Qt::CaseInsensitive))
        target += QStringLiteral(".zip");   // tar -a 按后缀定格式

    QTemporaryDir *staging = new QTemporaryDir;
    if (!staging->isValid()) {
        delete staging;
        showToast(tr("创建临时目录失败"));
        return;
    }
    const QDir root(staging->path());

    // 1) 失败文件副本（≤20MB/个：超大源截断拷贝，日志注明）
    const QDir filesDir = root.mkpath(QStringLiteral("files"))
                              ? QDir(root.filePath(QStringLiteral("files")))
                              : QDir();
    qint64 copied = 0;
    QSet<QString> seen;
    QStringList truncated;
    for (const auto &r : failedItems) {
        if (seen.contains(r.sourcePath))
            continue;
        seen.insert(r.sourcePath);
        QFile src(r.sourcePath);
        if (!src.open(QIODevice::ReadOnly))
            continue;
        constexpr qint64 kMaxPerFile = 20LL * 1024 * 1024;
        const qint64 size = src.size();
        const QByteArray bytes = src.read(kMaxPerFile);
        src.close();
        if (size > kMaxPerFile)
            truncated += QStringLiteral("%1（%2MB，仅收录前 20MB）")
                             .arg(QFileInfo(r.sourcePath).fileName())
                             .arg(size / 1048576);
        const QString name = QStringLiteral("%1_%2")
                                 .arg(copied)
                                 .arg(QFileInfo(r.sourcePath).fileName());
        QFile dst(filesDir.filePath(name));
        if (dst.open(QIODevice::WriteOnly)) {
            dst.write(bytes);
            dst.close();
            ++copied;
        }
    }

    // 2) 错误日志 + 版本信息 + 截断清单
    QFile log(root.filePath(QStringLiteral("errors.txt")));
    if (log.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&log);
        ts.setEncoding(QStringConverter::Utf8);
        ts << "FrameXT " << fxt::AppInfo::versionText() << "\n";
        ts << QLocale().system().name() << " / "
           << QSysInfo::prettyProductName() << "\n\n";
        if (!truncated.isEmpty())
            ts << "[超大文件截断收录]\n"
               << truncated.join(QStringLiteral("\n")) << "\n\n";
        for (const auto &r : failedItems) {
            ts << r.sourcePath << "\n  stage=" << r.stage
               << "\n  error=" << r.error << "\n\n";
        }
    }

    // 3) tar 异步打包（评审 P1-4：同步 waitForFinished 冻结 UI 最长 63s；
    // staging 堆分配随信号归档释放，lambda 持指针）
    QProcess *tar = new QProcess(this);
    tar->start(QStringLiteral("C:/Windows/System32/tar.exe"),
               { QStringLiteral("-a"), QStringLiteral("-c"),
                 QStringLiteral("-f"), target,
                 QStringLiteral("-C"), staging->path(),
                 QStringLiteral("files"), QStringLiteral("errors.txt") });
    connect(tar, &QProcess::finished, this,
            [this, tar, staging, target, copied](int code,
                                                 QProcess::ExitStatus) {
                tar->deleteLater();
                delete staging;
                if (code != 0) {
                    showToast(tr("诊断包生成失败（tar 退出码 %1）").arg(code));
                    return;
                }
                showToast(tr("诊断包已保存：%1（含 %2 个失败文件副本）")
                              .arg(QFileInfo(target).fileName())
                              .arg(copied));
            });
    connect(tar, &QProcess::errorOccurred, this, [this, staging, tar] {
        tar->deleteLater();
        delete staging;
        showToast(tr("诊断包生成失败（本机 tar.exe 不可用，"
                     "需 Windows 10 1803+）"));
    });
}

void MainWindow::restoreExportPrefs()
{
    QSettings settings;
    ExportOptions opt;
    opt.outputDir = settings.value(
        QStringLiteral("export/outputDir")).toString();
    opt.naming = NamingMode(settings
                                .value(QStringLiteral("export/naming"),
                                       int(opt.naming))
                                .toInt());
    opt.conflict = ConflictPolicy(settings
                                      .value(QStringLiteral("export/conflict"),
                                             int(opt.conflict))
                                      .toInt());
    opt.preset = QualityPreset(settings
                                   .value(QStringLiteral("export/quality"),
                                          int(opt.preset))
                                   .toInt());
    opt.sizeMode = SizeMode(settings
                                .value(QStringLiteral("export/size"),
                                       int(opt.sizeMode))
                                .toInt());
    opt.customQuality = settings.value(QStringLiteral("export/customQuality"),
                                       opt.customQuality)
                            .toInt();
    opt.customSubsampling =
        JpegSubsampling(settings
                            .value(QStringLiteral("export/customSubsampling"),
                                   int(opt.customSubsampling))
                            .toInt());
    m_exportPanel->setOptions(opt);
}

void MainWindow::saveExportPrefs(const ExportOptions &options)
{
    QSettings settings;
    settings.setValue(QStringLiteral("export/outputDir"), options.outputDir);
    settings.setValue(QStringLiteral("export/naming"), int(options.naming));
    settings.setValue(QStringLiteral("export/conflict"),
                      int(options.conflict));
    settings.setValue(QStringLiteral("export/quality"), int(options.preset));
    settings.setValue(QStringLiteral("export/size"), int(options.sizeMode));
    settings.setValue(QStringLiteral("export/customQuality"),
                      options.customQuality);
    settings.setValue(QStringLiteral("export/customSubsampling"),
                      int(options.customSubsampling));
}

void MainWindow::updateEmptyState()
{
    const bool empty = m_project->documents().isEmpty();
    m_canvas->setEmpty(empty);
    // 空态禁用只能作用于控件，不能作用于布局（M0 复现：布局被禁用后
    // 停止排列子项，提示文字停留左上角）
    m_templateCombo->parentWidget()->setEnabled(!empty);
    if (m_watermarkPanel)
        m_watermarkPanel->setEnabled(!empty);
    if (m_exportPanel)
        m_exportPanel->setHasPhotos(!empty);
    m_importSummary->setText(empty
                                 ? tr("尚未导入照片")
                                 : tr("已选 %1 张")
                                       .arg(m_project->documents().size()));
}

void MainWindow::confirmClear()
{
    if (m_project->documents().isEmpty())
        return;
    const auto answer = QMessageBox::question(
        this, tr("清空列表"),
        tr("将移除全部 %1 张照片（不删除文件）。继续？")
            .arg(m_project->documents().size()));
    if (answer != QMessageBox::Yes)
        return;
    m_photoList->clearPhotos();
    m_project->clearDocuments();
    m_basicInfo->clearFields();
    updateEmptyState();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
        return;
    }
    event->ignore();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
        return;
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    if (!paths.isEmpty())
        importPaths(paths);
    event->acceptProposedAction();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 导出进行中关窗（评审 P0-1 实锤 UAF：worker 持 controller 引用，
    // 直接析构 = 崩溃）：先单独确认，同意则 cancel + 析构内汇合等待
    if (m_export && m_export->busy()) {
        const auto answer = QMessageBox::question(
            this, tr("正在导出"),
            tr("导出进行中（已完成 %1 张）。退出将取消剩余任务，"
               "已导出的文件保留。确定退出吗？").arg(m_exportDone),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_export->cancel();
        // 不在此处等待：析构链 ExportController::~ExportController 会
        // waitForFinished 汇合（worker 引用存活到析构体内）
    } else if (!m_project->documents().isEmpty() && !m_exitConfirmed) {
        // §5.6 退出确认：有照片且未导出时挽留（照片工具最高频数据丢失场景）
        const auto answer = QMessageBox::question(
            this, tr("退出 FrameXT"),
            tr("还有 %1 张照片未导出。确定退出吗？")
                .arg(m_project->documents().size()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (answer != QMessageBox::Yes) {
            event->ignore();
            return;
        }
    }
    saveWindowState();
    QMainWindow::closeEvent(event);
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
        // 仅回收本窗口控件的、我们设置过的光标（控件自带特殊光标如分割条
        // 手柄不动）；弹出菜单等外来控件不归我们管
        auto *widget = qobject_cast<QWidget *>(watched);
        if (widget && widget->window() == this) {
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
            // 命中边缘 8px 带 → 进入原生尺寸调整循环并吞掉该次按下；
            // 系统循环吞掉后续 release，override 光标需立即清（否则残留）
            if (windowHandle() && windowHandle()->startSystemResize(edges)) {
                clearResizeCursor();
                return true;
            }
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

} // namespace fxt
