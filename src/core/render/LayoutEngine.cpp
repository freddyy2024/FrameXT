#include "render/LayoutEngine.h"

#include <QFontMetricsF>

#include "render/CustomLogoCache.h"
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
// 行间垂直间隙（相对带高）：紧凑行距（用户指令：均分行高空隙过大，
// 2026-10-14），两行文字按内容高紧凑成组后整体在带内居中
constexpr qreal kRowGapRatio = 0.08;
// Logo 最大宽度（相对其度量参照高）：横条 Logo（Canon 4.8:1 等）
// 按 logoScale 全高渲染时过宽失衡，限宽后高度等比缩小
constexpr qreal kLogoMaxWidthRatio = 2.0;
// 单行补偿（用户指令 2026-10-15）：镜头缺失时机型放大一号（15%），
// 单行由紧凑落位自然在带内垂直居中
constexpr qreal kSingleLineBoost = 1.15;

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
        qreal h = element.logoScale * rowHeight;
        const qreal ratio = (logo.isNull() || logo.height() == 0)
                                ? 2.0   // 缺图占位比（黄金图校准点）
                                : qreal(logo.width()) / qreal(logo.height());
        qreal w = h * ratio;
        // 最大宽度约束（用户指令：横条 Logo 过长失衡，限宽后等比缩小）
        const qreal maxW = kLogoMaxWidthRatio * rowHeight;
        if (w > maxW && ratio > 0) {
            w = maxW;
            h = w / ratio;
        }
        return QSizeF(w, h);
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
            p.logo = (e->kind == ElementKind::Logo)
                ? CustomLogo::resolve(*e, logoResolver(e->fieldKey))
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

    // —— 行组排布（hasCustomPosition 的元素不参与）——
    // 三段式：① 各行右列收集/收缩 → 跨行文本列共同宽 + 装饰块宽；
    // ② 各行左列收集/收缩（可用宽受共同右块约束）→ 行内容高；
    // ③ 行内容紧凑成组（行间隙 kRowGapRatio，带内整体居中——用户指令：
    // 均分行高空隙过大）落位。
    // 右块内部分两段（对齐原版附件图）：文本列 = 参数串/日期，跨行共同
    // 左缘（左对齐成组，用户指令），最宽行右缘贴信息带右缘；装饰块 =
    // Logo+分隔线，跨两行垂直居中，位于文本列左侧
    struct RowSlot {
        QList<Pending> left, right;
        qreal leftScale = 1.0;
        qreal rightScale = 1.0;
        qreal contentH = 0.0;   // 行内容高（非跨带元素的最大高）
        qreal boost = 1.0;      // 单行补偿字号放大（机型独行时 1.15）
    };
    auto isSpanBand = [](const Pending &p) {
        return p.element->kind == ElementKind::Logo
            || p.element->kind == ElementKind::Divider;
    };
    // 列内元素间距：与行内容高挂钩（字大间距大），下限兜底防粘连
    auto gapFor = [&](qreal contentH) {
        return qMax(contentH * kItemGapRatio, canvasW * kItemGapMinRatio);
    };

    QList<RowSlot> rowSlots;
    qreal textColW = 0.0;      // 跨行最大右列文本宽（共同左缘基准）
    qreal decorColW = 0.0;     // 跨行最大装饰块宽（Logo+分隔线，含内部间距）
    for (const LayoutRow &row : rows) {
        const qreal rowH = bandRect.height() * row.heightRatio;
        RowSlot s;
        s.left = collectPending(row.elementIds, rowH, false);
        s.right = collectPending(row.rightIds, rowH, true);
        if (!s.right.isEmpty()) {
            // 右列自身超带宽时收缩（极端长参数串兜底）
            s.rightScale = shrinkIfOverflow(s.right, bandRect.width(), rowH, true);
            qreal tw = 0.0;    // 文本列宽
            qreal dw = 0.0;    // 装饰块宽
            int decorN = 0;
            qreal refH = 0.0;  // 间距参照高 = 右列文本最大高
            for (const Pending &p : s.right) {
                if (isSpanBand(p)) {
                    dw += p.size.width();
                    ++decorN;
                } else {
                    tw = qMax(tw, p.size.width());
                    refH = qMax(refH, p.size.height());
                }
            }
            dw += gapFor(refH) * qMax(0, decorN - 1);
            textColW = qMax(textColW, tw);
            decorColW = qMax(decorColW, dw);
        }
        rowSlots.append(s);
    }

    // 单行补偿（用户指令 2026-10-15）：镜头实际不可见（隐藏或字段值空，
    // 与 collectPending 同判定规则）时，机型字号放大一号重测——放大系数
    // 并入 fontScale 链（收缩比 × boost），度量与绘制同源；单行由紧凑
    // 落位自然在带内垂直居中
    auto fieldPlacedVisible = [&](const char *key) {
        const QString fk = QString::fromLatin1(key);
        for (const Element &e : elements) {
            if (e.fieldKey != fk)
                continue;
            if (!e.visible)
                continue;
            if (e.hasCustomPosition)
                return true;   // 手动定位独立渲染，视为存在
            return !displayText(e, resolver).isEmpty();
        }
        return false;
    };
    if (!fieldPlacedVisible("lens")) {
        for (RowSlot &s : rowSlots) {
            for (Pending &p : s.left) {
                if (p.element->fieldKey == QLatin1String("model")
                    && p.element->kind == ElementKind::Text) {
                    s.boost = kSingleLineBoost;
                    p.size = measure(*p.element, p.text, p.logo, canvasW,
                                     bandRect.height(), kSingleLineBoost);
                }
            }
        }
    }

    for (RowSlot &s : rowSlots) {
        if (s.right.isEmpty() && s.left.isEmpty())
            continue;
        // 先以未收缩内容高参与收缩判定
        qreal contentH = 0.0;
        for (const Pending &p : s.left + s.right) {
            if (!isSpanBand(p))
                contentH = qMax(contentH, p.size.height());
        }
        s.contentH = contentH;
    }

    // 行剔除与左列收缩（可用宽 = 带宽 - 共同右块宽 - 列间距）
    QList<RowSlot> placed;
    for (RowSlot &s : rowSlots) {
        // 整行剔除：左右列皆空或仅剩分隔线（divider 依附其他元素存在，
        // 无 Exif 照片不应在信息带留孤零零一条竖线）→ 不消耗行高
        bool hasContent = false;
        for (const Pending &p : s.left + s.right) {
            if (p.element->kind != ElementKind::Divider) {
                hasContent = true;
                break;
            }
        }
        if (!hasContent)
            continue;
        if (!s.left.isEmpty()) {
            const qreal avail = (textColW + decorColW) > 0.0
                ? qMax(0.0, bandRect.width() - textColW - decorColW
                                - gapFor(s.contentH))
                : bandRect.width();
            // 放大系数并入缩放链：最终 fontScale = 收缩比 × boost，
            // 绘制字号与度量保持同源（超宽仍按 0.7 下限收缩兜底）
            s.leftScale = shrinkIfOverflow(s.left, avail, s.contentH, false)
                          * s.boost;
            // 收缩后重算内容高
            qreal contentH = 0.0;
            for (const Pending &p : s.left + s.right) {
                if (!isSpanBand(p))
                    contentH = qMax(contentH, p.size.height());
            }
            s.contentH = contentH;
        }
        placed.append(s);
    }

    // 紧凑落位：行间隙 kRowGapRatio×带高；空间不足时压缩间隙，充足时
    // 整组垂直居中（freeH = 带高 - Σ内容高）
    const int n = placed.size();
    if (n > 0) {
        qreal usedH = 0.0;
        for (const RowSlot &s : placed)
            usedH += s.contentH;
        qreal rowGap = bandRect.height() * kRowGapRatio;
        const qreal freeH = bandRect.height() - usedH;
        qreal y0 = bandRect.top();
        if (freeH < rowGap * qreal(n - 1)) {
            rowGap = n > 1 ? qMax(0.0, freeH / qreal(n - 1)) : 0.0;
        } else {
            y0 += (freeH - rowGap * qreal(n - 1)) / 2.0;
        }

        for (const RowSlot &s : placed) {
            const qreal gap = gapFor(s.contentH);
            // 左列可用宽 = 带宽 - 共同右块宽 - 列间距（不重叠保证）
            const qreal leftAvailW = (textColW + decorColW) > 0.0
                ? qMax(0.0, bandRect.width() - textColW - decorColW - gap)
                : bandRect.width();
            const QRectF leftRect(bandRect.left(), y0, leftAvailW, s.contentH);
            placePending(s.left, leftRect, gap, s.leftScale);
            if (!s.right.isEmpty()) {
                // 右列拆两段：文本列共同左缘（参数/日期左对齐）+ 装饰块
                //（Logo/分隔线）在文本列左侧，跨带垂直居中
                QList<Pending> texts, decors;
                for (const Pending &p : s.right)
                    (isSpanBand(p) ? decors : texts).append(p);
                const qreal textLeft = bandRect.right() - textColW;
                if (!texts.isEmpty()) {
                    const QRectF textRect(textLeft, y0, textColW, s.contentH);
                    placePending(texts, textRect, gap, s.rightScale);
                }
                if (!decors.isEmpty()) {
                    const QRectF decorRect(textLeft - gap - decorColW, y0,
                                           decorColW, s.contentH);
                    placePending(decors, decorRect, gap, s.rightScale);
                }
            }
            y0 += s.contentH + rowGap;
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
            ? CustomLogo::resolve(e, logoResolver(e.fieldKey))
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
