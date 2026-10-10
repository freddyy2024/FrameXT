#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

#include "export/ExportOptions.h"

namespace fxt {

// JPEG 编码器（libjpeg-turbo TurboJPEG API 封装）。
// 为什么不用 QImageWriter：Qt 6.11 的 qjpeg 插件只暴露 quality，
// 不暴露色度子采样 —— 「视觉无损 = q100 + 4:4:4」（§3.3 预设语义、
// R2 画质 P0）无法达成，故编码直接走 turbo（方案 §11 风险预案点名的
// libjpeg-turbo）。解码仍走 QImage（Qt 内部同为 libjpeg，无双轨问题）。
namespace JpegEncoder {

// 编码失败时返回空数组并填 error（调用方可直接透传给失败清单）
QByteArray encode(const QImage &image, int quality, JpegSubsampling subsampling,
                  QString *error = nullptr);

} // namespace JpegEncoder

} // namespace fxt
