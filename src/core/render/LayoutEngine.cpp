#include "render/LayoutEngine.h"

#include <QFontMetricsF>

#include "render/FontKit.h"

namespace fxt {

namespace {

// 行内相邻元素的水平间距（相对行高；黄金图校准点，M5 挪入模板 JSON）
constexpr qreal kItemGapRatio = 0.2;
// 分隔线：宽 = 行高×1.5%，高 = 行高×60%
constexpr qreal kDividerWidthRatio = 0.015;
constexpr qreal kDividerHeightRatio = 0.6;
// Logo：高 = logoScale × 行高，宽按图片宽高比

bool resolvesToVisible(const Element &element, const FieldValueResolver &resolver)
{
    if (!element.visible)
        return false;
    if (element.kind == ElementKind::Text)
        return !resolver(element.fieldKey).isEmpty();
    if (element.kind == ElementKind::Logo)
        return true;   // 缺图由 LogoResolver 决定，布局按占位比处理
    return true;
}

} // namespace

QSizeF LayoutEngine::measure(const Element &element, const QString &text,
                             const QImage &logo, qreal canvasWidth, qreal rowHeight)
{
    switch (element.kind) {
    case ElementKind::Text: {
        const QFont font = FontKit::create(element.style, canvasWidth);
        const QFontMetricsF metrics(font);
        return QSizeF(metrics.horizontalAdvance(text), metrics.height());
    }
    case ElementKind::Logo: {
        const qreal h = element.logoScale * rowHeight;
        const qreal ratio = (logo.isNull() || logo.height() == 0)
                                ? 2.0   // 缺图占位比（黄金图校准点）
                                : qreal(logo.width()) / qreal(logo.height());
        return QSizeF(h * ratio, h);
    }
    case ElementKind::Divider:
        return QSizeF(rowHeight * kDividerWidthRatio, rowHeight * kDividerHeightRatio);
    case ElementKind::Free: {
        const QFont font = FontKit::create(element.style, canvasWidth);
        const QFontMetricsF metrics(font);
        return QSizeF(metrics.horizontalAdvance(text), metrics.height());
    }
    }
    return QSizeF();
}

QHash<QUuid, ElementLayout> LayoutEngine::resolve(const QList<Element> &elements,
                                                  const QList<LayoutRow> &rows,
                                                  const CanvasPlan &plan,
                                                  const FieldValueResolver &resolver,
                                                  const LogoResolver &logoResolver)
{
    QHash<QUuid, ElementLayout> out;

    // 信息带：与原图水平对齐、位于其下（三模板同构，§4.3）
    const qreal canvasW = plan.canvasSize.width();
    const QRectF bandRect(plan.photoRect.left(), plan.photoRect.bottom() + 1.0,
                          plan.photoRect.width(), plan.exBandHeight);

    const auto findById = [&elements](const QUuid &id) -> const Element * {
        for (const Element &e : elements)
            if (e.id == id)
                return &e;
        return nullptr;
    };

    // —— 行组排布（hasCustomPosition 的元素不参与）——
    qreal rowTop = bandRect.top();
    for (const LayoutRow &row : rows) {
        const qreal rowH = bandRect.height() * row.heightRatio;
        const QRectF rowRect(bandRect.left(), rowTop, bandRect.width(), rowH);
        rowTop += rowH;

        // 收集行内可排元素（收紧：不可见 / 字段为空 / 已手动定位 → 剔除）
        struct Pending {
            const Element *element;
            QString text;
            QImage logo;
            QSizeF size;
        };
        QList<Pending> pending;
        for (const QUuid &id : row.elementIds) {
            const Element *e = findById(id);
            if (!e || e->hasCustomPosition || !resolvesToVisible(*e, resolver))
                continue;
            Pending p;
            p.element = e;
            p.text = resolver(e->fieldKey);
            p.logo = (e->kind == ElementKind::Logo) ? logoResolver(e->fieldKey)
                                                    : QImage();
            p.size = measure(*e, p.text, p.logo, canvasW, rowH);
            pending.append(p);
        }
        if (pending.isEmpty())
            continue;

        // 行内左对齐依次排布，垂直居中于行矩形
        const qreal gap = rowH * kItemGapRatio;
        qreal x = rowRect.left();
        for (const Pending &p : pending) {
            const qreal y = rowRect.top() + (rowRect.height() - p.size.height()) / 2.0;
            out.insert(p.element->id,
                       ElementLayout { QRectF(QPointF(x, y), p.size), p.text });
            x += p.size.width() + gap;
        }
    }

    // —— 手动定位优先（§4.3 v1.1）：锚点基准点 + offset × 画布尺寸 ——
    for (const Element &e : elements) {
        if (!e.hasCustomPosition)
            continue;
        const QString text = (e.kind == ElementKind::Text || e.kind == ElementKind::Free)
                                 ? resolver(e.fieldKey)
                                 : QString();
        if (e.kind == ElementKind::Text && text.isEmpty())
            continue;   // 字段空仍不渲染（内容缺失与位置无关）

        const QImage logo = (e.kind == ElementKind::Logo)
                                ? logoResolver(e.fieldKey)
                                : QImage();
        // 手动定位元素不绑定行高，度量以信息带整体高度为参照
        const QSizeF size = measure(e, text, logo, canvasW, plan.exBandHeight);

        QPointF base;
        switch (e.anchor) {
        case Anchor::TopLeft:        base = QPointF(0, 0); break;
        case Anchor::TopCenter:      base = QPointF(canvasW / 2.0, 0); break;
        case Anchor::TopRight:       base = QPointF(canvasW, 0); break;
        case Anchor::MiddleLeft:     base = QPointF(0, plan.canvasSize.height() / 2.0); break;
        case Anchor::Center:         base = QPointF(canvasW / 2.0, plan.canvasSize.height() / 2.0); break;
        case Anchor::MiddleRight:    base = QPointF(canvasW, plan.canvasSize.height() / 2.0); break;
        case Anchor::BottomLeft:     base = QPointF(0, plan.canvasSize.height()); break;
        case Anchor::BottomCenter:   base = QPointF(canvasW / 2.0, plan.canvasSize.height()); break;
        case Anchor::BottomRight:    base = QPointF(canvasW, plan.canvasSize.height()); break;
        case Anchor::LogoLineStart:  base = QPointF(plan.photoRect.left(), plan.photoRect.bottom()); break;
        }

        const QPointF topLeft = base + QPointF(e.offset.x() * canvasW,
                                               e.offset.y() * plan.canvasSize.height());
        out.insert(e.id, ElementLayout { QRectF(topLeft, size), text });
    }

    return out;
}

} // namespace fxt
