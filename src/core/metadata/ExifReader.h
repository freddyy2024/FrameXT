#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>

#include "metadata/FieldRegistry.h"

namespace fxt {

// Exif 读取器（02-技术设计 §4.5，exiv2 实现——0.28 API）。
// 每次读取独立构造（§2.4 线程安全约定：worker 内独享）。
// 读取失败/非图像文件 → ok=false + error；字段级缺失静默置空（§4.5）。
class ExifReader
{
public:
    explicit ExifReader(const QString &filePath);

    bool read();   // 成功后 raw() 可用

    const RawExif &raw() const { return m_raw; }
    // ICC 配置文件全字节（M4 起提取；无 ICC 源为空——导出重建路径按
    // 「无则不写」处理）
    const QByteArray &iccProfile() const { return m_icc; }
    const QString &error() const { return m_error; }

    // 解码后原图尺寸（Exif.Image 优先，SensorPixel 计量兜底）；未知返回 0×0
    QSize pixelSize() const;

private:
    QString m_filePath;
    RawExif m_raw;
    QByteArray m_icc;
    QString m_error;
};

} // namespace fxt
