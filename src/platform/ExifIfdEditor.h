#pragma once

#include <QByteArray>

namespace fxt::Platform {

// TIFF/Exif IFD 定点编辑器（02-技术设计 §4.8）：
// 输入为 APP1(Exif) payload 去掉 "Exif\0\0" 头后的 TIFF 数据。
// 只做定长 tag 覆写与 GPS IFD 摘除，不重建 IFD（保真红线：其他字节不动）。
// 畸形输入（坏字节序/越界 offset/IFD 环）一律原样返回，绝不崩溃。
namespace ExifIfdEditor {

// IFD 选择器（Exif 主链）
enum class Ifd {
    Ifd0,     // 主 IFD（Orientation / Resolution / GPS 指针在此）
    Exif,     // ExifIFD（PixelX/YDimension 等）
};

// tag 常量（只列出会用到的）
enum Tag : quint16 {
    TagOrientation = 0x0112,
    TagXResolution = 0x011A,
    TagYResolution = 0x011B,
    TagResolutionUnit = 0x0128,
    TagSoftware = 0x0131,
    TagPixelXDimension = 0xA002,
    TagPixelYDimension = 0xA003,
    TagGpsInfoPointer = 0x8825,   // IFD0 → GPS IFD 指针
};

// 覆写 IFD 内已有的 SHORT tag（in-place，字节级不移动其他数据）。
// tag 不存在 / 类型非 SHORT(3) 时原样返回（缺失即取 Exif 缺省语义）。
QByteArray setShort(const QByteArray &tiff, Ifd ifd, quint16 tag, quint16 value);

// 覆写 IFD 内已有的 RATIONAL tag（8 字节：num/den 各 4 字节）。
QByteArray setRational(const QByteArray &tiff, Ifd ifd, quint16 tag,
                       quint32 numerator, quint32 denominator);

// 同长度覆写 IFD 内已有的 ASCII tag（Software=0x0131 等）：value 字节
// （不含结尾 \0）超过 entry 原数据区长度时截断；剩余空间以 \0 填充。
// 变长 tag 不能改写数据区长度——所有 IFD 外 offset 均为绝对值，移动即错位。
// tag 不存在 / 类型非 ASCII(2) 时原样返回（保真红线：不新增 entry）。
QByteArray setAscii(const QByteArray &tiff, Ifd ifd, quint16 tag,
                    const QByteArray &value);

// 摘除 GPS IFD：从 IFD0 删除 GPSInfoPointer entry（entry 区前移补位，
// IFD 外数据 offset 均不变，外部引用安全）；无 GPS 时原样返回。
QByteArray stripGps(const QByteArray &tiff);

} // namespace ExifIfdEditor

} // namespace fxt::Platform
