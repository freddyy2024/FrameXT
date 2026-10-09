#pragma once

#include <QByteArray>
#include <QSize>
#include <QSizeF>
#include <QString>

namespace fxt {

// Exif 结构化数据：M2 由 exiv2 + FieldRegistry 填充（02-技术设计 §4.5）；
// M1 先占位品牌基础字段（BrandRegistry 匹配依赖）。
struct ExifData
{
    QString make;   // 品牌，如 "Canon"
    QString model;  // 机型，如 "Canon EOS R5"
    // TODO(M2): RawExif 全量 tag / GPS / MakerNote 品牌特色字段

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
