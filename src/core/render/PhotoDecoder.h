#pragma once

#include <QImage>
#include <QSize>
#include <QString>

namespace fxt::PhotoDecoder {

// S1 解码（02-技术设计 §4.2）：
// - autoTransform 按 Exif Orientation 自动转向（修原版"竖拍照片方向错误"类问题）
// - 预览时 scale<1：setScaledSize 下采样解码（45MP 预览不解全尺寸，控制内存与耗时）
// - ICC 配置文件字节由 M2 platform 层另行读取，此处不处理
struct DecodeResult
{
    QImage image;        // 解码结果（已转向/已下采样）
    QSize originalSize;  // 原始像素尺寸（转向修正后、缩放前）
    QString format;      // "jpeg" / "png" / "webp" ...
    bool ok = false;
    QString error;       // 失败原因（E10 健壮性：坏文件提示跳过）
};

// scale: 1.0=全尺寸（导出）；<1.0=预览下采样（scale = min(1, 2048/maxEdge)）
DecodeResult decode(const QString &filePath, qreal scale = 1.0);

} // namespace fxt::PhotoDecoder
