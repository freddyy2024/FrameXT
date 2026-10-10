#pragma once

#include <QByteArray>
#include <QHash>
#include <QSize>
#include <QSizeF>
#include <QString>

namespace fxt {

// Exif 结构化数据：导入时由 exiv2 + FieldRegistry 填充（02-技术设计 §4.5）。
// fields = FieldRegistry::resolveDisplay 的展示值集（fieldKey → 文本），
// 即"无覆盖层时的最终显示"；UI 覆盖层（ExifOverrides）在渲染装配时优先。
struct ExifData
{
    QString make;   // 品牌（BrandRegistry 匹配值，如 "Canon"）
    QString model;  // 机型原文，如 "NIKON Z 6_2"
    QHash<QString, QString> fields;  // fieldKey → 展示值（iso→"ISO 140" 等）

    bool isEmpty() const { return make.isEmpty() && model.isEmpty(); }
    bool operator==(const ExifData &) const = default;
};

// 一次解码形成的元数据快照（02-技术设计 §4.1，导入时解析一次）
struct PhotoMeta
{
    QSize pixelSize;
    QString format;    // "jpeg" / "png" / "webp" ...
    int bitDepth = 8;
    QSizeF dpi;        // JFIF/Exif 合成结果
    ExifData exif;
    QByteArray iccProfile;  // ICC 配置文件字节（导出保真写回，M2）
    bool hasGps = false;
    QString filePath;

    bool operator==(const PhotoMeta &) const = default;
};

} // namespace fxt
