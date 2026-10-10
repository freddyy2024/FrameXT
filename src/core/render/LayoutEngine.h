#pragma once

#include <QHash>
#include <QImage>
#include <QUuid>
#include <functional>

#include "model/Element.h"
#include "render/CanvasGeometry.h"

namespace fxt {

// 字段显示值解析器：fieldKey → 最终展示文本。
// 返回空串视为字段缺失（行组收紧时剔除该元素，不占位）。
// controller 层以 ExifFormatter（M2）装配；字面值/覆盖由实现方按优先级合并。
using FieldValueResolver = std::function<QString(const QString &fieldKey)>;

// 品牌 Logo 图解析：fieldKey（品牌 key）→ Logo 图；空图视为缺失。
using LogoResolver = std::function<QImage(const QString &fieldKey)>;

// 模板行组定义（02-技术设计 §4.3）。M5 起由模板 JSON 解析，
// 内置三模板先在代码内构造（常量集中、黄金图校准点）。
struct LayoutRow
{
    int index = 0;            // 行序（信息带内自上而下）
    qreal heightRatio = 0.6;  // 该行占信息带高度的比例（黄金图校准点）
    QList<QUuid> elementIds;  // 行内元素（水平排列顺序）
};

// 单元素布局结果
struct ElementLayout
{
    QRectF pxRect;         // 画布像素矩形
    QString resolvedText;  // 解析后的显示文本（Painter 直接使用；行溢出时已是省略文本）
    qreal fontScale = 1.0; // 行溢出收缩系数（Painter 与布局度量同字号）
};

// S5 布局引擎（02-技术设计 §4.3）：
// - 9 宫格锚点 + 行组自动收紧（visible=false / 字段为空 → 行内重排、行高重算）
// - hasCustomPosition=true 的元素脱离行组，按 锚点+offset×画布 直算（手动定位优先）
class LayoutEngine
{
public:
    // rows 中未出现的元素（如 hasCustomPosition）也会被解算
    static QHash<QUuid, ElementLayout> resolve(const QList<Element> &elements,
                                               const QList<LayoutRow> &rows,
                                               const CanvasPlan &plan,
                                               const FieldValueResolver &resolver,
                                               const LogoResolver &logoResolver);

    // 锚点在画布上的基准点（手动定位参考系：topLeft = base + offset×画布）。
    // 公开给 controller 层做「行组位置 → offset 参考系」的物化换算。
    static QPointF anchorBase(Anchor anchor, const CanvasPlan &plan);

private:
    // 元素显示尺寸度量（文本宽高 / Logo 等比 / 分隔线固定比例）；
    // fontScaleFactor：行溢出收缩系数（与 ElementPainter 绘制字号同源）
    static QSizeF measure(const Element &element, const QString &text,
                          const QImage &logo, qreal canvasWidth, qreal rowHeight,
                          qreal fontScaleFactor = 1.0);
};

} // namespace fxt
