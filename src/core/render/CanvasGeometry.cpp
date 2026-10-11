#include "render/CanvasGeometry.h"

#include <QtGlobal>

namespace fxt::CanvasGeometry {

namespace {

using namespace fxt::BuiltinTemplate;

// 参数键（与 TemplateSpec.cpp 同一拼写约定；此处只做读取回落）
namespace PK {
inline constexpr const char *kExBandLandscape = "exBandRatioLandscape";
inline constexpr const char *kExBandPortrait = "exBandRatioPortrait";
inline constexpr const char *kAroundInset = "aroundInsetRatio";
inline constexpr const char *kAroundBottom = "aroundBottomRatio";
inline constexpr const char *kBlurMargin = "blurMarginRatio";
inline constexpr const char *kBlurBottomGap = "blurBottomGapRatio";
inline constexpr const char *kBlurCorner = "blurCornerRadiusRatio";
} // namespace PK

} // namespace

qreal paramOf(const QVariantMap &params, const char *key, qreal fallback)
{
    const auto it = params.constFind(QLatin1String(key));
    if (it == params.constEnd())
        return fallback;
    bool ok = false;
    const qreal v = it->toDouble(&ok);
    return ok ? v : fallback;
}

bool isBuiltin(const QString &templateId)
{
    return templateId == QLatin1String(kBottom)
        || templateId == QLatin1String(kAround)
        || templateId == QLatin1String(kBlur);
}

CanvasPlan plan(const QString &templateId, const QSize &photoSize,
                const QVariantMap &params)
{
    const int w = photoSize.width();
    const int h = photoSize.height();

    // 信息带高：横图 0.09W / 竖图 0.10W（原版 exHeight 视觉常量，§4.4；
    // 参数缺省 = 常量，黄金图校准点勿动默认值）
    const qreal exL = paramOf(params, PK::kExBandLandscape, 0.09);
    const qreal exP = paramOf(params, PK::kExBandPortrait, 0.10);
    const qreal exH = photoSize.width() >= photoSize.height() ? w * exL
                                                              : w * exP;

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
        const int inset = qRound(w * paramOf(params, PK::kAroundInset, 0.02));
        const qreal bottomW = paramOf(params, PK::kAroundBottom, 0.02);
        out.canvasSize = QSize(w + inset * 2, qRound(h + exH + w * bottomW));
        out.photoRect = QRect(inset, inset, w, h);
    } else if (templateId == QLatin1String(kBlur)) {
        // 原图居中带圆角，模糊背景铺满画布
        const int mLR = qRound(w * paramOf(params, PK::kBlurMargin, 0.08));
        const int mT = qRound(exH / 2.0);
        const int mB = qRound(exH
                              + h * paramOf(params, PK::kBlurBottomGap, 0.01));
        out.canvasSize = QSize(w + mLR * 2, h + mT + mB);
        out.photoRect = QRect(mLR, mT, w, h);
        out.cornerRadius = w * paramOf(params, PK::kBlurCorner, 1.0 / 60.0);
        out.roundedPhoto = true;
    }
    return out;
}

CanvasPlan plan(const QString &templateId, const QSize &photoSize)
{
    return plan(templateId, photoSize, QVariantMap());
}

} // namespace fxt::CanvasGeometry