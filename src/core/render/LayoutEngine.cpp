#include "render/LayoutEngine.h"

#include <QFontMetricsF>

#include "render/FontKit.h"

namespace fxt {

namespace {

// 行内相邻元素的水平间距（相对行高；黄金图校准点，M5 挪入模板 JSON）
constexpr qreal kItemGapRatio = 0.2;
// 间距下限（相对画布宽）：row1 行高较小（0.4×exH），纯比例间距会小到
// 两个字段视觉粘连（产品评审 P2），以 ≈0.5× 字号（0.028×0.5）兜底
constexpr qreal kItemGapMinRatio = 0.014;
// 分隔线：宽 = 行高×1.5%，高 = 行高×60%
constexpr qreal kDividerWidthRatio = 0.015;
constexpr qreal kDividerHeightRatio = 0.6;
// 行溢出收缩下限：整行等比缩字到 0.7× 仍溢出再省略号截断（产品评审 P1）
constexpr qreal kRowShrinkMin = 0.7;
// 信息带水平内边距（相对照片宽）：对齐原版 paddingLeftRight = 2.5%W
constexpr qreal kBandPaddingRatio = 0.025;

// 元素最终展示文本：literal（自由文本/覆盖值）优先，其次字段解析。
// 空串视为不可见（行组收紧剔除，不占位）。
QString displayText(const Element &element, const FieldValueResolver &resolver)
{
    if (!element.literal.isEmpty())
        return element.literal;
    if (element.kind == ElementKind::Text || element.kind == ElementKind::Free)
        return resolver(element.fieldKey);
    return QString();
}

bool resolvesToVisible(const Element &element, const QString &text, const QImage &logo)
{
    if (!element.visible)
        return false;
    if (element.kind == ElementKind::Logo)
        return !logo.isNull();   // 缺图即剔除，行内不留占位空洞（产品评审 P1）
    if (element.kind == ElementKind::Text || element.kind == ElementKind::Free)
        return !text.isEmpty();
    return true;   // Divider 恒可见
}

} // namespace

QSizeF LayoutEngine::measure(const Element &element, const QString &text,
                             const QImage &logo, qreal canvasWidth, qreal rowHeight,
                             qreal fontScaleFactor)
{
    switch (element.kind) {
    case ElementKind::Text: {
        const QFont font = FontKit::create(element.style, canvasWidth, fontScaleFactor);
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
        const QFont font = FontKit::create(element.style, canvasWidth, fontScaleFactor);
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

    // 空解析器防御：视为全部字段缺失（不 terminate）
    if (!resolver || !logoResolver)
        return out;

    // 信息带：与原图水平对齐、位于其下（三模板同构，§4.3）；
    // 左右各留 2.5% 照片宽内边距（对齐原版 paddingLeftRight，水印不顶格）
    const qreal canvasW = plan.canvasSize.width();
    const qreal bandPad = plan.photoRect.width() * kBandPaddingRatio;
    const QRectF bandRect(plan.photoRect.left() + bandPad, plan.photoRect.bottom() + 1.0,
                          plan.photoRect.width() - bandPad * 2.0, plan.exBandHeight);

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

        // 收集行内可排元素（收紧：不可见 / 文本空 / Logo 缺图 / 已手动定位 → 剔除）
        struct Pending {
            const Element *element;
            QString text;
            QImage logo;
            QSizeF size;
        };
        QList<Pending> pending;
        for (const QUuid &id : row.elementIds) {
            const Element *e = findById(id);
            if (!e || e->hasCustomPosition)
                continue;
            Pending p;
            p.element = e;
            p.text = displayText(*e, resolver);   // literal 优先，字段值兜底
            p.logo = (e->kind == ElementKind::Logo) ? logoResolver(e->fieldKey)
                                                    : QImage();
            if (!resolvesToVisible(*e, p.text, p.logo))
                continue;
            p.size = measure(*e, p.text, p.logo, canvasW, rowH);
            pending.append(p);
        }
        if (pending.isEmpty())
            continue;   // 整行剔除：不消耗行高，后续行前移（§4.3 自动收紧）

        // 全为分隔线的行也视为空：divider 依附行内其他元素存在，
        // 无 Exif 照片不应在信息带留孤零零一条竖线
        bool hasNonDivider = false;
        for (const Pending &p : pending) {
            if (p.element->kind != ElementKind::Divider) {
                hasNonDivider = true;
                break;
            }
        }
        if (!hasNonDivider)
            continue;

        const QRectF rowRect(bandRect.left(), rowTop, bandRect.width(), rowH);
        rowTop += rowH;   // 仅实际占位的行推进 rowTop

        // 行溢出：总宽超行宽 → 整行等比缩字（下限 0.7×）后按缩后行高重测
        const qreal gap = qMax(rowH * kItemGapRatio, canvasW * kItemGapMinRatio);
        qreal totalW = gap * qreal(pending.size() - 1);
        for (const Pending &p : pending)
            totalW += p.size.width();
        qreal fontScale = 1.0;
        if (totalW > rowRect.width() && totalW > 0.0)
            fontScale = qBound(kRowShrinkMin, rowRect.width() / totalW, 1.0);
        if (fontScale < 1.0) {
            const qreal shrunkH = rowH * fontScale;
            for (Pending &p : pending)
                p.size = measure(*p.element, p.text, p.logo, canvasW, shrunkH,
                                 fontScale);
        }

        // 行内左对齐依次排布，垂直居中于行矩形；仍溢出（收缩下限截断）时
        // 首个超右缘的文本以省略号收尾，其后元素不再排布（信息带右缘约束）
        qreal x = rowRect.left();
        for (const Pending &p : pending) {
            QRectF px(QPointF(x, rowRect.top() + (rowRect.height() - p.size.height()) / 2.0),
                      p.size);
            QString text = p.text;
            const bool isText = p.element->kind == ElementKind::Text
                || p.element->kind == ElementKind::Free;
            if (isText && x + px.width() > rowRect.right() + 0.5) {
                const QFont font = FontKit::create(p.element->style, canvasW, fontScale);
                text = QFontMetricsF(font).elidedText(p.text, Qt::ElideRight,
                                                      rowRect.right() - x);
                px.setWidth(qMin(px.width(), rowRect.right() - x));
            }
            out.insert(p.element->id, ElementLayout { px, text, fontScale });
            x += px.width() + gap;
            if (x > rowRect.right())
                break;
        }
    }

    // —— 手动定位优先（§4.3 v1.1）：锚点基准点 + offset × 画布尺寸 ——
    for (const Element &e : elements) {
        if (!e.hasCustomPosition)
            continue;
        const QString text = displayText(e, resolver);
        if ((e.kind == ElementKind::Text || e.kind == ElementKind::Free)
            && text.isEmpty())
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
