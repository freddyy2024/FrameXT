#include "render/RenderPipeline.h"

#include <QPainter>

#include "render/BackdropRenderer.h"
#include "render/BuiltinTemplates.h"
#include "render/CanvasGeometry.h"
#include "render/ElementPainter.h"
#include "render/LayoutEngine.h"
#include "render/PhotoDecoder.h"

namespace fxt {

namespace {
using namespace BuiltinTemplate;

// S3 背景缓存 key：照片 + 模板 + 画布尺寸已覆盖底图差异
// （文字/元素参数不影响 S3 —— 改文字必然命中缓存，这是 <80ms 指标的根基）
QString backdropKey(const RenderRequest &req, const CanvasPlan &plan)
{
    return QStringLiteral("%1|%2|%3x%4")
        .arg(req.photoId.toString(QUuid::WithoutBraces), req.templateState.templateId)
        .arg(plan.canvasSize.width())
        .arg(plan.canvasSize.height());
}

} // namespace

RenderCacheManager *RenderPipeline::defaultCache()
{
    static RenderCacheManager s_cache;   // 进程级共享，512MB LRU
    return &s_cache;
}

RenderResult RenderPipeline::run(const RenderRequest &request,
                                 const FieldValueResolver &resolver,
                                 const LogoResolver &logoResolver,
                                 RenderCacheManager *cache)
{
    if (cache == nullptr)
        cache = defaultCache();

    RenderResult result;

    // —— S1 解码（Exif 转向 + 预览下采样）——
    const auto decoded = PhotoDecoder::decode(request.filePath, request.scale);
    if (!decoded.ok) {
        result.error = decoded.error;
        return result;
    }
    result.originalSize = decoded.originalSize;

    // —— S2 画布几何（相对单位制：以缩放后尺寸解算，比例恒定）——
    const CanvasPlan plan =
        CanvasGeometry::plan(request.templateState.templateId, decoded.image.size());
    result.image = QImage(plan.canvasSize, QImage::Format_ARGB32_Premultiplied);

    // —— S3 背景（缓存优先）——
    const QString key = backdropKey(request, plan);
    QImage backdrop = cache->backdrop(key);
    if (backdrop.isNull()) {
        backdrop = BackdropRenderer::backdrop(plan, request.templateState.templateId,
                                              decoded.image);
        cache->insertBackdrop(key, backdrop);
    }
    result.image = backdrop;

    // —— S4 原图合成（blur 模板带圆角蒙版）——
    BackdropRenderer::composite(result.image, plan, decoded.image);

    // —— S5 布局 + S6 元素绘制 ——
    const auto layouts = LayoutEngine::resolve(
        request.templateState.elements,
        BuiltinTemplates::defaultRows(request.templateState.elements), plan,
        resolver, logoResolver);
    QPainter painter(&result.image);
    ElementPainter::paintAll(&painter, request.templateState.elements, layouts,
                             logoResolver, plan.canvasSize.width());
    painter.end();

    result.ok = true;
    return result;
}

} // namespace fxt
