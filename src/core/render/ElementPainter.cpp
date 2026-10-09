#include "render/ElementPainter.h"

#include "render/FontKit.h"

namespace fxt::ElementPainter {

namespace {
// 分隔线颜色跟随元素样式色（原版为浅灰竖线）
} // namespace

void paint(QPainter *painter, const Element &element, const ElementLayout &layout,
           const QImage &logo, qreal canvasWidth)
{
    if (!element.visible)
        return;

    painter->save();
    painter->setOpacity(element.style.opacity);

    switch (element.kind) {
    case ElementKind::Text:
    case ElementKind::Free: {
        if (layout.resolvedText.isEmpty())
            break;
        // 字号含布局层行溢出收缩系数（与度量同源，预览/导出一致）
        painter->setFont(FontKit::create(element.style, canvasWidth, layout.fontScale));
        painter->setPen(QPen(element.style.color));
        painter->drawText(layout.pxRect, Qt::AlignLeft | Qt::AlignVCenter,
                          layout.resolvedText);
        break;
    }
    case ElementKind::Logo: {
        if (logo.isNull())
            break;
        // 等比缩放到布局矩形（SmoothTransformation，Logo 缩小时防锯齿）
        painter->setRenderHint(QPainter::SmoothPixmapTransform);
        painter->drawImage(layout.pxRect, logo);
        break;
    }
    case ElementKind::Divider: {
        painter->fillRect(layout.pxRect, element.style.color);
        break;
    }
    }

    painter->restore();
}

void paintAll(QPainter *painter, const QList<Element> &elements,
              const QHash<QUuid, ElementLayout> &layouts,
              const LogoResolver &logoResolver, qreal canvasWidth)
{
    for (const Element &e : elements) {
        if (!layouts.contains(e.id))
            continue;
        const QImage logo = (e.kind == ElementKind::Logo) ? logoResolver(e.fieldKey)
                                                          : QImage();
        paint(painter, e, layouts.value(e.id), logo, canvasWidth);
    }
}

} // namespace fxt::ElementPainter
