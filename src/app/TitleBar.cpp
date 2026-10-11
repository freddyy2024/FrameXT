#include "TitleBar.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QToolButton>
#include <QWindow>

namespace {

// 04-UI设计.md §1.3：标题栏高 40；三按钮 = Windows 标题按钮规格（46 宽通栏）
constexpr int kBarHeight = 40;
constexpr int kCaptionButtonWidth = 46;
constexpr int kLogoSize = 22;

// Segoe 图标字形：最小化 / 最大化 / 还原 / 关闭 / 眼睛（与系统标题按钮同源字形）
constexpr char16_t kGlyphMinimize = 0xE921;
constexpr char16_t kGlyphMaximize = 0xE922;
constexpr char16_t kGlyphRestore  = 0xE923;
constexpr char16_t kGlyphClose    = 0xE8BB;
constexpr char16_t kGlyphEye      = 0xE7B3;   // RedEye/View 图标

// 语言菜单项（3.9）：按钮与菜单统一用语言自称（endonym），不参与翻译——
// 否则非中文环境下按钮永远显示「中文」/「Chinese」，无法辨认当前语言
const QList<QPair<QString, QString>> kLanguages = {
    { QStringLiteral("简体中文"), QStringLiteral("zh-CN") },
    { QStringLiteral("繁體中文"), QStringLiteral("zh-TW") },
    { QStringLiteral("English"), QStringLiteral("en") },
};

QFont captionGlyphFont()
{
    QFont font;
    font.setFamilies({ QStringLiteral("Segoe Fluent Icons"),      // Win11
                       QStringLiteral("Segoe MDL2 Assets") });    // Win10 回退
    font.setPointSize(9);
    return font;
}

} // namespace

namespace fxt {

TitleBar::TitleBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("titleBar"));
    setFixedHeight(kBarHeight);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 0, 0, 0);
    layout->setSpacing(8);

    m_logoLabel = new QLabel(this);
    m_logoLabel->setFixedSize(kLogoSize, kLogoSize);
    m_logoLabel->setPixmap(logoPixmap());
    layout->addWidget(m_logoLabel);

    m_titleLabel = new QLabel(QStringLiteral("FrameXT"), this);   // 产品名固定，不参与翻译
    QFont titleFont = m_titleLabel->font();
    titleFont.setPixelSize(17);   // 视觉稿 13pt Bold ≈ 17px
    titleFont.setBold(true);
    m_titleLabel->setFont(titleFont);
    layout->addWidget(m_titleLabel);

    layout->addStretch();

    // 关于入口（用户指令：从导入导出面板移至标题栏，ⓘ 与标题按钮同规格）
    auto *aboutButton = createCaptionButton(QStringLiteral("captionAbout"),
                                            QString(QChar(0xE946)),   // Info
                                            tr("关于 FrameXT"));
    connect(aboutButton, &QToolButton::clicked, this,
            &TitleBar::aboutRequested);
    layout->addWidget(aboutButton);

    // 语言切换（3.9）：选择后写持久化 key、重启生效；按钮文本由
    // updateLanguageButton 按已保存的语言回填（构造默认简体中文）
    m_languageButton = new QToolButton(this);
    m_languageButton->setObjectName(QStringLiteral("languageButton"));
    m_languageButton->setToolTip(tr("切换界面语言"));
    m_languageButton->setCursor(Qt::PointingHandCursor);
    m_languageButton->setFocusPolicy(Qt::NoFocus);
    updateLanguageButton(QStringLiteral("zh-CN"));

    auto *languageMenu = new QMenu(m_languageButton);
    for (const auto &[name, code] : kLanguages) {
        QAction *action = languageMenu->addAction(name);
        action->setData(code);
    }
    connect(languageMenu, &QMenu::triggered, this,
            [this](QAction *action) {
                emit languageSelected(action->data().toString());
            });
    m_languageButton->setMenu(languageMenu);
    m_languageButton->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(m_languageButton);

    // 纯看图开关（👁，§3.4 第三态入口）：点击切换水印显隐
    m_eyeButton = createCaptionButton(QStringLiteral("captionEye"),
                                      QString(QChar(kGlyphEye)),
                                      tr("纯看图（隐藏水印）"));
    m_eyeButton->setCheckable(true);
    connect(m_eyeButton, &QToolButton::toggled, this,
            &TitleBar::watermarkEyeToggled);
    layout->addWidget(m_eyeButton);

    m_minButton = createCaptionButton(QStringLiteral("captionMin"),
                                      QString(QChar(kGlyphMinimize)), tr("最小化"));
    m_maxButton = createCaptionButton(QStringLiteral("captionMax"),
                                      QString(QChar(kGlyphMaximize)), tr("最大化"));
    m_closeButton = createCaptionButton(QStringLiteral("captionClose"),
                                        QString(QChar(kGlyphClose)), tr("关闭"));
    connect(m_minButton, &QToolButton::clicked, this, &TitleBar::minimizeClicked);
    connect(m_maxButton, &QToolButton::clicked, this, &TitleBar::maximizeClicked);
    connect(m_closeButton, &QToolButton::clicked, this, &TitleBar::closeClicked);

    layout->addWidget(m_minButton);
    layout->addWidget(m_maxButton);
    layout->addWidget(m_closeButton);
}

