#include "platform/JpegSegmentIO.h"

namespace fxt::Platform {

namespace {
// 段类型判定（只搬运元数据段；SOF/DHT/DQT/RST 等编码器自管）
bool isMetadataMarker(quint8 m)
{
    return (m >= 0xE0 && m <= 0xEF) || m == 0xFE;   // APPn / COM
}
} // namespace

QList<JpegSegment> JpegSegmentIO::parse(const QByteArray &jpeg)
{
    QList<JpegSegment> out;
    // SOI 签名（FF D8）——非 JPEG 直接返回空
    if (jpeg.size() < 4 || quint8(jpeg.at(0)) != 0xFF || quint8(jpeg.at(1)) != 0xD8)
        return out;

    int i = 2;
    while (i + 4 <= jpeg.size()) {
        // 段前填充 FF（部分编码器输出多个 FF）
        if (quint8(jpeg.at(i)) != 0xFF) {
            if (quint8(jpeg.at(i)) == 0x00) {   // 异常：段内出现 00，数据已损坏
                ++i;
                continue;
            }
            break;
        }
        const quint8 marker = quint8(jpeg.at(i + 1));
        if (marker == 0xD9)   // EOI：无元数据段可再收
            break;
        if (marker == 0xDA) {   // SOS：其后为压缩数据，元数据段收集结束
            break;
        }
        // 无长度段（DQT/DHT/RST 等不在此处理，SOF 族有长度但我们不收集）
        const bool hasLength = !(marker >= 0xD0 && marker <= 0xD7) && marker != 0x01;
        if (!hasLength) {
            i += 2;
            continue;
        }
        if (i + 4 > jpeg.size())
            break;
        const int length = (quint8(jpeg.at(i + 2)) << 8) | quint8(jpeg.at(i + 3));
        // 坏长度（<2 或越界）：数据畸形，丢弃剩余收集（已有段仍有效）
        if (length < 2 || i + 2 + length > jpeg.size())
            break;
        if (isMetadataMarker(marker)) {
            JpegSegment seg;
            seg.marker = marker;
            seg.payload = jpeg.mid(i + 4, length - 2);
            out.append(seg);
        }
        i += 2 + length;
    }
    return out;
}

QByteArray JpegSegmentIO::splice(const QByteArray &encodedImage,
                                 const QList<JpegSegment> &metadataSegments)
{
    if (encodedImage.size() < 4 || quint8(encodedImage.at(0)) != 0xFF
        || quint8(encodedImage.at(1)) != 0xD8)
        return encodedImage;

    // 找 SOS 位置：其前的编码器自带 APPn/COM 全部丢弃（§4.8 防重复段），
    // 保留 SOI 与编码链必需段（DQT/SOF/DHT 等）
    QByteArray head;
    head.append(encodedImage.constData(), 2);   // SOI

    int i = 2;
    const int n = encodedImage.size();
    while (i + 4 <= n) {
        if (quint8(encodedImage.at(i)) != 0xFF)
            break;
        const quint8 marker = quint8(encodedImage.at(i + 1));
        if (marker == 0x01 || (marker >= 0xD0 && marker <= 0xD7)) {
            i += 2;   // 无长度段
            continue;
        }
        if (marker == 0xDA || marker == 0xD9)
            break;
        const int length = (quint8(encodedImage.at(i + 2)) << 8)
            | quint8(encodedImage.at(i + 3));
        if (length < 2 || i + 2 + length > n)
            break;
        if (!isMetadataMarker(marker))
            head.append(encodedImage.constData() + i, 2 + length);
        i += 2 + length;
    }

    QByteArray out = head;
    for (const JpegSegment &seg : metadataSegments) {
        const int payloadLen = seg.payload.size();
        if (payloadLen > 0xFFFF - 2)   // 段长上限 65533 字节
            continue;
        out.append(char(0xFF));
        out.append(char(seg.marker));
        const int length = payloadLen + 2;
        out.append(char((length >> 8) & 0xFF));
        out.append(char(length & 0xFF));
        out.append(seg.payload);
    }
    out.append(encodedImage.constData() + i, n - i);   // SOS 起的压缩数据原样接上
    return out;
}

} // namespace fxt::Platform
