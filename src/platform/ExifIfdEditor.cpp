#include "platform/ExifIfdEditor.h"

#include <QtGlobal>

namespace fxt::Platform {

namespace {

// TIFF 常量
constexpr int kTiffHeaderSize = 8;   // II/MM + 42 + IFD0 offset
constexpr int kIfdEntrySize = 12;    // tag(2) + type(2) + count(4) + value/offset(4)
constexpr quint16 kTypeShort = 3;
constexpr quint16 kTypeRational = 5;

// 小端读取（II）；大端（MM）由 lambda 适配，调用侧无感
struct ByteOrder
{
    bool little = true;

    quint16 u16(const QByteArray &d, int pos) const
    {
        if (pos + 2 > d.size())
            return 0;
        const uchar a = uchar(d.at(pos));
        const uchar b = uchar(d.at(pos + 1));
        return little ? quint16(a | (b << 8)) : quint16((a << 8) | b);
    }
    quint32 u32(const QByteArray &d, int pos) const
    {
        if (pos + 4 > d.size())
            return 0;
        const uchar a = uchar(d.at(pos));
        const uchar b = uchar(d.at(pos + 1));
        const uchar c = uchar(d.at(pos + 2));
        const uchar e = uchar(d.at(pos + 3));
        return little ? quint32(a | (b << 8) | (c << 16) | (e << 24))
                      : quint32((a << 24) | (b << 16) | (c << 8) | e);
    }
    void put16(QByteArray &d, int pos, quint16 v) const
    {
        if (pos + 2 > d.size())
            return;
        if (little) {
            d[pos] = char(v & 0xFF);
            d[pos + 1] = char((v >> 8) & 0xFF);
        } else {
            d[pos] = char((v >> 8) & 0xFF);
            d[pos + 1] = char(v & 0xFF);
        }
    }
    void put32(QByteArray &d, int pos, quint32 v) const
    {
        if (pos + 4 > d.size())
            return;
        if (little) {
            d[pos] = char(v & 0xFF);
            d[pos + 1] = char((v >> 8) & 0xFF);
            d[pos + 2] = char((v >> 16) & 0xFF);
            d[pos + 3] = char((v >> 24) & 0xFF);
        } else {
            d[pos] = char((v >> 24) & 0xFF);
            d[pos + 1] = char((v >> 16) & 0xFF);
            d[pos + 2] = char((v >> 8) & 0xFF);
            d[pos + 3] = char(v & 0xFF);
        }
    }
};

// 解析 TIFF 头，返回 {字节序, IFD0 offset}；畸形返回 false
bool parseHeader(const QByteArray &tiff, ByteOrder &bo, quint32 &ifd0Offset)
{
    if (tiff.size() < kTiffHeaderSize)
        return false;
    const QByteArray magic = tiff.left(2);
    if (magic == QByteArray("II"))
        bo.little = true;
    else if (magic == QByteArray("MM"))
        bo.little = false;
    else
        return false;
    if (bo.u16(tiff, 2) != 42)
        return false;
    ifd0Offset = bo.u32(tiff, 4);
    return ifd0Offset >= kTiffHeaderSize && ifd0Offset < quint32(tiff.size());
}

// 定位 IFD：返回 entryCount 与 entry 区起始（绝对 offset）；畸形返回 false
bool locateIfd(const QByteArray &tiff, const ByteOrder &bo, quint32 ifdOffset,
               int &entryCount, int &entriesPos)
{
    if (ifdOffset + 2 > quint32(tiff.size()))
        return false;
    entryCount = int(bo.u16(tiff, int(ifdOffset)));
    entriesPos = int(ifdOffset) + 2;
    // 防御：entry 区必须完整在数据内（环路 IFD 的 offset 也越不出文件）
    if (entryCount < 0 || entriesPos + qint64(entryCount) * kIfdEntrySize + 4
        > tiff.size())
        return false;
    return true;
}

// 在指定 IFD 中找 tag 的 entry 绝对位置；未找到返回 -1
int findEntry(const QByteArray &tiff, const ByteOrder &bo, quint32 ifdOffset,
              quint16 tag)
{
    int entryCount = 0;
    int entriesPos = 0;
    if (!locateIfd(tiff, bo, ifdOffset, entryCount, entriesPos))
        return -1;
    for (int i = 0; i < entryCount; ++i) {
        const int pos = entriesPos + i * kIfdEntrySize;
        if (bo.u16(tiff, pos) == tag)
            return pos;
    }
    return -1;
}

// ExifIFD offset（经 IFD0 的 0x8769 指针）；无 ExifIFD 返回 0
quint32 exifIfdOffset(const QByteArray &tiff, const ByteOrder &bo)
{
    const int pos = findEntry(tiff, bo, bo.u32(tiff, 4), 0x8769);
    if (pos < 0)
        return 0;
    const quint32 offset = bo.u32(tiff, pos + 8);
    if (offset < kTiffHeaderSize || offset >= quint32(tiff.size()))
        return 0;
    return offset;
}

quint32 ifdOffsetFor(const QByteArray &tiff, const ByteOrder &bo, ExifIfdEditor::Ifd ifd)
{
    if (ifd == ExifIfdEditor::Ifd::Ifd0)
        return bo.u32(tiff, 4);
    return exifIfdOffset(tiff, bo);
}

} // namespace

QByteArray ExifIfdEditor::setShort(const QByteArray &tiff, Ifd ifd, quint16 tag,
                                   quint16 value)
{
    ByteOrder bo;
    quint32 ifd0 = 0;
    if (!parseHeader(tiff, bo, ifd0))
        return tiff;
    const quint32 target = ifdOffsetFor(tiff, bo, ifd);
    if (target == 0)
        return tiff;
    const int pos = findEntry(tiff, bo, target, tag);
    if (pos < 0)
        return tiff;
    // 只覆写 SHORT 定长值（count=1 内联在 entry 的 value 字段）
    if (bo.u16(tiff, pos + 2) != kTypeShort || bo.u32(tiff, pos + 4) != 1)
        return tiff;
    QByteArray out = tiff;
    bo.put16(out, pos + 8, value);
    return out;
}

QByteArray ExifIfdEditor::setRational(const QByteArray &tiff, Ifd ifd, quint16 tag,
                                      quint32 numerator, quint32 denominator)
{
    ByteOrder bo;
    quint32 ifd0 = 0;
    if (!parseHeader(tiff, bo, ifd0))
        return tiff;
    const quint32 target = ifdOffsetFor(tiff, bo, ifd);
    if (target == 0)
        return tiff;
    const int pos = findEntry(tiff, bo, target, tag);
    if (pos < 0)
        return tiff;
    // RATIONAL(5) count=1：值在数据区（entry 的 value 字段是 offset）
    if (bo.u16(tiff, pos + 2) != kTypeRational || bo.u32(tiff, pos + 4) != 1)
        return tiff;
    const quint32 valueOffset = bo.u32(tiff, pos + 8);
    if (valueOffset + 8 > quint32(tiff.size()))
        return tiff;
    QByteArray out = tiff;
    bo.put32(out, int(valueOffset), numerator);
    bo.put32(out, int(valueOffset) + 4, denominator);
    return out;
}

QByteArray ExifIfdEditor::stripGps(const QByteArray &tiff)
{
    ByteOrder bo;
    quint32 ifd0 = 0;
    if (!parseHeader(tiff, bo, ifd0))
        return tiff;

    int entryCount = 0;
    int entriesPos = 0;
    if (!locateIfd(tiff, bo, ifd0, entryCount, entriesPos))
        return tiff;

    int gpsPos = -1;
    for (int i = 0; i < entryCount; ++i) {
        const int pos = entriesPos + i * kIfdEntrySize;
        if (bo.u16(tiff, pos) == TagGpsInfoPointer) {
            gpsPos = pos;
            break;
        }
    }
    if (gpsPos < 0)
        return tiff;   // 无 GPS，原样

    // 删除该 entry：后续 entry 前移 12 字节、count-1、entry 区尾部补零。
    // IFD 外部数据（被 offset 引用）位置全部不变，引用安全。
    QByteArray out = tiff;
    out.remove(gpsPos, kIfdEntrySize);
    out.append(QByteArray(kIfdEntrySize, char(0)));   // 补齐长度，外部 offset 不变
    bo.put16(out, int(ifd0), quint16(entryCount - 1));
    return out;
}

} // namespace fxt::Platform
