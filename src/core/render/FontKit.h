#pragma once

#include <QFont>
#include <QtGlobal>

#include "model/Element.h"

namespace fxt::FontKit {

// TextStyle → QFont（相对单位制：字号 = fontScale × 画布宽）。
// 值类型约束（§4.1）：QFont 仅在渲染层即时构造，模型不持有。
inline QFont create(const TextStyle &style, qreal canvasWidth)
{
    QFont font(style.fontFamily);
    font.setPixelSize(qMax(1, qRound(style.fontScale * canvasWidth)));
    font.setBold(style.bold);
    return font;
}

} // namespace fxt::FontKit
