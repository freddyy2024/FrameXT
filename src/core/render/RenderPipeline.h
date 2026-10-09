#pragma once

#include <QImage>
#include <QString>

#include "model/PhotoDocument.h"
#include "render/LayoutEngine.h"
#include "render/RenderCacheManager.h"

namespace fxt {

enum class RenderPurpose { Preview, Export };

// 渲染请求（02-技术设计 §4.2）。相对单位制：scale 只影响输出分辨率，
// 元素比例跨尺寸一致——预览与导出走同一管线。
struct RenderRequest
{
    QUuid photoId;              // 缓存 key 组件
    QString filePath;
    TemplateState templateState;
    ExifOverrides overrides;    // M2 起参与内容解析（当前声明占位）
    qreal scale = 1.0;          // 预览 = min(1, 2048/maxEdge)；导出 = 1
    RenderPurpose purpose = RenderPurpose::Export;
};

struct RenderResult
{
    QImage image;        // S1–S6 产物（S7 编码 / S8 元数据自 M2 接入）
    QSize originalSize;  // 转向修正后、未缩放的原图尺寸
    bool ok = false;
    QString error;
};

// 8 阶段渲染管线的 S1–S6 组装：
//   S1 Decode → S2 Canvas → S3 Backdrop(缓存) → S4 Composite
//   → S5 Layout → S6 Paint     （S7/S8 由导出控制器在 M2 接续）
// 防抖约定：参数连续修改由 controller 层以 250ms QTimer 合并后调用（§4.2）。
class RenderPipeline
{
public:
    // cache 可注入（测试）；缺省使用进程级共享缓存（512MB LRU）
    static RenderResult run(const RenderRequest &request,
                            const FieldValueResolver &resolver,
                            const LogoResolver &logoResolver,
                            RenderCacheManager *cache = nullptr);

private:
    static RenderCacheManager *defaultCache();
};

} // namespace fxt
