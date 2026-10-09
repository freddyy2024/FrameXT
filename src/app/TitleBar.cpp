#include "TitleBar.h"

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

// Segoe 图标字形：最小化 / 最大化 / 还原 / 关闭（与系统标题按钮同源字形）
constexpr char16_t kGlyphMinimize = 0xE921;
constexpr char16_t kGlyphMaximize = 0xE922;
constexpr char16_t kGlyphRestore  = 0xE923;
constexpr char16_t kGlyphClose    = 0xE8BB;

QFont captionGlyphFont()
{
    QFont font;
    font.setFamilies({ QStringLiteral("Segoe Fluent Icons"),      // Win11
                       QStringLiteral("Segoe MDL2 Assets") });    // Win10 回退
    font.setPointSize(9);
    return font;
}

} // namespace

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

    // 语言切换：M3 接入 i18n 运行时切换，当前先展示语言菜单
    m_languageButton = new QToolButton(this);
    m_languageButton->setObjectName(QStringLiteral("languageButton"));
    m_languageButton->setText(tr("中文") + QStringLiteral(" ▾"));
    m_languageButton->setToolTip(tr("切换界面语言"));
    m_languageButton->setCursor(Qt::PointingHandCursor);
    m_languageButton->setFocusPolicy(Qt::NoFocus);

    auto *languageMenu = new QMenu(m_languageButton);
    languageMenu->addAction(QStringLiteral("简体中文"));
    languageMenu->addAction(QStringLiteral("繁體中文"));
    languageMenu->addAction(QStringLiteral("English"));
    m_languageButton->setMenu(languageMenu);
    m_languageButton->setPopupMode(QToolButton::InstantPopup);
    layout->addWidget(m_languageButton);

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
        || m_minButton->geometry().contains(pos)
        || m_maxButton->geometry().contains(pos)
        || m_closeButton->geometry().contains(pos);
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
    // 空白处按下 → 系统拖拽移动窗口（Qt 6 无边框标准做法；
    // 按钮/语言菜单区域由子控件自行处理，不会走到这里）
    if (event->button() == Qt::LeftButton
        && !isInteractiveAt(event->position().toPoint())
        && window() && window()->windowHandle()) {
        window()->windowHandle()->startSystemMove();
    }
    QWidget::mousePressEvent(event);
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

    // 资源缺失时的回退：135° 渐变圆角方块 + 首字母（04-UI设计.md §1）
    QPixmap fallback(pixelSize);
    fallback.fill(Qt::transparent);
    QPainter painter(&fallback);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(0, 0, pixelSize.width(), pixelSize.height());
    gradient.setColorAt(0.0, QColor(QStringLiteral("#7C6AF0")));
    gradient.setColorAt(1.0, QColor(QStringLiteral("#5B8DEF")));
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawRoundedRect(0, 0, kLogoSize, kLogoSize, 5, 5);
    painter.setPen(Qt::white);
    QFont font = this->font();
    font.setBold(true);
    font.setPixelSize(14);
    painter.setFont(font);
    painter.drawText(QRect(0, 0, kLogoSize, kLogoSize), Qt::AlignCenter,
                     QStringLiteral("F"));
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
