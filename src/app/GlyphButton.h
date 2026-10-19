#pragma once

#include "CaptionIcons.h"

#include <QColor>
#include <QString>
#include <QToolButton>
#include <QtGlobal>

namespace fxt {

// 工具条/面板标题共用的轻量图标按钮。
// 历史实现为 Segoe 图标字体字形（Win11 Fluent Icons / Win10 MDL2 Assets），
// 精简版 Windows 缺字体导致按钮全空白（2.0.1 实测），2.0.2 起改自绘矢量
// 图标（CaptionIcons.h）。原 Segoe 码位 → Glyph 映射保留在此，调用方
// 无需改动：
//   Undo E7A7 · FitPage E9A6 · Delete E74D · Copy E8C8
//   （Zoom E71E / ZoomIn E8A3 / RedEye E7B3 为历史候选，暂未接线；
//    E9A5「实际尺寸」两套字体本就无映射，此类语义用 createTextToolButton）
inline QToolButton *createTextToolButton(QWidget *parent,
                                         const QString &objectName,
                                         const QString &text,
                                         const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(text);
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    // 悬停手型光标（用户指令：图标按钮 hover 以手型为反馈，不出现边框）
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

inline QToolButton *createGlyphButton(QWidget *parent,
                                      const QString &objectName,
                                      char16_t glyph,
                                      const QString &tooltip)
{
    auto *button = createTextToolButton(parent, objectName, QString(), tooltip);
    // 图标色与 #toolGlyph QSS 同源：普通 #5A6068，悬停紫 #6C5CE7
    QIcon icon;
    switch (glyph) {
    case char16_t(0xE7A7):
        icon = glyphIcon(Glyph::Undo, QColor(QStringLiteral("#5A6068")),
                         QColor(QStringLiteral("#6C5CE7")));
        break;
    case char16_t(0xE9A6):
        icon = glyphIcon(Glyph::FitPage, QColor(QStringLiteral("#5A6068")),
                         QColor(QStringLiteral("#6C5CE7")));
        break;
    case char16_t(0xE74D):
        icon = glyphIcon(Glyph::Delete, QColor(QStringLiteral("#5A6068")),
                         QColor(QStringLiteral("#6C5CE7")));
        break;
    case char16_t(0xE8C8):
        icon = glyphIcon(Glyph::Copy, QColor(QStringLiteral("#5A6068")),
                         QColor(QStringLiteral("#6C5CE7")));
        break;
    default:
        // 新码位必须先在 CaptionIcons.h 画线稿并在此映射——防静默空白
        Q_ASSERT(!"createGlyphButton: 未映射的字形码位");
        icon = glyphIcon(Glyph::Info, QColor(QStringLiteral("#5A6068")),
                         QColor(QStringLiteral("#6C5CE7")));
        break;
    }
    button->setIcon(icon);
    button->setIconSize(QSize(12, 12));
    return button;
}

} // namespace fxt
