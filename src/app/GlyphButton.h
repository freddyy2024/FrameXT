#pragma once

#include <QFont>
#include <QString>
#include <QToolButton>
#include <QtGlobal>

namespace fxt {

// Segoe 图标字形按钮（工具条/面板标题共用的轻量图标按钮）。
// 字形两族字体回退：Win11 Segoe Fluent Icons / Win10 Segoe MDL2 Assets。
// 常用字形（已实测存在）：FitPage E9A6 · Zoom E71E · ZoomIn E8A3 ·
//   Copy E8C8 · Undo E7A7 · Delete E74D · RedEye E7B3。
// 注意：两套字体都不含「实际尺寸/100%」字形——曾用的 E9A5 经 fontTools
//   解析 cmap 与离屏渲染双重验证均无映射（按钮显示空白），此类语义请用
//   createTextToolButton（如「1:1」）。
inline QFont glyphFont(int pointSize = 10)
{
    QFont font;
    font.setFamilies({ QStringLiteral("Segoe Fluent Icons"),     // Win11
                       QStringLiteral("Segoe MDL2 Assets") });   // Win10 回退
    font.setPointSize(pointSize);
    return font;
}

// 文字型工具按钮（图标字体无对应字形时的表达，如「1:1 原始尺寸」）：
// 样式/光标与图标按钮一致（共用 #toolGlyph QSS）
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
    auto *button = createTextToolButton(parent, objectName,
                                        QString(QChar(glyph)), tooltip);
    button->setFont(glyphFont());
    return button;
}

} // namespace fxt
