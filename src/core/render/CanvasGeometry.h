#pragma once

#include <QRect>
#include <QSize>
#include <QVariantMap>

namespace fxt {

// 内置模板 id（与 resources/templates/ 的模板 JSON、TemplateState::templateId 一致）
namespace BuiltinTemplate {
inline constexpr const char *kBottom = "bottom";  // 底部边框
inline constexpr const char *kAround = "around";  // 环绕边框
inline constexpr const char *kBlur   = "blur";    // 高斯模糊
} // namespace BuiltinTemplate

// S2 画布几何（02-技术设计 §4.4 常量表）：
// 相对单位制下按原图尺寸解算画布尺寸与原图贴入位置——预览/导出共用，比例永不漂移。
struct CanvasPlan
{
    QSize canvasSize;
    QRect photoRect;        // 原图贴入位置（画布坐标，px）
    qreal cornerRadius = 0; // 原图圆角半径 px（0 = 不圆角）
    bool roundedPhoto = false;  // 原图是否需要圆角蒙版（blur 模板）
    qreal exBandHeight = 0; // 底部信息带高 px（LayoutEngine 行组锚定用）
};

namespace CanvasGeometry {

bool isBuiltin(const QString &templateId);

// 三模板画布规则（视觉常量来源标注在实现内，与原版 1.0.5 人工比对校准）：
//  bottom: H' = H + exH            （exH = 0.09W 横图 / 0.10W 竖图）
//  around: W' = W + 4%W; H' = H + exH + 2%W; 原图 inset 2%W
//  blur:   画布 = 原图 + L/R 8%W + T exH/2 + B exH+1%H；圆角 = W/60
// M5.1 参数化：params 为 TemplateState::templateParams（参数键约定见
// TemplateSpec.cpp），缺失/越界的键回落内置常量——空 params 与旧版逐像素一致。
CanvasPlan plan(const QString &templateId, const QSize &photoSize,
                const QVariantMap &params);
// 兼容入口（无参数 = 全默认值；既有测试/黄金图走此签名）
CanvasPlan plan(const QString &templateId, const QSize &photoSize);

// 参数读取约定：缺失或类型不符回落 fallback（越界不钳制——钳制是
// UI 滑杆的职责，渲染层保持对模板包参数的宽容）
qreal paramOf(const QVariantMap &params, const char *key, qreal fallback);

} // namespace CanvasGeometry
} // namespace fxt
