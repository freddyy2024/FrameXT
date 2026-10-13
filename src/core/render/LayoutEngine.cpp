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
    // 行内 pending 收集（左列/右列共用）。收紧：不可见 / 文本空 /
    // Logo 缺图 / 已手动定位 → 剔除。bandMetric=true 时 Logo/Divider
    // 以信息带整体高度度量（跨两行居中布局，对齐原版右块视觉）
    struct Pending {
        const Element *element;
        QString text;
        QImage logo;
        QSizeF size;
    };
    auto collectPending = [&](const QList<QUuid> &ids, qreal rowH,
                              bool bandMetric) -> QList<Pending> {
        QList<Pending> out;
        for (const QUuid &id : ids) {
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
            p.size = measure(*e, p.text, p.logo, canvasW,
                             bandMetric ? bandRect.height() : rowH);
            out.append(p);
        }
        return out;
    };
    // 行内排布（列内左对齐依次、垂直定位后写入 out；溢出时首个超列
    // 右缘的文本省略号收尾）。返回实际消耗宽度
    auto placePending = [&](const QList<Pending> &pending, const QRectF &colRect,
                            qreal gap, qreal fontScale) {
        qreal x = colRect.left();
        for (const Pending &p : pending) {
            // Logo/Divider 跨信息带垂直居中（右块 Logo 跨两行，原版视觉），
            // 文本元素行内垂直居中
            const bool spanBand = p.element->kind == ElementKind::Logo
                || p.element->kind == ElementKind::Divider;
            const qreal yTop = spanBand
                ? bandRect.center().y() - p.size.height() / 2.0
                : colRect.top() + (colRect.height() - p.size.height()) / 2.0;
            QRectF px(QPointF(x, yTop), p.size);
            QString text = p.text;
            const bool isText = p.element->kind == ElementKind::Text
                || p.element->kind == ElementKind::Free;
            if (isText && x + px.width() > colRect.right() + 0.5) {
                const QFont font = FontKit::create(p.element->style, canvasW, fontScale);
                text = QFontMetricsF(font).elidedText(p.text, Qt::ElideRight,
                                                      colRect.right() - x);
                px.setWidth(qMin(px.width(), colRect.right() - x));
            }
            out.insert(p.element->id, ElementLayout { px, text, fontScale });
            x += px.width() + gap;
            if (x > colRect.right())
                break;
        }
    };
    // 行溢出收缩系数：总宽超可用宽 → 整列等比缩字（下限 0.7×）；
    // shrunkenH 为收缩后行高（Logo/Divider 跨带度量不受行高影响，
    // measure 内部对这两类忽略传入行高以外的因素——此处传入仅影响文本）
    auto shrinkIfOverflow = [&](QList<Pending> &pending, qreal availW, qreal rowH,
                                bool bandMetric) -> qreal {
        qreal totalW = qMax(0, pending.size() - 1) * qMax(
            rowH * kItemGapRatio, canvasW * kItemGapMinRatio);
        for (const Pending &p : pending)
            totalW += p.size.width();
        if (totalW <= availW || totalW <= 0.0)
            return 1.0;
        const qreal scale = qBound(kRowShrinkMin, availW / totalW, 1.0);
        const qreal shrunkH = rowH * scale;
        for (Pending &p : pending)
            p.size = measure(*p.element, p.text, p.logo, canvasW,
                             bandMetric ? bandRect.height() : shrunkH, scale);
        return scale;
    };

    qreal rowTop = bandRect.top();
    for (const LayoutRow &row : rows) {
        const qreal rowH = bandRect.height() * row.heightRatio;
        const qreal gap = qMax(rowH * kItemGapRatio, canvasW * kItemGapMinRatio);

        // —— 右列先算：左列可用宽受其约束（左右列永不重叠）——
        QList<Pending> rightPending = collectPending(row.rightIds, rowH, true);
        qreal rightScale = 1.0;
        if (!rightPending.isEmpty())
            // 右列自身超带宽时收缩（极端长参数串兜底）
            rightScale = shrinkIfOverflow(rightPending, bandRect.width(), rowH, true);
        qreal rightBlockW = gap * qreal(rightPending.size() - 1);
        for (const Pending &p : rightPending)
            rightBlockW += p.size.width();

        QList<Pending> leftPending = collectPending(row.elementIds, rowH, false);

        // 整行剔除：左右列皆空或仅剩分隔线（divider 依附其他元素存在，
        // 无 Exif 照片不应在信息带留孤零零一条竖线）→ 不消耗行高，后续行前移
        bool hasContent = false;
        for (const Pending &p : leftPending + rightPending) {
            if (p.element->kind != ElementKind::Divider) {
                hasContent = true;
                break;
            }
        }
        if (!hasContent)
            continue;

        const QRectF rowRect(bandRect.left(), rowTop, bandRect.width(), rowH);
        rowTop += rowH;   // 仅实际占位的行推进 rowTop

        // 左列可用宽 = 带宽 - 右块占宽 - 列间间距
        const qreal leftAvailW = rightPending.isEmpty()
            ? bandRect.width()
            : qMax(0.0, bandRect.width() - rightBlockW - gap);
        const QRectF leftRect(bandRect.left(), rowTop - rowH, leftAvailW, rowH);
        const qreal leftScale = shrinkIfOverflow(leftPending, leftAvailW, rowH, false);
        placePending(leftPending, leftRect, gap, leftScale);

        // 右列：整体对齐信息带右缘（右缘与左列无重叠约束已由可用宽保证）
        if (!rightPending.isEmpty()) {
            QRectF rightRect(bandRect.right() - rightBlockW, rowTop - rowH,
                             rightBlockW, rowH);
            placePending(rightPending, rightRect, gap, rightScale);
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

        const QPointF base = anchorBase(e.anchor, plan);
        const QPointF topLeft = base + QPointF(e.offset.x() * canvasW,
                                               e.offset.y() * plan.canvasSize.height());
        out.insert(e.id, ElementLayout { QRectF(topLeft, size), text });
    }

    return out;
}

QPointF LayoutEngine::anchorBase(Anchor anchor, const CanvasPlan &plan)
{
    switch (anchor) {
    case Anchor::TopLeft:        return QPointF(0, 0);
    case Anchor::TopCenter:      return QPointF(plan.canvasSize.width() / 2.0, 0);
    case Anchor::TopRight:       return QPointF(plan.canvasSize.width(), 0);
    case Anchor::MiddleLeft:     return QPointF(0, plan.canvasSize.height() / 2.0);
    case Anchor::Center:         return QPointF(plan.canvasSize.width() / 2.0, plan.canvasSize.height() / 2.0);
    case Anchor::MiddleRight:    return QPointF(plan.canvasSize.width(), plan.canvasSize.height() / 2.0);
    case Anchor::BottomLeft:     return QPointF(0, plan.canvasSize.height());
    case Anchor::BottomCenter:   return QPointF(plan.canvasSize.width() / 2.0, plan.canvasSize.height());
    case Anchor::BottomRight:    return QPointF(plan.canvasSize.width(), plan.canvasSize.height());
    case Anchor::LogoLineStart:  return QPointF(plan.photoRect.left(), plan.photoRect.bottom());
    }
    return QPointF(0, 0);
}

} // namespace fxt
