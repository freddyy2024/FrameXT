#include "render/CanvasGeometry.h"

#include <QtGlobal>

namespace fxt::CanvasGeometry {

namespace {

using namespace fxt::BuiltinTemplate;

// 底部信息带高：横图 0.09W / 竖图 0.10W（原版 exHeight 视觉常量，§4.4）
qreal exBandHeightFor(const QSize &photoSize)
{
    const qreal w = photoSize.width();
    return photoSize.width() >= photoSize.height() ? w * 0.09 : w * 0.10;
}

} // namespace

bool isBuiltin(const QString &templateId)
{
    return templateId == QLatin1String(kBottom)
        || templateId == QLatin1String(kAround)
        || templateId == QLatin1String(kBlur);
}

CanvasPlan plan(const QString &templateId, const QSize &photoSize)
{
    const int w = photoSize.width();
    const int h = photoSize.height();
    const qreal exH = exBandHeightFor(photoSize);

    CanvasPlan out;
    // 无效尺寸/未知模板 → 空 plan（canvasSize 0×0）；RenderPipeline 以
    // isBuiltin + canvasSize 校验兜底报错，不产出空白废图
    if (!photoSize.isValid() || w <= 0 || h <= 0 || !isBuiltin(templateId))
        return out;
    out.exBandHeight = exH;

    if (templateId == QLatin1String(kBottom)) {
        // 原图置顶，底部加信息带
        out.canvasSize = QSize(w, qRound(h + exH));
        out.photoRect = QRect(0, 0, w, h);
    } else if (templateId == QLatin1String(kAround)) {
        // 四周 2%W 内边距，底部信息带 exH；
        // 画布宽由 inset 反推（w + 2×inset），保证左右白边像素对称
        const int inset = qRound(w * 0.02);
        out.canvasSize = QSize(w + inset * 2, qRound(h + exH + w * 0.02));
        out.photoRect = QRect(inset, inset, w, h);
    } else if (templateId == QLatin1String(kBlur)) {
        // 原图居中带圆角，模糊背景铺满画布
        const int mLR = qRound(w * 0.08);
        const int mT = qRound(exH / 2.0);
        const int mB = qRound(exH + h * 0.01);
        out.canvasSize = QSize(w + mLR * 2, h + mT + mB);
        out.photoRect = QRect(mLR, mT, w, h);
        out.cornerRadius = w / 60.0;
        out.roundedPhoto = true;
    }
    return out;
}

} // namespace fxt::CanvasGeometry
