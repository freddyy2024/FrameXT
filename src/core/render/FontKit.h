#pragma once

#include <QFont>
#include <QtGlobal>

#include "model/Element.h"

namespace fxt::FontKit {

// TextStyle → QFont（相对单位制：字号 = fontScale × 画布宽）。
// factor：布局层行溢出收缩系数（LayoutEngine 等比缩字后同步给绘制层，
// 保证度量与绘制同字号）；默认 1.0 = 不收缩。
// 值类型约束（§4.1）：QFont 仅在渲染层即时构造，模型不持有。
inline QFont create(const TextStyle &style, qreal canvasWidth, qreal factor = 1.0)
{
    QFont font(style.fontFamily);
    font.setPixelSize(qMax(1, qRound(style.fontScale * factor * canvasWidth)));
    font.setBold(style.bold);
    font.setItalic(style.italic);
    // 字间距与字号同制（相对画布宽），保证预览/导出比例一致
    if (style.letterSpacing != 0.0)
        font.setLetterSpacing(QFont::AbsoluteSpacing,
                              style.letterSpacing * canvasWidth);
    return font;
}

} // namespace fxt::FontKit
