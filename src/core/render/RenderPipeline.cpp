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

// 导入像素上限（§10.4：超大图必须显式报错而非崩溃/静默废图）：
// 100MP × 4B ≈ 400MB/张，叠加画布与合成峰值仍在单 worker 安全范围；
// 32767 为 QImage 单边硬上限
constexpr qint64 kMaxPixels = 100LL * 1000 * 1000;
constexpr qint64 kMaxEdge = 32767;

// S3 背景缓存 key：照片 + 模板 + 画布尺寸已覆盖底图差异
// （文字/元素参数不影响 S3 —— 改文字必然命中缓存，这是 <80ms 指标的根基）。
// '\n' 分隔而非 arg 链：templateId 为任意用户字符串（M5 自定义模板），
// 含 "%1" 时 arg 链会错位造成不同条目同 key。
// M5 模板参数化后：影响 S3 的参数（模糊强度/圆角/底色）必须纳入 key。
QString backdropKey(const RenderRequest &req, const CanvasPlan &plan)
{
    // M5.1：模板参数并入缓存键。blurStrength 不改变画布尺寸，若不入键，
    // 调强度滑杆会命中旧背景缓存（拖动无反应）；其余参数改变画布尺寸
    // 本身就会 miss，统一入键无害。
    QString params;
    for (auto it = req.templateState.templateParams.constBegin();
         it != req.templateState.templateParams.constEnd(); ++it)
        params += it.key() + QLatin1Char('=') + it.value().toString()
                  + QLatin1Char(';');
    return req.photoId.toString(QUuid::WithoutBraces) + QLatin1Char('\n')
        + req.templateState.templateId + QLatin1Char('\n')
        + QString::number(plan.canvasSize.width()) + QLatin1Char('x')
        + QString::number(plan.canvasSize.height()) + QLatin1Char('\n')
        + params;
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

    // —— 入口防御：未知模板 / 超大图显式报错（§10.4 红线，绝不静默废图）——
    if (!CanvasGeometry::isBuiltin(request.templateState.templateId)) {
        result.error = QStringLiteral("未知模板：%1").arg(request.templateState.templateId);
        return result;
    }

    // —— S1 解码（Exif 转向 + 预览下采样）——
    const auto decoded = PhotoDecoder::decode(request.filePath, request.scale);
    if (!decoded.ok) {
        result.error = decoded.error;
        return result;
    }
    result.originalSize = decoded.originalSize;
    if (decoded.image.isNull()) {
        result.error = QStringLiteral("解码结果为空：%1").arg(request.filePath);
        return result;
    }
    const qint64 pixels = qint64(decoded.image.width()) * qint64(decoded.image.height());
    if (pixels > kMaxPixels
        || decoded.image.width() > kMaxEdge || decoded.image.height() > kMaxEdge) {
        result.error = QStringLiteral("图片尺寸超出处理上限（%1 MP）")
                           .arg(qint64(kMaxPixels / (1000 * 1000)));
        return result;
    }

    // —— S2 画布几何（相对单位制：以缩放后尺寸解算，比例恒定；
    //      M5.1 起模板参数参与解算，缺失键回落内置常量）——
    const CanvasPlan plan =
        CanvasGeometry::plan(request.templateState.templateId,
                             decoded.image.size(),
                             request.templateState.templateParams);
    if (plan.canvasSize.isEmpty()) {
        result.error = QStringLiteral("画布解算失败：%1")
                           .arg(request.templateState.templateId);
        return result;
    }

    // —— S3 背景（缓存优先）——
    const QString key = backdropKey(request, plan);
    QImage backdrop = cache->backdrop(key);
    if (backdrop.isNull()) {
        backdrop = BackdropRenderer::backdrop(
            plan, request.templateState.templateId, decoded.image,
            CanvasGeometry::paramOf(request.templateState.templateParams,
                                    "blurStrength", 1.0));
        // 导出图不写入共享预览缓存：单张导出底图 ≈180MB 会挤掉全部预览条目
        // （§4.2 缓存为预览防抖服务，导出不依赖预览历史）
        if (request.purpose == RenderPurpose::Preview)
            cache->insertBackdrop(key, backdrop);
    }
    // 直接共享缓存副本；S4 写入时 QPainter 触发 detach（缓存条目保持纯背景，
    // 深拷贝是保护共享条目的必要成本）
    result.image = backdrop;

    // —— S4 原图合成（blur 模板带圆角蒙版）——
    BackdropRenderer::composite(result.image, plan, decoded.image);

    // —— S5 布局 + S6 元素绘制 ——
    const auto layouts = LayoutEngine::resolve(
        request.templateState.elements,
        BuiltinTemplates::defaultRows(request.templateState.elements), plan,
        resolver, logoResolver);
    QPainter painter(&result.image);
    if (!painter.isActive()) {
        result.error = QStringLiteral("画布创建失败（内存不足或尺寸超限）");
        return result;
    }
    ElementPainter::paintAll(&painter, request.templateState.elements, layouts,
                             logoResolver, plan.canvasSize.width());
    painter.end();

    result.ok = !result.image.isNull();
    if (!result.ok)
        result.error = QStringLiteral("渲染结果为空");
    return result;
}

} // namespace fxt
