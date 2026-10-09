#pragma once

#include <QHash>
#include <QImage>
#include <QPainter>
#include <QUuid>

#include "model/Element.h"
#include "render/LayoutEngine.h"

namespace fxt::ElementPainter {

// S6 元素绘制（02-技术设计 §4.2）：文本 / Logo / 分隔线 / 自由元素。
// 输入为 LayoutEngine 的解算结果（位置与已解析文本），不重复做内容解析。
// canvasWidth：相对单位制字号基准（与 LayoutEngine 度量同源，保证所见即所得）。
void paint(QPainter *painter, const Element &element, const ElementLayout &layout,
           const QImage &logo, qreal canvasWidth);

// 便捷入口：按布局表逐个绘制（含 hasCustomPosition 的元素）
void paintAll(QPainter *painter, const QList<Element> &elements,
              const QHash<QUuid, ElementLayout> &layouts,
              const LogoResolver &logoResolver, qreal canvasWidth);

} // namespace fxt::ElementPainter
