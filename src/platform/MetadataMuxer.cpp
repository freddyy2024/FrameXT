#include "platform/MetadataMuxer.h"

#include <QtEndian>
#include <QtGlobal>

#include "platform/JpegSegmentIO.h"
#include "platform/PngChunkIO.h"

namespace fxt::Platform {

namespace {
// APP1(Exif) payload = "Exif\0\0" + TIFF；分离返回裸 TIFF
QByteArray tiffFromApp1Payload(const QByteArray &payload)
{
    if (payload.size() > 6 && payload.startsWith(QByteArray("Exif\0\0", 6)))
        return payload.mid(6);
    return payload;   // 无头（罕见写法）按裸 TIFF 处理
}

QByteArray app1FromTiff(const QByteArray &tiff)
{
    return QByteArray("Exif\0\0", 6) + tiff;
}

// DPI：源 Exif → DPI（RATIONAL，ResolutionUnit=2 英寸）；解析失败回落 0
double dpiFromTiff(const QByteArray &tiff)
{
    // 轻量解析：直接在 IFD0 找 XResolution 的 RATIONAL 值。
    // 复用 ExifIfdEditor 的头解析逻辑过于内部——这里独立最小实现。
    if (tiff.size() < 8)
        return 0;
    const bool little = tiff.startsWith("II");
    if (!little && !tiff.startsWith("MM"))
        return 0;
    auto u32 = [&](int pos) -> quint32 {
        if (pos + 4 > tiff.size())
            return 0;
        return little ? qFromLittleEndian<quint32>(tiff.constData() + pos)
                      : qFromBigEndian<quint32>(tiff.constData() + pos);
    };
    auto u16 = [&](int pos) -> quint32 {
        if (pos + 2 > tiff.size())
            return 0;
        return little ? qFromLittleEndian<quint16>(tiff.constData() + pos)
                      : qFromBigEndian<quint16>(tiff.constData() + pos);
    };
    const quint32 ifd0 = u32(4);
    if (ifd0 + 2 > quint32(tiff.size()))
        return 0;
    const int count = int(u16(int(ifd0)));
    for (int i = 0; i < count; ++i) {
        const int pos = int(ifd0) + 2 + i * 12;
        if (pos + 12 > tiff.size())
            return 0;
        if (u16(pos) == 0x011A) {   // XResolution
            const quint32 vOff = u32(pos + 8);
            if (vOff + 8 > quint32(tiff.size()))
                return 0;
            const quint32 num = u32(int(vOff));
            const quint32 den = u32(int(vOff) + 4);
            if (den == 0)
                return 0;
            return double(num) / double(den);
        }
    }
    return 0;
}

// 覆写导出语义 tag：Orientation=1（画布已含转向，出图不再旋转）+ 输出尺寸 + DPI
QByteArray applyExportOverrides(const QByteArray &tiff, const ExportPolicy &policy)
{
    QByteArray out = ExifIfdEditor::setShort(tiff, ExifIfdEditor::Ifd::Ifd0,
                                             ExifIfdEditor::TagOrientation, 1);
    if (policy.outputSize.width() > 0) {
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
        // v1：不覆写（保持源值），softwareTag 留给重建路径
    }
    // DPI
    double dpi = 0;
    if (policy.dpiMode == ExportPolicy::DpiMode::Fixed)
        dpi = policy.fixedDpi;
    else
        dpi = dpiFromTiff(out);
    if (dpi > 0) {
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
    for (const JpegSegment &seg : segments)
        if (seg.marker == 0xE1 && tiffFromApp1Payload(seg.payload).size() > 8)
            return tiffFromApp1Payload(seg.payload);
    return QByteArray();
}

QByteArray MetadataMuxer::composeJpeg(const QByteArray &encodedJpeg,
                                      const QByteArray &sourceJpeg,
                                      const ExportPolicy &policy)
{
    const auto segments = JpegSegmentIO::parse(sourceJpeg);
    QList<JpegSegment> keep;

    QByteArray exifTiff;
    if (policy.keepExif) {
        for (const JpegSegment &seg : segments) {
            if (seg.marker == 0xE1) {
                exifTiff = tiffFromApp1Payload(seg.payload);
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

    // IPTC/XMP：APP13 原样
    if (policy.keepIptcXmp) {
        for (const JpegSegment &seg : segments)
            if (seg.marker == 0xED)
                keep.append(seg);
    }

    // COM 注释段：随 Exif 策略搬运（版权/说明类，用户可见信息）
    if (policy.keepExif) {
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
    if (policy.keepIcc && !iccProfile.isEmpty() && iccProfile.size() <= 0xFFFF - 31) {
        // 单段 ICC_PROFILE（31 字节头内：12 签名 + 4 seqno + 4 total——
        // 超限 ICC 分段在 M4 导出控制器按 64KB 分块时扩展）
        QByteArray payload = QByteArray("ICC_PROFILE\0", 12);
        payload.append(char(0));   // seqno = 1
        payload.append(char(1));   // total = 1
        payload.append(char(0));
        payload.append(char(0));
        payload.append(iccProfile);
        JpegSegment iccSeg;
        iccSeg.marker = 0xE2;
        iccSeg.payload = payload;
        keep.append(iccSeg);
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

    // eXIf：从源搬运（摘 GPS 后回填）
    if (policy.keepExif) {
        QByteArray tiff = PngChunkIO::chunkData(sourcePng, "eXIf");
        if (!tiff.isEmpty()) {
            if (policy.stripGps)
                tiff = ExifIfdEditor::stripGps(tiff);
            out = PngChunkIO::setChunk(out, PngChunkIO::makeExif(tiff));
        }
    }

    // iCCP：原样拷贝（压缩字节不动）
    if (policy.keepIcc) {
        const QByteArray iccp = PngChunkIO::chunkData(sourcePng, "iCCP");
        if (!iccp.isEmpty()) {
            PngChunkIO::Chunk c;
            c.type = QByteArray("iCCP");
            c.data = iccp;
            out = PngChunkIO::setChunk(out, c);
        }
    }

    // pHYs：策略 DPI（Source 模式从源 pHYs 读，缺省 300）
    double dpi = 300;
    if (policy.dpiMode == ExportPolicy::DpiMode::Source) {
        const QByteArray phys = PngChunkIO::chunkData(sourcePng, "pHYs");
        if (phys.size() == 9) {
            const quint32 ppm = qFromBigEndian<quint32>(
                reinterpret_cast<const uchar *>(phys.constData()));
            if (phys.at(8) == 1 && ppm > 0)
                dpi = ppm * 0.0254;
        }
    } else {
        dpi = policy.fixedDpi;
    }
    out = PngChunkIO::setChunk(out, PngChunkIO::makePhys(dpi));
    return out;
}

} // namespace fxt::Platform
