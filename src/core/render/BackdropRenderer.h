#pragma once

#include <QImage>

#include "render/CanvasGeometry.h"

namespace fxt::BackdropRenderer {

// S3 背景生成（02-技术设计 §4.2/§4.4）：
// - bottom/around：白底画布
// - blur：原图 downscale(≤512) → boxBlur×2 → 放大铺满画布
// 输出固定 ARGB32_Premultiplied（圆角蒙版与后续元素绘制的统一格式）
// M5.1：blurStrength = 模糊强度系数（σ = W/20 × strength，默认 1.0 =
// 原视觉常量；仅 blur 模板消费，其余模板忽略）
QImage backdrop(const CanvasPlan &plan, const QString &templateId,
                const QImage &photo, qreal blurStrength = 1.0);

// S4 原图合成：将原图绘入 plan.photoRect（blur 模板施加圆角蒙版）
void composite(QImage &canvas, const CanvasPlan &plan, const QImage &photo);

} // namespace fxt::BackdropRenderer
