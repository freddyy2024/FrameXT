#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include "platform/ExifIfdEditor.h"

namespace fxt::Platform {

// 导出元数据策略（02-技术设计 §4.8）
struct ExportPolicy
{
    enum class DpiMode { Source, Fixed };

    bool keepExif = true;
    bool stripGps = false;       // E2 隐私开关：摘除 GPS IFD
    bool keepIcc = true;
    bool keepIptcXmp = true;
    DpiMode dpiMode = DpiMode::Source;
    int fixedDpi = 300;          // dpiMode==Fixed 时生效
    // 默认开启：段搬运路径仅在源已有 Software 时同长度覆写（不新增、
    // 不移动字节，保守）；版本串由 core 层组装后覆盖 softwareValue
    bool softwareTag = true;
    QByteArray softwareValue = QByteArrayLiteral("FrameXT");
    QSize outputSize;            // 写 PixelX/YDimension 用（画布尺寸）

    bool operator==(const ExportPolicy &) const = default;
};

// 元数据复用器：把源文件元数据段搬运进编码器输出（S8 阶段，§4.8）。
// JPEG→JPEG：段级搬运 + 定点覆写（字节级保真路径）。
// 其他组合：重建路径（从 Exif TIFF 关键段子集合成；无 Exif 源不写 APP1）。
namespace MetadataMuxer {

// JPEG 输出复用。sourceJpeg 为原文件字节（读段），encodedJpeg 为编码器输出。
// policy.keepExif=false 时不搬 APP1；stripGps 覆写后摘 GPS；
// ICC(APP2 多段) / IPTC-XMP(APP13) / COM 按 policy 搬运。
QByteArray composeJpeg(const QByteArray &encodedJpeg, const QByteArray &sourceJpeg,
                       const ExportPolicy &policy);

// JPEG 输出 + 重建路径（源为 PNG 等非 JPEG，或调用方已有 Exif TIFF 块）。
// exifTiff 为裸 TIFF（PNG eXIf chunk 内容或调用方合成）；空且 keepExif 时不写 APP1。
QByteArray composeJpegFromExif(const QByteArray &encodedJpeg,
                               const QByteArray &exifTiff,
                               const QByteArray &iccProfile,
                               const ExportPolicy &policy);

// PNG 输出：eXIf 搬运（可覆写 GPS 摘除）+ iCCP 拷贝 + pHYs 写 DPI
QByteArray composePng(const QByteArray &encodedPng, const QByteArray &sourcePng,
                      const ExportPolicy &policy);

// PNG 输出 + 跨格式重建路径（源为 JPEG 等非 PNG）：eXIf 从 raw TIFF 构建
// （套导出覆写）、iCCP 从裸 ICC 字节压缩写入、pHYs 按 dpiMode 写 DPI。
// 无 Exif 的源（exifTiff 空）不写 eXIf。
QByteArray composePngFromRaw(const QByteArray &encodedPng,
                             const QByteArray &exifTiff,
                             const QByteArray &iccProfile,
                             const ExportPolicy &policy);

// 从源 JPEG 提取 APP1 Exif 的裸 TIFF 块（去 "Exif\0\0" 头）；无则返回空
QByteArray exifTiffFromJpeg(const QByteArray &sourceJpeg);

} // namespace MetadataMuxer

} // namespace fxt::Platform
