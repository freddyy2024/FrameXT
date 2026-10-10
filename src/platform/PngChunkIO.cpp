#include "platform/PngChunkIO.h"

#include <QtEndian>
#include <QtGlobal>

namespace fxt::Platform {

namespace {
constexpr int kSignatureSize = 8;
const char kPngSignature[kSignatureSize] = {
    char(0x89), 'P', 'N', 'G', char(0x0D), char(0x0A), char(0x1A), char(0x0A)
};

// PNG 标准_CRC-32（ISO 3309，zlib 多项式 0xEDB88320 反射式）
quint32 crc32Of(const QByteArray &data)
{
    static quint32 table[256];
    static bool init = false;
    if (!init) {
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        init = true;
    }
    quint32 crc = 0xFFFFFFFFu;
    for (uchar ch : data)
        crc = table[(crc ^ ch) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

// 4 字节大端写入
void appendU32(QByteArray &out, quint32 v)
{
    uchar b[4];
    qToBigEndian<quint32>(v, b);
    out.append(reinterpret_cast<const char *>(b), 4);
}

// chunk 序列化：len(4) + type(4) + data + crc32(type+data)
QByteArray chunkBytes(const PngChunkIO::Chunk &chunk)
{
    QByteArray out;
    out.reserve(12 + chunk.data.size());
    appendU32(out, quint32(chunk.data.size()));
    out.append(chunk.type);
    out.append(chunk.data);
    appendU32(out, crc32Of(chunk.type + chunk.data));
    return out;
}
} // namespace

bool PngChunkIO::isValidPng(const QByteArray &png)
{
    return png.size() >= kSignatureSize + 12
        && memcmp(png.constData(), kPngSignature, kSignatureSize) == 0;
}

QList<PngChunkIO::Chunk> PngChunkIO::parse(const QByteArray &png)
{
    QList<Chunk> out;
    if (!isValidPng(png))
        return out;

    int i = kSignatureSize;
    const int n = png.size();
    while (i + 12 <= n) {
        const quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(png.constData() + i));
        const QByteArray type = png.mid(i + 4, 4);
        // 长度越界 / 类型残缺：畸形，停止（已解析段仍有效）
        if (length > quint32(n - i - 12) || type.size() != 4)
            break;
        Chunk c;
        c.type = type;
        c.data = png.mid(i + 8, int(length));
        out.append(c);
        if (type == QByteArray("IEND"))
            break;
        i += 12 + int(length);   // len(4) + type(4) + data + crc(4)
    }
    return out;
}

QByteArray PngChunkIO::setChunk(const QByteArray &png, const Chunk &newChunk)
{
    if (!isValidPng(png))
        return png;

    const int bodyStart = kSignatureSize;
    int i = bodyStart;
    QByteArray kept;   // 保留的非同类型 chunk（到 IEND 前）
    bool iendFound = false;
    const int n = png.size();
    while (i + 12 <= n) {
        const quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(png.constData() + i));
        const QByteArray type = png.mid(i + 4, 4);
        if (length > quint32(n - i - 12))
            return png;   // 畸形，原样
        const int total = 12 + int(length);
        const bool sameType = !newChunk.type.isEmpty() && type == newChunk.type;
        if (type == QByteArray("IEND")) {
            iendFound = true;
            // IEND 之前的保留段已收齐，插入新 chunk 后接 IEND 及其后
            if (newChunk.data.isEmpty())
                break;   // 仅删除：跳过插入
            kept.append(chunkBytes(newChunk));
            break;
        }
        if (!sameType)
            kept.append(png.constData() + i, total);
        i += total;
    }
    if (!iendFound)
        return png;   // 无 IEND（截断文件），原样

    return png.left(bodyStart) + kept + png.mid(i);
}

PngChunkIO::Chunk PngChunkIO::makePhys(double dpi)
{
    // pHYs：4 字节 x ppu + 4 字节 y ppu + 1 字节 unit(=1 米)
    const quint32 ppm = quint32(qRound(dpi / 0.0254));
    Chunk c;
    c.type = QByteArray("pHYs");
    c.data.reserve(9);
    appendU32(c.data, ppm);
    appendU32(c.data, ppm);
    c.data.append(char(1));
    return c;
}

PngChunkIO::Chunk PngChunkIO::makeExif(const QByteArray &tiff)
{
    Chunk c;
    c.type = QByteArray("eXIf");
    c.data = tiff;
    return c;
}

QByteArray PngChunkIO::chunkData(const QByteArray &png, const char type[4])
{
    const auto chunks = parse(png);
    for (const Chunk &c : chunks)
        if (c.type == QByteArray(type, 4))
            return c.data;
    return QByteArray();
}

} // namespace fxt::Platform