bool TitleBar::isInteractiveAt(const QPoint &pos) const
{
    return m_languageButton->geometry().contains(pos)
        || m_eyeButton->geometry().contains(pos)
        || m_minButton->geometry().contains(pos)
        || m_maxButton->geometry().contains(pos)
        || m_closeButton->geometry().contains(pos);
}

// 启动回填：按持久化的语言 key 显示对应语言自称（未知 code 回落简体中文）
void TitleBar::updateLanguageButton(const QString &code)
{
    for (const auto &[name, langCode] : kLanguages) {
        if (langCode == code) {
            m_languageButton->setText(name + QStringLiteral(" ▾"));
            return;
        }
    }
    m_languageButton->setText(QStringLiteral("简体中文 ▾"));
}

void TitleBar::updateWatermarkEyeButton(bool hidden)
{
    QSignalBlocker blocker(m_eyeButton);   // 编程性同步不回发 toggled
    m_eyeButton->setChecked(hidden);
    m_eyeButton->setToolTip(hidden ? tr("显示水印")
                                   : tr("纯看图（隐藏水印）"));
}

void TitleBar::updateEyeEnabled(bool enabled)
{
    m_eyeButton->setEnabled(enabled);
}

void TitleBar::updateMaximizeButton(bool maximized)
{
    m_maxButton->setText(QString(QChar(maximized ? kGlyphRestore : kGlyphMaximize)));
    m_maxButton->setToolTip(maximized ? tr("向下还原") : tr("最大化"));
}

void TitleBar::paintEvent(QPaintEvent *)
{
    // 底色与窗口一体（#F5F6F8），下缘 1px 分隔线（04-UI设计.md §2.1）
    QPainter painter(this);
    painter.fillRect(rect(), QColor(QStringLiteral("#F5F6F8")));
    painter.setPen(QColor(QStringLiteral("#E4E7EC")));
    painter.drawLine(0, height() - 1, width() - 1, height() - 1);
}

void TitleBar::mousePressEvent(QMouseEvent *event)
{
    // 空白处按下仅记录起点，拖动阈值达成（mouseMoveEvent）才启动系统移动：
    // startSystemMove 会进入系统模态循环并吞掉后续鼠标事件，若在 press
    // 立即调用，双击的第二次 press 不再派发 → 双击最大化永远失效。
    if (event->button() == Qt::LeftButton
        && !isInteractiveAt(event->position().toPoint()))
        m_pressPos = event->globalPosition().toPoint();
    else
        m_pressPos = QPoint();
    QWidget::mousePressEvent(event);
}

void TitleBar::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_pressPos.isNull() && (event->buttons() & Qt::LeftButton)
        && (event->globalPosition().toPoint() - m_pressPos).manhattanLength()
               > QApplication::startDragDistance()
        && window() && window()->windowHandle()) {
        m_pressPos = QPoint();
        window()->windowHandle()->startSystemMove();
    }
    QWidget::mouseMoveEvent(event);
}

void TitleBar::mouseReleaseEvent(QMouseEvent *event)
{
    m_pressPos = QPoint();
    QWidget::mouseReleaseEvent(event);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *event)
{
    // 双击空白处 → 最大化/还原（原生 HTCAPTION 不可用，手动切换）
    if (event->button() == Qt::LeftButton
        && !isInteractiveAt(event->position().toPoint())) {
        emit maximizeClicked();
    }
    QWidget::mouseDoubleClickEvent(event);
}

QPixmap TitleBar::logoPixmap() const
{
    const qreal dpr = devicePixelRatioF();
    const QSize pixelSize(int(kLogoSize * dpr), int(kLogoSize * dpr));

    // 优先使用品牌资源
    QPixmap source(QStringLiteral(":/icons/logo.png"));
    if (!source.isNull()) {
        QPixmap scaled = source.scaled(pixelSize, Qt::IgnoreAspectRatio,
                                       Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
        return scaled;
    }

    // 资源缺失时的回退：135° 渐变圆角方块 + 首字母（04-UI设计.md §1）。
    // 以物理像素尺寸绘制并标记 dpr——否则高 DPI 下只画出左上 1/4 区域
    QPixmap fallback(pixelSize);
    fallback.fill(Qt::transparent);
    QPainter painter(&fallback);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, pixelSize.width(), pixelSize.height());
    gradient.setColorAt(0.0, QColor(QStringLiteral("#7C6AF0")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#5B8DEF")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(0, 0, pixelSize.width(), pixelSize.height(), 5, 5);
    painter.setPen(Qt::white);
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(qRound(14 * dpr));
    painter.setFont(font);
    painter.drawText(QRect(0, 0, pixelSize.width(), pixelSize.height()),
                     Qt::AlignCenter, QStringLiteral("F"));
    painter.end();
    fallback.setDevicePixelRatio(dpr);
    return fallback;
}

QToolButton *TitleBar::createCaptionButton(const QString &objectName,
                                           const QString &glyph,
                                           const QString &tooltip)
{
    auto *button = new QToolButton(this);
    button->setObjectName(objectName);
    button->setText(glyph);
    button->setFont(captionGlyphFont());
    button->setToolTip(tooltip);
    button->setFixedSize(kCaptionButtonWidth, kBarHeight);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    return button;
}

} // namespace fxt
