#pragma once

#include <QFont>
#include <QString>
#include <QToolButton>
#include <QtGlobal>

namespace fxt {

// Segoe Fluent 图标字形按钮（工具条/面板标题共用的轻量图标按钮）。
// 字形两族字体回退：Win11 Segoe Fluent Icons / Win10 Segoe MDL2 Assets。
// 常用字形：FitPage E9A6 · ActualSize E9A5 · Copy E8C8 · Undo E7A7
//           · Delete E74D（Microsoft Learn "Segoe MDL2 assets icons"）。
inline QFont glyphFont(int pointSize = 10)
{
    QFont font;
    font.setFamilies({ QStringLiteral("Segoe Fluent Icons"),     // Win11
                       QStringLiteral("Segoe MDL2 Assets") });   // Win10 回退
    font.setPointSize(pointSize);
    return font;
}

inline QToolButton *createGlyphButton(QWidget *parent,
                                      const QString &objectName,
                                      char16_t glyph,
                                      const QString &tooltip)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setText(QString(QChar(glyph)));
    button->setFont(glyphFont());
    button->setToolTip(tooltip);
    button->setFocusPolicy(Qt::NoFocus);
    button->setAutoRaise(true);
    return button;
}

} // namespace fxt
