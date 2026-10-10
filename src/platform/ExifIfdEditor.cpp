#include "platform/ExifIfdEditor.h"

#include <QtGlobal>

#include <cstring>

namespace fxt::Platform {

namespace {

// TIFF 常量
constexpr int kTiffHeaderSize = 8;   // II/MM + 42 + IFD0 offset
constexpr int kIfdEntrySize = 12;    // tag(2) + type(2) + count(4) + value/offset(4)
constexpr quint16 kTypeShort = 3;
constexpr quint16 kTypeRational = 5;

// Exif type → 单元素字节数；未知 type 返回 0（不参与数据区运算）
quint32 typeSizeOf(quint16 type)
{
    switch (type) {
    case 1: case 2: case 6: case 7: return 1;   // BYTE/ASCII/SBYTE/UNDEFINED
    case 3: case 8: return 2;                    // SHORT/SSHORT
    case 4: case 9: case 11: return 4;           // LONG/SLONG/FLOAT
    case 5: case 10: case 12: return 8;          // RATIONAL/SRATIONAL/DOUBLE
    default: return 0;
    }
}

// 小端读取（II）；大端（MM）由 lambda 适配，调用侧无感。
// 索引统一 qint64 且拒绝负数——防御畸形 offset 回绕（评审 P0：
// valueOffset≈0xFFFFFFFF 时 quint32 加法回绕可绕过边界检查致堆越界写）
struct ByteOrder
{
    bool little = true;

    quint16 u16(const QByteArray &d, int pos) const
    {
        if (pos < 0 || qint64(pos) + 2 > qint64(d.size()))
            return 0;
        const uchar a = uchar(d.at(pos));
        const uchar b = uchar(d.at(pos + 1));
        return little ? quint16(a | (b << 8)) : quint16((a << 8) | b);
    }
    quint32 u32(const QByteArray &d, int pos) const
    {
        if (pos < 0 || qint64(pos) + 4 > qint64(d.size()))
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
        if (pos < 0 || qint64(pos) + 2 > qint64(d.size()))
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
        if (pos < 0 || qint64(pos) + 4 > qint64(d.size()))
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
    if (qint64(ifdOffset) + 2 > qint64(tiff.size()))
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
    // qint64 运算防回绕：valueOffset≈0xFFFFFFFF 时 quint32 加法回为小值
    // 即可绕过检查 → put32 负索引堆越界写（评审 P0）
    if (qint64(valueOffset) + 8 > qint64(tiff.size()))
        return tiff;
    QByteArray out = tiff;
    bo.put32(out, int(valueOffset), numerator);
    bo.put32(out, int(valueOffset) + 4, denominator);
    return out;
}

QByteArray ExifIfdEditor::setAscii(const QByteArray &tiff, Ifd ifd,
                                   quint16 tag, const QByteArray &value)
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
    // 只覆写 ASCII(2)；数据区长度 count 不变（绝对 offset 保真红线）
    if (bo.u16(tiff, pos + 2) != 2)
        return tiff;
    const quint32 count = bo.u32(tiff, pos + 4);
    if (count == 0)
        return tiff;

    // ASCII count 含结尾 \0，可写正文字节上限 = count-1；
    // 超长截断（同长度约束下无法扩写）
    const int maxChars = int(count) - 1;
    if (maxChars <= 0)
        return tiff;

    QByteArray out = tiff;
    if (count <= 4) {
        // 值内联在 entry 的 value 字段（≤4 字节）
        for (quint32 i = 0; i < count; ++i)
            out[pos + 8 + int(i)] = i < quint32(value.size())
                                        ? value.at(int(i))
                                        : '\0';
    } else {
        const quint32 valueOffset = bo.u32(tiff, pos + 8);
        if (qint64(valueOffset) + qint64(count) > qint64(tiff.size()))
            return tiff;
        for (quint32 i = 0; i < count; ++i)
            out[int(valueOffset) + int(i)] =
                i < quint32(value.size()) ? value.at(int(i)) : '\0';
    }
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
    quint32 gpsIfdOffset = 0;
    for (int i = 0; i < entryCount; ++i) {
        const int pos = entriesPos + i * kIfdEntrySize;
        if (bo.u16(tiff, pos) == TagGpsInfoPointer) {
            gpsPos = pos;
            gpsIfdOffset = bo.u32(tiff, pos + 8);
            break;
        }
    }
    if (gpsPos < 0)
        return tiff;   // 无 GPS，原样

    QByteArray out = tiff;

    // 1) 清零 GPS IFD 区域与其数据区（§4.8 删指针 + 删 tag 数据——只删
    //    指针时坐标字节仍留在文件内可被取证还原，评审 P0）
    if (gpsIfdOffset >= kTiffHeaderSize && gpsIfdOffset < quint32(out.size())) {
        int gCount = 0;
        int gEntriesPos = 0;
        if (locateIfd(out, bo, gpsIfdOffset, gCount, gEntriesPos)) {
            for (int i = 0; i < gCount; ++i) {
                const int pos = gEntriesPos + i * kIfdEntrySize;
                const quint16 type = bo.u16(out, pos + 2);
                const quint64 total =
                    quint64(bo.u32(out, pos + 4)) * typeSizeOf(type);
                // 值不在 entry 内联（>4 字节）→ 数据区在 offset 处，清零
                if (total > 4) {
                    const quint32 dataOff = bo.u32(out, pos + 8);
                    if (dataOff < quint32(out.size())) {
                        const int clearLen = int(qMin<quint64>(
                            total, quint64(out.size()) - dataOff));
                        if (clearLen > 0)
                            memset(out.data() + dataOff, 0, clearLen);
                    }
                }
            }
            // GPS IFD 自身（count + entries + next 指针）
            const qint64 region = 2 + qint64(gCount) * kIfdEntrySize + 4;
            if (qint64(gpsIfdOffset) + region <= out.size())
                memset(out.data() + gpsIfdOffset, 0, int(region));
        }
    }

    // 2) 删除 IFD0 的 GPS entry：仅在 entry 区内部前移（后续 entry 各前移
    //    12 字节、entry 区尾部 12 字节置零、count-1）。总长不变，IFD0 之后
    //    的所有数据（ExifIFD、MakerNote、缩略图等）位置不动——外部 offset
    //    引用全部保持有效（评审 P0：QByteArray::remove 整体前移会使
    //    0x8769 等指针全部错位）
    const int tail = entriesPos + entryCount * kIfdEntrySize;
    std::memmove(out.data() + gpsPos, out.data() + gpsPos + kIfdEntrySize,
                 size_t(tail - (gpsPos + kIfdEntrySize)));
    memset(out.data() + tail - kIfdEntrySize, 0, kIfdEntrySize);
    bo.put16(out, int(ifd0), quint16(entryCount - 1));
    return out;
}

} // namespace fxt::Platform
