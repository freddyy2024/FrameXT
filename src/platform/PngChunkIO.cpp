#include "platform/PngChunkIO.h"

#include <QtEndian>
#include <QtGlobal>

#include <array>

namespace fxt::Platform {

namespace {
constexpr int kSignatureSize = 8;
const char kPngSignature[kSignatureSize] = {
    char(0x89), 'P', 'N', 'G', char(0x0D), char(0x0A), char(0x1A), char(0x0A)
};

// PNG 标准_CRC-32（ISO 3309，zlib 多项式 0xEDB88320 反射式）。
// 查表用 magic static 一次性初始化——函数级 static 的惰性标志位在并发
// 首调下有数据竞争（M4 QtConcurrent 并行导出场景，评审 P1）
quint32 crc32Of(const QByteArray &data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> t = {};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            t[i] = c;
        }
        return t;
    }();
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

    const int n = png.size();
    // 预扫描：确认 IEND 存在且结构完整（任一 chunk 越界即畸形原样返回）
    bool iendFound = false;
    {
        int i = kSignatureSize;
        while (i + 12 <= n) {
            const quint32 length = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(png.constData() + i));
            const QByteArray type = png.mid(i + 4, 4);
            if (length > quint32(n - i - 12) || type.size() != 4)
                return png;
            if (type == QByteArray("IEND")) {
                iendFound = true;
                break;
            }
            i += 12 + int(length);
        }
    }
    if (!iendFound)
        return png;   // 无 IEND（截断文件），原样

    // 重建：同类型 chunk 剔除（全段范围），新 chunk 插到首个 IDAT 之前——
    // PNG 规范要求 eXIf/iCCP/pHYs 先于 IDAT，libpng 对 IDAT 之后的辅助
    // chunk 报 benign error 并忽略（评审 P1：原实现插在 IEND 前导致
    // DPI/ICC 写了但读取端静默丢失）
    if (newChunk.type.isEmpty())
        return png;   // 无类型 chunk 无意义
    const bool insert = !newChunk.data.isEmpty();   // data 空 = 仅删除同类型
    bool inserted = !insert;
    QByteArray out;
    out.append(png.constData(), kSignatureSize);
    int i = kSignatureSize;
    while (i + 12 <= n) {
        const quint32 length = qFromBigEndian<quint32>(
            reinterpret_cast<const uchar *>(png.constData() + i));
        const QByteArray type = png.mid(i + 4, 4);
        const int total = 12 + int(length);
        const bool drop = !newChunk.type.isEmpty() && type == newChunk.type;
        if (!inserted && type == QByteArray("IDAT")) {
            out.append(chunkBytes(newChunk));
            inserted = true;
        }
        if (!drop)
            out.append(png.constData() + i, total);
        if (type == QByteArray("IEND"))
            break;
        i += total;
    }
    // 无 IDAT 的极端文件（空画布 PNG）：退到 IEND 前插入，保证不丢
    if (!inserted)
        out.insert(out.size() - 12, chunkBytes(newChunk));
    return out;
}

PngChunkIO::Chunk PngChunkIO::makePhys(double dpi)
{
    // pHYs：4 字节 x ppu + 4 字节 y ppu + 1 字节 unit(=1 米)；
    // dpi clamp 到 [1,10000]（0/负值经 quint32 转换回绕成怪值）
    const quint32 ppm = quint32(qRound(qBound(1.0, dpi, 10000.0) / 0.0254));
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
