#include "platform/MetadataMuxer.h"

#include <QtEndian>
#include <QtGlobal>

#include "platform/JpegSegmentIO.h"
#include "platform/PngChunkIO.h"

namespace fxt::Platform {

namespace {
// APP1(Exif) payload = "Exif\0\0" + TIFF；按签名识别，非 Exif 段（XMP 等）
// 返回空——评审 P0：XMP 的标准载体同为 APP1，"首个 APP1 即 Exif" 的假设
// 会把 XMP 字节包上 Exif 头写出（段损坏且真 Exif 丢失）
QByteArray tiffFromApp1Payload(const QByteArray &payload)
{
    if (payload.size() > 6 && payload.startsWith(QByteArray("Exif\0\0", 6)))
        return payload.mid(6);
    // 罕见无头写法：直接以 TIFF 魔数开头
    if (payload.size() >= 8
        && (payload.startsWith(QByteArray("II\x2a\x00", 4))
            || payload.startsWith(QByteArray("MM\x00\x2a", 4))))
        return payload;
    return QByteArray();
}

// XMP APP1 签名（"http://ns.adobe.com/xap/1.0/\0" + payload）
bool isXmpApp1(const QByteArray &payload)
{
    return payload.startsWith(
        QByteArray("http://ns.adobe.com/xap/1.0/\x00", 29));
}

QByteArray app1FromTiff(const QByteArray &tiff)
{
    return QByteArray("Exif\0\0", 6) + tiff;
}

// DPI：源 Exif → DPI（RATIONAL XResolution，按 ResolutionUnit 归一到英寸：
// 2=英寸原值、3=cm ×2.54）；解析失败回落 0
double dpiFromTiff(const QByteArray &tiff)
{
    // 轻量解析：直接在 IFD0 找 XResolution 与 ResolutionUnit。
    // 复用 ExifIfdEditor 的头解析逻辑过于内部——这里独立最小实现。
    if (tiff.size() < 8)
        return 0;
    const bool little = tiff.startsWith("II");
    if (!little && !tiff.startsWith("MM"))
        return 0;
    auto u32 = [&](int pos) -> quint32 {
        if (pos < 0 || qint64(pos) + 4 > qint64(tiff.size()))
            return 0;
        return little ? qFromLittleEndian<quint32>(tiff.constData() + pos)
                      : qFromBigEndian<quint32>(tiff.constData() + pos);
    };
    auto u16 = [&](int pos) -> quint32 {
        if (pos < 0 || qint64(pos) + 2 > qint64(tiff.size()))
            return 0;
        return little ? qFromLittleEndian<quint16>(tiff.constData() + pos)
                      : qFromBigEndian<quint16>(tiff.constData() + pos);
    };
    const quint32 ifd0 = u32(4);
    if (qint64(ifd0) + 2 > qint64(tiff.size()))
        return 0;
    const int count = int(u16(int(ifd0)));
    quint32 num = 0, den = 0;
    quint32 unit = 2;   // 缺省英寸（tag 缺失时 Exif 规范默认）
    for (int i = 0; i < count; ++i) {
        const int pos = int(ifd0) + 2 + i * 12;
        if (pos + 12 > tiff.size())
            break;
        const quint16 tag = u16(pos);
        if (tag == 0x011A) {   // XResolution
            const quint32 vOff = u32(pos + 8);
            if (qint64(vOff) + 8 > qint64(tiff.size()))
                return 0;
            num = u32(int(vOff));
            den = u32(int(vOff) + 4);
        } else if (tag == 0x0128) {   // ResolutionUnit
            unit = u32(pos + 8);
        }
    }
    if (den == 0)
        return 0;
    const double perUnit = double(num) / double(den);
    switch (unit) {
    case 2: return perUnit;             // 英寸
    case 3: return perUnit * 2.54;      // cm
    default: return 0;                  // 无单位：DPI 语义不成立
    }
}

// 覆写导出语义 tag：Orientation=1（画布已含转向，出图不再旋转）+ 输出尺寸 + DPI
QByteArray applyExportOverrides(const QByteArray &tiff, const ExportPolicy &policy)
{
    QByteArray out = ExifIfdEditor::setShort(tiff, ExifIfdEditor::Ifd::Ifd0,
                                             ExifIfdEditor::TagOrientation, 1);
    // SHORT 容不下 >65535 的全景尺寸——截断会写出错误值，不如不写（评审 P2）
    if (policy.outputSize.width() > 0 && policy.outputSize.height() > 0
        && policy.outputSize.width() <= 0xFFFF
        && policy.outputSize.height() <= 0xFFFF) {
        out = ExifIfdEditor::setShort(out, ExifIfdEditor::Ifd::Exif,
                                      ExifIfdEditor::TagPixelXDimension,
                                      quint16(policy.outputSize.width()));
        out = ExifIfdEditor::setShort(out, ExifIfdEditor::Ifd::Exif,
                                      ExifIfdEditor::TagPixelYDimension,
                                      quint16(policy.outputSize.height()));
    }
    if (policy.softwareTag) {
        // Software 为 ASCII 变长——仅当已存在时以同长度截断覆写（保真约束
        // 下不新增变长 tag；变长写入在重建路径处理）
        // v1：不覆写（保持源值），softwareTag 留给重建路径（挂账 M4）
    }
    // DPI（clamp 到 [1,10000]，防恶意 RATIONAL 回绕写怪值）
    double dpi = 0;
    if (policy.dpiMode == ExportPolicy::DpiMode::Fixed)
        dpi = policy.fixedDpi;
    else
        dpi = dpiFromTiff(out);
    if (dpi > 0) {
        dpi = qBound(1.0, dpi, 10000.0);
        out = ExifIfdEditor::setRational(out, ExifIfdEditor::Ifd::Ifd0,
                                         ExifIfdEditor::TagXResolution,
                                         quint32(qRound(dpi * 100)), 100);
        out = ExifIfdEditor::setRational(out, ExifIfdEditor::Ifd::Ifd0,
                                         ExifIfdEditor::TagYResolution,
                                         quint32(qRound(dpi * 100)), 100);
        out = ExifIfdEditor::setShort(out, ExifIfdEditor::Ifd::Ifd0,
                                      ExifIfdEditor::TagResolutionUnit, 2);   // 英寸
    }
    if (policy.stripGps)
        out = ExifIfdEditor::stripGps(out);
    return out;
}
} // namespace

QByteArray MetadataMuxer::exifTiffFromJpeg(const QByteArray &sourceJpeg)
{
    const auto segments = JpegSegmentIO::parse(sourceJpeg);
    for (const JpegSegment &seg : segments) {
        if (seg.marker != 0xE1)
            continue;
        const QByteArray tiff = tiffFromApp1Payload(seg.payload);
        if (tiff.size() > 8)
            return tiff;
    }
    return QByteArray();
}

QByteArray MetadataMuxer::composeJpeg(const QByteArray &encodedJpeg,
                                      const QByteArray &sourceJpeg,
                                      const ExportPolicy &policy)
{
    const auto segments = JpegSegmentIO::parse(sourceJpeg);
    QList<JpegSegment> keep;

    // Exif：APP1 按 "Exif\0\0"/TIFF 魔数签名识别（XMP APP1 不误判）
    QByteArray exifTiff;
    if (policy.keepExif) {
        for (const JpegSegment &seg : segments) {
            if (seg.marker != 0xE1)
                continue;
            const QByteArray tiff = tiffFromApp1Payload(seg.payload);
            if (tiff.size() > 8) {
                exifTiff = tiff;
                break;
            }
        }
        if (!exifTiff.isEmpty()) {
            const QByteArray edited = applyExportOverrides(exifTiff, policy);
            JpegSegment exifSeg;
            exifSeg.marker = 0xE1;
            exifSeg.payload = app1FromTiff(edited);
            keep.append(exifSeg);
        }
    }

    // ICC：APP2 按 "ICC_PROFILE\0" 签名识别，可能多段（按原序搬运）
    if (policy.keepIcc) {
        for (const JpegSegment &seg : segments) {
            if (seg.marker == 0xE2
                && seg.payload.startsWith(QByteArray("ICC_PROFILE\0", 12))) {
                keep.append(seg);
            }
        }
    }

    // IPTC/XMP：stripGps 时整段丢弃——XMP/IPTC 内常嵌 GPS 坐标，无通用
    // 方式安全局部剥离，去定位导出不得残留位置信息（评审 P0）
    if (policy.keepIptcXmp && !policy.stripGps) {
        for (const JpegSegment &seg : segments) {
            if (seg.marker == 0xED)
                keep.append(seg);
            else if (seg.marker == 0xE1 && isXmpApp1(seg.payload))
                keep.append(seg);   // XMP APP1 原样搬运
        }
    }

    // COM 注释段：随 Exif 策略搬运；stripGps 时同弃（注释可能含位置描述）
    if (policy.keepExif && !policy.stripGps) {
        for (const JpegSegment &seg : segments)
            if (seg.marker == 0xFE)
                keep.append(seg);
    }

    return JpegSegmentIO::splice(encodedJpeg, keep);
}

QByteArray MetadataMuxer::composeJpegFromExif(const QByteArray &encodedJpeg,
                                              const QByteArray &exifTiff,
                                              const QByteArray &iccProfile,
                                              const ExportPolicy &policy)
{
    QList<JpegSegment> keep;
    if (policy.keepExif && !exifTiff.isEmpty()) {
        JpegSegment exifSeg;
        exifSeg.marker = 0xE1;
        exifSeg.payload = app1FromTiff(applyExportOverrides(exifTiff, policy));
        keep.append(exifSeg);
    }
    if (policy.keepIcc && !iccProfile.isEmpty()) {
        // ICC_PROFILE 规范分段（12 签名 + seqno + total + 2 保留 + 数据）：
        // 单段数据上限 0xFFFF-2-16=65517；>64KB 的相机/广色域 profile 分段
        // 搬运，不再静默丢弃（评审 P1：丢 ICC 会导出色彩漂移）
        constexpr int kChunkData = 0xFFFF - 2 - 16;
        const int totalSegments = (iccProfile.size() + kChunkData - 1) / kChunkData;
        if (totalSegments <= 255) {   // seqno/total 各 1 字节
            for (int i = 0; i < totalSegments; ++i) {
                QByteArray payload = QByteArray("ICC_PROFILE\0", 12);
                payload.append(char(i + 1));      // seqno 从 1 起
                payload.append(char(totalSegments));
                payload.append(char(0));
                payload.append(char(0));
                payload.append(iccProfile.mid(i * kChunkData, kChunkData));
                JpegSegment iccSeg;
                iccSeg.marker = 0xE2;
                iccSeg.payload = payload;
                keep.append(iccSeg);
            }
        }
    }
    return JpegSegmentIO::splice(encodedJpeg, keep);
}

QByteArray MetadataMuxer::composePng(const QByteArray &encodedPng,
                                     const QByteArray &sourcePng,
                                     const ExportPolicy &policy)
{
    QByteArray out = encodedPng;
    if (!PngChunkIO::isValidPng(out))
        return out;

    // eXIf：从源搬运并套导出语义覆写（Orientation=1 + 输出尺寸 + DPI +
    // GPS 摘除——像素已转正/重采样，元数据必须与新画布一致）
    if (policy.keepExif) {
        const QByteArray tiff = PngChunkIO::chunkData(sourcePng, "eXIf");
        if (!tiff.isEmpty())
            out = PngChunkIO::setChunk(
                out, PngChunkIO::makeExif(applyExportOverrides(tiff, policy)));
    }

    // iCCP：原样拷贝（压缩字节不动）；stripGps 时同样不带入（iCCP 与
    // 位置无关，仅按 keepIcc 策略）
    if (policy.keepIcc) {
        const QByteArray iccp = PngChunkIO::chunkData(sourcePng, "iCCP");
        if (!iccp.isEmpty()) {
            PngChunkIO::Chunk c;
            c.type = QByteArray("iCCP");
            c.data = iccp;
            out = PngChunkIO::setChunk(out, c);
        }
    }

    // pHYs：Fixed=固定值；Source=源 pHYs 优先、回落源 eXIf XResolution、
    // 最终缺省 300（"Source" 语义是忠实源文件，而非固定 300）
    double dpi = 0;
    if (policy.dpiMode == ExportPolicy::DpiMode::Source) {
        const QByteArray phys = PngChunkIO::chunkData(sourcePng, "pHYs");
        if (phys.size() == 9) {
            const quint32 ppm = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(phys.constData()));
            if (phys.at(8) == 1 && ppm > 0)
                dpi = ppm * 0.0254;
        }
        if (dpi <= 0) {
            const QByteArray tiff = PngChunkIO::chunkData(sourcePng, "eXIf");
            if (!tiff.isEmpty())
                dpi = dpiFromTiff(tiff);
        }
    } else {
        dpi = policy.fixedDpi;
    }
    if (dpi <= 0)
        dpi = 300;
    dpi = qBound(1.0, dpi, 10000.0);
    out = PngChunkIO::setChunk(out, PngChunkIO::makePhys(dpi));
    return out;
}

} // namespace fxt::Platform
