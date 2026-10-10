// M2.3 platform 层单测：JpegSegmentIO / ExifIfdEditor / PngChunkIO / MetadataMuxer
// 关键准入（G2）：畸形样本不崩；GPS 摘除生效；段搬运字节级保真
#include <QByteArray>
#include <QDebug>
#include <QtTest>

#include "platform/ExifIfdEditor.h"
#include "platform/JpegSegmentIO.h"
#include "platform/MetadataMuxer.h"
#include "platform/PngChunkIO.h"

using namespace fxt::Platform;

namespace {

quint16 rd16(const QByteArray &d, int pos)
{
    return quint16(uchar(d.at(pos)) | (uchar(d.at(pos + 1)) << 8));
}
quint32 rd32(const QByteArray &d, int pos)
{
    return quint32(uchar(d.at(pos)) | (uchar(d.at(pos + 1)) << 8)
                   | (uchar(d.at(pos + 2)) << 16) | (uchar(d.at(pos + 3)) << 24));
}
void wr16(QByteArray &d, int pos, quint16 v)
{
    d[pos] = char(v & 0xFF);
    d[pos + 1] = char((v >> 8) & 0xFF);
}
void wr32(QByteArray &d, int pos, quint32 v)
{
    d[pos] = char(v & 0xFF);
    d[pos + 1] = char((v >> 8) & 0xFF);
    d[pos + 2] = char((v >> 16) & 0xFF);
    d[pos + 3] = char((v >> 24) & 0xFF);
}
quint16 rd16be(const QByteArray &d, int pos)
{
    return quint16((uchar(d.at(pos)) << 8) | uchar(d.at(pos + 1)));
}
quint32 rd32be(const QByteArray &d, int pos)
{
    return (quint32(uchar(d.at(pos))) << 24) | (quint32(uchar(d.at(pos + 1))) << 16)
        | (quint32(uchar(d.at(pos + 2))) << 8) | quint32(uchar(d.at(pos + 3)));
}
void wr16be(QByteArray &d, int pos, quint16 v)
{
    d[pos] = char((v >> 8) & 0xFF);
    d[pos + 1] = char(v & 0xFF);
}
void wr32be(QByteArray &d, int pos, quint32 v)
{
    d[pos] = char((v >> 24) & 0xFF);
    d[pos + 1] = char((v >> 16) & 0xFF);
    d[pos + 2] = char((v >> 8) & 0xFF);
    d[pos + 3] = char(v & 0xFF);
}

// —— 手工构造最小 Exif TIFF（II 小端）——
// IFD0: Orientation(SHORT)=6 / XResolution(RATIONAL)=72/1 / GPS 指针(LONG)
// 数据区: 72/1 + GPS IFD(含 1 个 entry)
QByteArray makeSampleTiff()
{
    const int ifd0 = 8;
    const int entries = 3;
    const int ifdSize = 2 + entries * 12 + 4;
    const int xresVal = ifd0 + ifdSize;         // 8 字节
    const int gpsIfd = xresVal + 8;             // GPS IFD：count(2)+entry(12)+next(4)

    QByteArray t;
    t.append("II");
    t.resize(8);             // 先扩容再写（wr16/wr32 无越界防御）
    wr16(t, 2, 42);          // TIFF 魔数
    wr32(t, 4, quint32(ifd0));

    t.resize(ifd0 + ifdSize);
    wr16(t, ifd0, quint16(entries));
    // entry1：Orientation SHORT=6
    wr16(t, ifd0 + 2, 0x0112);
    wr16(t, ifd0 + 4, 3);
    wr32(t, ifd0 + 6, 1);
    wr16(t, ifd0 + 10, 6);
    wr16(t, ifd0 + 12, 0);
    // entry2：XResolution RATIONAL → 数据区
    wr16(t, ifd0 + 14, 0x011A);
    wr16(t, ifd0 + 16, 5);
    wr32(t, ifd0 + 18, 1);
    wr32(t, ifd0 + 22, quint32(xresVal));
    // entry3：GPSInfo 指针 LONG → GPS IFD
    wr16(t, ifd0 + 26, 0x8825);
    wr16(t, ifd0 + 28, 4);
    wr32(t, ifd0 + 30, 1);
    wr32(t, ifd0 + 34, quint32(gpsIfd));
    wr32(t, ifd0 + 38, 0);   // next IFD = 0

    t.resize(gpsIfd + 18);   // GPS IFD：count(2) + entry(12) + next(4)
    wr32(t, xresVal, 72);
    wr32(t, xresVal + 4, 1);
    // GPS IFD：count=1，GPSLatitudeRef ASCII "N\0" 内联，next=0
    wr16(t, gpsIfd, 1);
    wr16(t, gpsIfd + 2, 0x0001);
    wr16(t, gpsIfd + 4, 2);
    wr32(t, gpsIfd + 6, 2);
    t[gpsIfd + 10] = 'N';
    t[gpsIfd + 11] = '\0';
    wr32(t, gpsIfd + 14, 0);
    return t;
}

// —— 富样例 TIFF（II 小端，评审 P0 回归专用）——
// IFD0: Orientation=6 / XRes=72/1 / 0x8769 ExifIFD / 0x8825 GPS 指针
// ExifIFD: ExposureTime=1/160 / PixelXDimension / PixelYDimension（SHORT 内联）
// GPS IFD: GPSLatitudeRef "N" 内联 + GPSLatitude=25°2'6"（数据区 RATIONAL×3）
// 布局（绝对 offset）：header 0-8 | IFD0 8-62 | xresVal 62 | GPS IFD 70-100
// | GPS 数据 100-124 | ExifIFD 124-166 | 曝光数据 166-174，总长 174
QByteArray makeRichTiff()
{
    const int ifd0 = 8;
    const int xresVal = 62;
    const int gpsIfd = 70;
    const int gpsData = 100;
    const int exifIfd = 124;
    const int exifData = 166;

    QByteArray t;
    t.append("II");
    t.resize(8);
    wr16(t, 2, 42);
    wr32(t, 4, quint32(ifd0));
    t.resize(exifData + 8);

    // IFD0：count=4
    wr16(t, ifd0, 4);
    wr16(t, ifd0 + 2, 0x0112);       // Orientation SHORT=6
    wr16(t, ifd0 + 4, 3);
    wr32(t, ifd0 + 6, 1);
    wr16(t, ifd0 + 10, 6);
    wr16(t, ifd0 + 12, 0);
    wr16(t, ifd0 + 14, 0x011A);      // XResolution RATIONAL → 数据区
    wr16(t, ifd0 + 16, 5);
    wr32(t, ifd0 + 18, 1);
    wr32(t, ifd0 + 22, quint32(xresVal));
    wr16(t, ifd0 + 26, 0x8769);      // ExifIFD 指针 LONG
    wr16(t, ifd0 + 28, 4);
    wr32(t, ifd0 + 30, 1);
    wr32(t, ifd0 + 34, quint32(exifIfd));
    wr16(t, ifd0 + 38, 0x8825);      // GPS 指针 LONG（strip 目标）
    wr16(t, ifd0 + 40, 4);
    wr32(t, ifd0 + 42, 1);
    wr32(t, ifd0 + 46, quint32(gpsIfd));
    wr32(t, ifd0 + 50, 0);           // next IFD = 0

    wr32(t, xresVal, 72);
    wr32(t, xresVal + 4, 1);

    // GPS IFD：count=2（e0 = 72..84 含 4 字节 value 字段，e1 从 84 起）
    wr16(t, gpsIfd, 2);
    wr16(t, gpsIfd + 2, 0x0001);     // GPSLatitudeRef ASCII "N\0" 内联
    wr16(t, gpsIfd + 4, 2);
    wr32(t, gpsIfd + 6, 2);
    t[gpsIfd + 10] = 'N';
    t[gpsIfd + 11] = '\0';
    wr16(t, gpsIfd + 14, 0x0002);    // e1@84：GPSLatitude RATIONAL×3 → 数据区
    wr16(t, gpsIfd + 16, 5);
    wr32(t, gpsIfd + 18, 3);
    wr32(t, gpsIfd + 22, quint32(gpsData));
    wr32(t, gpsIfd + 26, 0);
    wr32(t, gpsData, 25);
    wr32(t, gpsData + 4, 1);
    wr32(t, gpsData + 8, 2);
    wr32(t, gpsData + 12, 1);
    wr32(t, gpsData + 16, 6);
    wr32(t, gpsData + 20, 10);

    // ExifIFD：count=3
    wr16(t, exifIfd, 3);
    wr16(t, exifIfd + 2, 0x829A);    // ExposureTime RATIONAL → 数据区
    wr16(t, exifIfd + 4, 5);
    wr32(t, exifIfd + 6, 1);
    wr32(t, exifIfd + 10, quint32(exifData));
    wr16(t, exifIfd + 14, 0xA002);   // PixelXDimension SHORT 内联
    wr16(t, exifIfd + 16, 3);
    wr32(t, exifIfd + 18, 1);
    wr16(t, exifIfd + 22, 0);        // e1 value 低 2B（值由 setShort 覆写）
    wr16(t, exifIfd + 24, 0);        // e1 value 高 2B（padding，必须写零）
    wr16(t, exifIfd + 26, 0xA003);   // e2@150：PixelYDimension SHORT 内联
    wr16(t, exifIfd + 28, 3);
    wr32(t, exifIfd + 30, 1);
    wr16(t, exifIfd + 32, 0);        // e2 value @158
    wr16(t, exifIfd + 34, 0);
    wr32(t, exifIfd + 38, 0);
    wr32(t, exifData, 1);
    wr32(t, exifData + 4, 160);
    return t;
}

QByteArray makeApp1Payload(const QByteArray &tiff)
{
    return QByteArray("Exif\0\0", 6) + tiff;
}

// —— 手工构造最小 JPEG ——
QByteArray makeSampleJpeg(const QList<QPair<quint8, QByteArray>> &metaSegments)
{
    QByteArray j;
    j.append(char(0xFF));
    j.append(char(0xD8));   // SOI
    for (const auto &seg : metaSegments) {
        j.append(char(0xFF));
        j.append(char(seg.first));
        const int len = seg.second.size() + 2;
        j.append(char((len >> 8) & 0xFF));
        j.append(char(len & 0xFF));
        j.append(seg.second);
    }
    // DQT 假段 + SOS + 压缩数据 + EOI
    QByteArray dqt(10, char(0x11));
    j.append(char(0xFF));
    j.append(char(0xDB));
    j.append(char(0x00));
    j.append(char(0x0C));
    j.append(dqt);
    j.append(char(0xFF));
    j.append(char(0xDA));   // SOS
    j.append(char(0x00));
    j.append(char(0x04));
    j.append(char(0x01));
    j.append(char(0x02));
    j.append(char(0xAB));
    j.append(char(0xCD));   // 压缩数据
    j.append(char(0xFF));
    j.append(char(0xD9));   // EOI
    return j;
}

// —— 手工构造最小 PNG ——
QByteArray makeSamplePng(const QList<QPair<QByteArray, QByteArray>> &chunks)
{
    QByteArray p;
    p.append(char(0x89));
    p.append("PNG\r\n\032\n");
    auto appendChunk = [&p](const QByteArray &type, const QByteArray &data) {
        quint32 len = quint32(data.size());
        uchar lb[4];
        for (int i = 0; i < 4; ++i)
            lb[i] = uchar((len >> (24 - i * 8)) & 0xFF);
        p.append(reinterpret_cast<const char *>(lb), 4);
        p.append(type);
        p.append(data);
        // CRC 略（本库 parse 不校验 CRC；setChunk 生成端正确性由其单测自证）
        p.append(QByteArray(4, char(0)));
    };
    appendChunk("IHDR", QByteArray(13, char(0)));
    for (const auto &c : chunks)
        appendChunk(c.first, c.second);
    appendChunk("IDAT", QByteArray(4, char(0xAA)));
    appendChunk("IEND", QByteArray());
    return p;
}

} // namespace

class TestMetadata : public QObject
{
    Q_OBJECT

private slots:
    void jpegParseRoundTrip() const;
    void jpegSpliceDropsEncoderMetadata() const;
    void jpegMalformedInputNoCrash() const;
    void exifSetShortOverridesInPlace() const;
    void exifSetRationalOverrides() const;
    void exifStripGpsRemovesPointer() const;
    void exifMalformedTiffNoCrash() const;
    void pngParseAndSetChunk() const;
    void pngPhysValue() const;
    void muxerJpegTransplant() const;
    void muxerStripGpsPolicy() const;
    void muxerJpegWithoutExifWritesNoApp1() const;
    void muxerPngCompose() const;
    // —— 评审回归（M2 评审 P0/P1 修复的防回归锁定）——
    void exifStripGpsKeepsExifIfdReadable() const;
    void exifStripGpsClearsGpsBytes() const;
    void exifSetRationalEvilOffsetNoCrash() const;
    void exifBigEndianPositivePath() const;
    void jpegSpliceKeepsApp0() const;
    void jpegFuzzHeavyDuty() const;
    void muxerXmpApp1NotMisdetected() const;
    void muxerComposeFromExifFullChain() const;
    void muxerExportPolicyNegative() const;
    void pngChunkOrderBeforeIdat() const;
    void pngCrcGeneratedMatchesZlib() const;
};

void TestMetadata::jpegParseRoundTrip() const
{
    qDebug() << "step1: make tiff";
    const QByteArray tiff = makeSampleTiff();
    qDebug() << "tiff size" << tiff.size();
    const QByteArray app1 = makeApp1Payload(tiff);
    qDebug() << "step2: make jpeg, app1 size" << app1.size();
    const QByteArray jpeg = makeSampleJpeg({ { 0xE1, app1 },
                                             { 0xFE, QByteArray("comment") },
                                             { 0xE2, QByteArray("ICC_PROFILE\0", 12) + "xx" } });
    qDebug() << "step3: jpeg size" << jpeg.size();
    const auto segments = JpegSegmentIO::parse(jpeg);
    qDebug() << "step4: parsed" << segments.size();
    QCOMPARE(segments.size(), 3);
    QCOMPARE(segments[0].marker, quint8(0xE1));
    QCOMPARE(segments[0].payload, app1);
    QCOMPARE(segments[1].marker, quint8(0xFE));
    QCOMPARE(segments[2].marker, quint8(0xE2));
}

void TestMetadata::jpegSpliceDropsEncoderMetadata() const
{
    // 编码器自带的 APP1 必须被丢弃（防重复段），元数据段插到 SOS 前
    const QByteArray encoderApp1("old-exif");
    const QByteArray encoded = makeSampleJpeg({ { 0xE1, encoderApp1 } });
    const QByteArray app1 = makeApp1Payload(makeSampleTiff());

    QList<JpegSegment> meta;
    JpegSegment seg;
    seg.marker = 0xE1;
    seg.payload = app1;
    meta.append(seg);

    const QByteArray out = JpegSegmentIO::splice(encoded, meta);
    const auto parsed = JpegSegmentIO::parse(out);
    QCOMPARE(parsed.size(), 1);
    QCOMPARE(parsed[0].payload, app1);
    QVERIFY(!out.contains(encoderApp1));
    // 压缩数据与 EOI 仍在
    QVERIFY(out.contains(QByteArrayLiteral("\xFF\xDA\x00\x04")));
    QVERIFY(out.endsWith(QByteArrayLiteral("\xFF\xD9")));
}

void TestMetadata::jpegMalformedInputNoCrash() const
{
    // fuzz 准入（G2）：各种畸形输入不得崩溃，parse 返回空/部分结果
    QVERIFY(JpegSegmentIO::parse(QByteArray()).isEmpty());
    QVERIFY(JpegSegmentIO::parse(QByteArray("not a jpeg")).isEmpty());
    QVERIFY(JpegSegmentIO::parse(QByteArrayLiteral("\xFF\xD8\xFF\xE1\xFF"))
            .isEmpty());   // 声明长度越界
    // 坏长度段后已有段仍有效
    const QByteArray good = makeApp1Payload("ok");
    QByteArray mixed;
    mixed.append(char(0xFF));
    mixed.append(char(0xD8));
    mixed.append(char(0xFF));
    mixed.append(char(0xE1));
    mixed.append(char(0x00));
    mixed.append(char(0x04));
    mixed.append(good);
    mixed.append(char(0xFF));
    mixed.append(char(0xE0));
    mixed.append(char(0xFF));   // 坏长度（高字节 0xFF 越界）
    const auto segments = JpegSegmentIO::parse(mixed);
    QCOMPARE(segments.size(), 1);
}

void TestMetadata::exifSetShortOverridesInPlace() const
{
    const QByteArray tiff = makeSampleTiff();
    const QByteArray out = ExifIfdEditor::setShort(tiff, ExifIfdEditor::Ifd::Ifd0,
                                                   ExifIfdEditor::TagOrientation, 1);
    QCOMPARE(out.size(), tiff.size());   // in-place：长度不变（保真红线）
    // Orientation entry 的 value 字段（IFD0+10，2 字节）已改为 1
    QCOMPARE(rd16(out, 8 + 2 + 8), quint16(1));
    // 其他字节（XResolution offset 等）不动
    QCOMPARE(rd32(out, 8 + 2 + 12 + 6), rd32(tiff, 8 + 2 + 12 + 6));
    // 非 SHORT 类型 / 不存在的 tag：原样
    QCOMPARE(ExifIfdEditor::setShort(tiff, ExifIfdEditor::Ifd::Ifd0, 0x9999, 7), tiff);
}

void TestMetadata::exifSetRationalOverrides() const
{
    const QByteArray tiff = makeSampleTiff();
    const QByteArray out = ExifIfdEditor::setRational(tiff, ExifIfdEditor::Ifd::Ifd0,
                                                      ExifIfdEditor::TagXResolution,
                                                      30000, 100);
    QCOMPARE(out.size(), tiff.size());
    // 数据区（offset 50，即 entry valueOffset 指向处）72/1 → 30000/100
    QCOMPARE(rd32(out, 50), quint32(30000));
    QCOMPARE(rd32(out, 54), quint32(100));
}

void TestMetadata::exifStripGpsRemovesPointer() const
{
    const QByteArray tiff = makeSampleTiff();
    const QByteArray out = ExifIfdEditor::stripGps(tiff);
    QCOMPARE(out.size(), tiff.size());   // 长度不变（尾部补零）
    // IFD0 entry count 3 → 2
    QCOMPARE(rd16(out, 8), quint16(2));
    // 遍历剩余 entry：无 0x8825；Orientation 仍在
    bool hasGps = false;
    bool hasOrient = false;
    for (int i = 0; i < 2; ++i) {
        const quint16 tag = rd16(out, 8 + 2 + i * 12);
        if (tag == 0x8825)
            hasGps = true;
        if (tag == 0x0112)
            hasOrient = true;
    }
    QVERIFY(!hasGps);
    QVERIFY(hasOrient);
    // 无 GPS 的图原样返回
    const QByteArray noGps = ExifIfdEditor::stripGps(out);
    QCOMPARE(noGps, out);
}

void TestMetadata::exifMalformedTiffNoCrash() const
{
    QVERIFY(ExifIfdEditor::setShort(QByteArray(), ExifIfdEditor::Ifd::Ifd0, 0x0112, 1)
                .isEmpty());
    const QByteArray junk("MM\x00\x2A\xFF\xFF\xFF\xFF", 8);   // IFD0 offset 越界
    QCOMPARE(ExifIfdEditor::setShort(junk, ExifIfdEditor::Ifd::Ifd0, 0x0112, 1), junk);
    // entry count 虚大（entry 区越界）
    QByteArray evil = makeSampleTiff();
    wr16(evil, 8, 9999);
    QCOMPARE(ExifIfdEditor::setShort(evil, ExifIfdEditor::Ifd::Ifd0, 0x0112, 1), evil);
}

void TestMetadata::pngParseAndSetChunk() const
{
    const QByteArray png = makeSamplePng({ { "eXIf", "tiff-data" } });
    QVERIFY(PngChunkIO::isValidPng(png));
    const auto chunks = PngChunkIO::parse(png);
    QCOMPARE(chunks.size(), 4);   // IHDR + eXIf + IDAT + IEND
    QCOMPARE(PngChunkIO::chunkData(png, "eXIf"), QByteArray("tiff-data"));

    // 替换 eXIf（先删旧再插新）+ 摘除（data 为空仅删除）
    QByteArray out = PngChunkIO::setChunk(png, PngChunkIO::makeExif("new-tiff"));
    QCOMPARE(PngChunkIO::chunkData(out, "eXIf"), QByteArray("new-tiff"));
    QCOMPARE(PngChunkIO::parse(out).size(), 4);
    out = PngChunkIO::setChunk(out, PngChunkIO::makeExif(QByteArray()));
    QVERIFY(PngChunkIO::chunkData(out, "eXIf").isEmpty());
    QCOMPARE(PngChunkIO::parse(out).size(), 3);
}

void TestMetadata::pngPhysValue() const
{
    const PngChunkIO::Chunk phys = PngChunkIO::makePhys(300.0);
    QCOMPARE(phys.data.size(), 9);
    // 300 dpi → 300/0.0254 = 11811 ppm（pHYs 大端序列化，按大端读回）
    const quint32 ppm = (quint32(uchar(phys.data.at(0))) << 24)
        | (quint32(uchar(phys.data.at(1))) << 16)
        | (quint32(uchar(phys.data.at(2))) << 8) | quint32(uchar(phys.data.at(3)));
    QCOMPARE(ppm, quint32(11811));
    QCOMPARE(phys.data.at(8), char(1));   // unit = metre
}

void TestMetadata::muxerJpegTransplant() const
{
    const QByteArray tiff = makeSampleTiff();
    const QByteArray source = makeSampleJpeg({
        { 0xE1, makeApp1Payload(tiff) },
        { 0xE2, QByteArray("ICC_PROFILE\0", 12) + "iccdata" },
        { 0xED, "iptc-or-xmp" },
    });
    // 编码器输出自带一个 APP1（必须被丢弃）
    const QByteArray encoded = makeSampleJpeg({ { 0xE1, "encoder-junk" } });

    ExportPolicy policy;
    policy.outputSize = QSize(1200, 800);

    const QByteArray out = MetadataMuxer::composeJpeg(encoded, source, policy);
    const auto segments = JpegSegmentIO::parse(out);
    QCOMPARE(segments.size(), 3);   // Exif + ICC + APP13

    // Exif 已覆写：Orientation=1、尺寸=输出尺寸
    const QByteArray tiffOut = MetadataMuxer::exifTiffFromJpeg(out);
    QVERIFY(!tiffOut.isEmpty());
    QCOMPARE(rd16(tiffOut, 8 + 2 + 8), quint16(1));   // Orientation=1
    QVERIFY(!out.contains("encoder-junk"));
    // ICC 原样搬运
    bool hasIcc = false;
    for (const JpegSegment &seg : segments)
        if (seg.marker == 0xE2 && seg.payload.endsWith("iccdata"))
            hasIcc = true;
    QVERIFY(hasIcc);
}

void TestMetadata::muxerStripGpsPolicy() const
{
    const QByteArray tiff = makeSampleTiff();
    const QByteArray source = makeSampleJpeg({ { 0xE1, makeApp1Payload(tiff) } });

    ExportPolicy policy;
    policy.stripGps = true;
    const QByteArray out = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source, policy);

    const QByteArray tiffOut = MetadataMuxer::exifTiffFromJpeg(out);
    QVERIFY(!tiffOut.isEmpty());
    // GPS 指针 entry 已摘除：IFD0 count=2 且无 0x8825
    QCOMPARE(rd16(tiffOut, 8), quint16(2));
    QCOMPARE(rd16(tiffOut, 8 + 2), quint16(0x0112));
}

void TestMetadata::muxerJpegWithoutExifWritesNoApp1() const
{
    // 无 Exif 源（§准入）：不写 APP1
    const QByteArray source = makeSampleJpeg({});   // 无任何元数据段
    const QByteArray out = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source,
                                                      ExportPolicy());
    QVERIFY(MetadataMuxer::exifTiffFromJpeg(out).isEmpty());
    QVERIFY(JpegSegmentIO::parse(out).isEmpty());
}

void TestMetadata::muxerPngCompose() const
{
    const QByteArray source = makeSamplePng({ { "eXIf", makeSampleTiff() },
                                              { "iCCP", "zlib-icc-bytes" } });
    const QByteArray encoded = makeSamplePng({});

    ExportPolicy policy;
    policy.dpiMode = ExportPolicy::DpiMode::Fixed;
    policy.fixedDpi = 350;

    const QByteArray out = MetadataMuxer::composePng(encoded, source, policy);
    // eXIf：搬运 + 导出语义覆写（makeSampleTiff 的 entry0 即 Orientation，
    // 源值 6 → 出图应为 1：像素已转正，元数据与新画布一致）
    const QByteArray exif = PngChunkIO::chunkData(out, "eXIf");
    QVERIFY(!exif.isEmpty());
    QVERIFY(exif.size() >= 20);
    QCOMPARE(quint8(exif.at(18)), quint8(1));   // Orientation 低字节
    QCOMPARE(quint8(exif.at(19)), quint8(0));
    QCOMPARE(PngChunkIO::chunkData(out, "iCCP"), QByteArray("zlib-icc-bytes"));
    // pHYs：350 dpi → 13780 ppm（大端读回）
    const QByteArray phys = PngChunkIO::chunkData(out, "pHYs");
    QCOMPARE(phys.size(), 9);
    const quint32 ppm = (quint32(uchar(phys.at(0))) << 24)
        | (quint32(uchar(phys.at(1))) << 16) | (quint32(uchar(phys.at(2))) << 8)
        | quint32(uchar(phys.at(3)));
    QCOMPARE(ppm, quint32(13780));
}

// —— 评审回归用例 ——

void TestMetadata::exifStripGpsKeepsExifIfdReadable() const
{
    // P0-1：strip 后其他 IFD 的 offset 必须仍然有效（0x8769 指针不动、
    // ExifIFD 的 ExposureTime 数据按原 offset 可读、XRes 数据区不动）
    const QByteArray tiff = makeRichTiff();
    const QByteArray out = ExifIfdEditor::stripGps(tiff);
    QCOMPARE(out.size(), tiff.size());
    QCOMPARE(rd32(out, 62), quint32(72));      // XRes 数据区 72/1 未错位
    QCOMPARE(rd32(out, 42), quint32(124));     // 0x8769 指针值不变
    QCOMPARE(rd16(out, 124), quint16(3));      // ExifIFD count 完整
    QCOMPARE(rd32(out, 166), quint32(1));      // ExposureTime 1/160
    QCOMPARE(rd32(out, 170), quint32(160));    // 按原 offset 可读
    // Orientation entry 仍在 IFD0
    QCOMPARE(rd16(out, 8 + 2), quint16(0x0112));
}

void TestMetadata::exifStripGpsClearsGpsBytes() const
{
    // P0-1 隐私：GPS IFD 区域与坐标数据区整体清零（不可取证还原）
    const QByteArray out = ExifIfdEditor::stripGps(makeRichTiff());
    // GPS IFD（70-100）+ 坐标数据区（100-124）全零
    QCOMPARE(out.mid(70, 54), QByteArray(54, char(0)));
    // IFD0 被删 entry 的 12 字节（46-58）置零
    QCOMPARE(out.mid(46, 12), QByteArray(12, char(0)));
    // IFD0 count 4→3
    QCOMPARE(rd16(out, 8), quint16(3));
}

void TestMetadata::exifSetRationalEvilOffsetNoCrash() const
{
    // P0：valueOffset≈0xFFFFFFFF 的 quint32 回绕不得绕过边界检查
    QByteArray evil = makeSampleTiff();
    wr32(evil, 30, 0xFFFFFFF8u);   // XRes entry 的 valueOffset
    QCOMPARE(ExifIfdEditor::setRational(evil, ExifIfdEditor::Ifd::Ifd0,
                                        ExifIfdEditor::TagXResolution, 1, 2),
             evil);   // 越界：原样返回，不崩不写
    // 贴边（offset+8==size）合法：应正常写入
    QByteArray edge = makeSampleTiff();
    wr32(edge, 30, quint32(edge.size() - 8));
    const QByteArray out = ExifIfdEditor::setRational(
        edge, ExifIfdEditor::Ifd::Ifd0, ExifIfdEditor::TagXResolution, 1, 2);
    QCOMPARE(rd32(out, edge.size() - 8), quint32(1));
}

void TestMetadata::exifBigEndianPositivePath() const
{
    // P1：MM 大端正向路径（此前只有 junk 负例）
    QByteArray mm;
    mm.append("MM");
    mm.resize(8);
    wr16be(mm, 2, 42);
    wr32be(mm, 4, 8);
    mm.resize(26);
    wr16be(mm, 8, 1);           // IFD0 count=1
    wr16be(mm, 10, 0x0112);     // Orientation
    wr16be(mm, 12, 3);          // SHORT
    wr32be(mm, 14, 1);          // count
    wr16be(mm, 18, 6);          // value = 6（旋转 90°）
    wr16be(mm, 20, 0);
    wr32be(mm, 22, 0);          // next IFD

    const QByteArray out = ExifIfdEditor::setShort(
        mm, ExifIfdEditor::Ifd::Ifd0, ExifIfdEditor::TagOrientation, 1);
    QCOMPARE(out.size(), mm.size());
    QCOMPARE(rd16be(out, 18), quint16(1));   // 大端读回 1
    // 无 GPS：stripGps 原样
    QCOMPARE(ExifIfdEditor::stripGps(mm), mm);
}

void TestMetadata::jpegSpliceKeepsApp0() const
{
    // P1：APP0-JFIF 豁免（含 density；删除会让 DPI 语义缺半边）
    const QByteArray encoded = makeSampleJpeg({ { 0xE0, QByteArray("JFIF-density") } });
    const QByteArray out = JpegSegmentIO::splice(encoded, {});
    QVERIFY(out.contains(QByteArrayLiteral("JFIF-density")));
    // APP0 不再作为元数据段收集（parse 只管 E1/E2/ED/FE）
    QVERIFY(JpegSegmentIO::parse(out).isEmpty());
}

void TestMetadata::jpegFuzzHeavyDuty() const
{
    // P1 fuzz 补强：全 FF / 全 00 / 长度 0-1 / 超长段
    const QByteArray soi = QByteArrayLiteral("\xFF\xD8");
    QVERIFY(JpegSegmentIO::parse(soi + QByteArray(512, char(0xFF))).isEmpty());
    QVERIFY(JpegSegmentIO::parse(soi + QByteArray(512, char(0x00))).isEmpty());
    QVERIFY(JpegSegmentIO::parse(QByteArrayLiteral("\xFF\xD8\xFF\xE1\x00\x00"))
                .isEmpty());   // length=0
    QVERIFY(JpegSegmentIO::parse(QByteArrayLiteral("\xFF\xD8\xFF\xE1\x00\x01"))
                .isEmpty());   // length=1
    QVERIFY(JpegSegmentIO::parse(soi + QByteArrayLiteral("\xFF\xFF")).isEmpty());
    // splice 超长段（>65533）：丢段且输出结构完好
    QList<JpegSegment> meta;
    JpegSegment seg;
    seg.marker = 0xFE;
    seg.payload = QByteArray(70000, 'x');
    meta.append(seg);
    const QByteArray out = JpegSegmentIO::splice(makeSampleJpeg({}), meta);
    QVERIFY(JpegSegmentIO::parse(out).isEmpty());
    QVERIFY(out.endsWith(QByteArrayLiteral("\xFF\xD9")));
}

void TestMetadata::muxerXmpApp1NotMisdetected() const
{
    // P0：XMP APP1（Lightroom/手机常见）不得被误包成 "Exif\0\0" 伪段
    const QByteArray xmpPayload
        = QByteArray("http://ns.adobe.com/xap/1.0/\x00", 29)
        + QByteArrayLiteral("<x:xmpmeta>meta</x:xmpmeta>");
    const QByteArray source = makeSampleJpeg({ { 0xE1, xmpPayload } });

    const QByteArray out = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source,
                                                      ExportPolicy());
    QVERIFY(MetadataMuxer::exifTiffFromJpeg(out).isEmpty());
    for (const JpegSegment &seg : JpegSegmentIO::parse(out))
        QVERIFY(!seg.payload.startsWith(QByteArrayLiteral("Exif\0\0")));
    QVERIFY(out.contains(QByteArrayLiteral("<x:xmpmeta>")));   // XMP 原样保留

    // stripGps 时 XMP 整段丢弃（隐私：XMP 可嵌坐标）
    ExportPolicy policy;
    policy.stripGps = true;
    const QByteArray out2 = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source,
                                                       policy);
    QVERIFY(!out2.contains(QByteArrayLiteral("<x:xmpmeta>")));
}

void TestMetadata::muxerComposeFromExifFullChain() const
{
    // P0：跨格式重建路径（设计 §4.8）此前零覆盖
    ExportPolicy policy;
    policy.outputSize = QSize(1200, 800);
    QByteArray icc(70000, char(0x5A));   // >64KB：触发 ICC 分段

    const QByteArray out = MetadataMuxer::composeJpegFromExif(
        makeSampleJpeg({}), makeRichTiff(), icc, policy);

    // APP1：Exif 可读 + 导出覆写（Orientation=1 / PixelDimension=输出尺寸）
    const QByteArray tiff = MetadataMuxer::exifTiffFromJpeg(out);
    QVERIFY(!tiff.isEmpty());
    QCOMPARE(rd16(tiff, 18), quint16(1));        // Orientation
    QCOMPARE(rd16(tiff, 124 + 14 + 8), quint16(1200));   // PixelXDimension
    QCOMPARE(rd16(tiff, 124 + 26 + 8), quint16(800));    // PixelYDimension

    // APP2：ICC 规范分段，seqno/total 正确，拼接还原
    QByteArray joined;
    int iccSegCount = 0;
    for (const JpegSegment &seg : JpegSegmentIO::parse(out)) {
        if (seg.marker != 0xE2)
            continue;
        ++iccSegCount;
        QVERIFY(seg.payload.startsWith(QByteArrayLiteral("ICC_PROFILE\0")));
        QCOMPARE(quint8(seg.payload.at(12)), quint8(iccSegCount));   // seqno
        QCOMPARE(quint8(seg.payload.at(13)), quint8(2));             // total
        joined += seg.payload.mid(16);
    }
    QCOMPARE(iccSegCount, 2);
    QCOMPARE(joined, icc);
}


void TestMetadata::muxerExportPolicyNegative() const
{
    const QByteArray source = makeSampleJpeg({
        { 0xE1, makeApp1Payload(makeRichTiff()) },
        { 0xE2, QByteArray("ICC_PROFILE\0", 12) + QByteArray("icc") },
        { 0xED, QByteArray("iptc") },
        { 0xFE, QByteArray("comment") },
    });

    // 全关：不搬任何元数据段
    ExportPolicy off;
    off.keepExif = false;
    off.keepIcc = false;
    off.keepIptcXmp = false;
    const QByteArray outOff = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source,
                                                         off);
    QVERIFY(JpegSegmentIO::parse(outOff).isEmpty());

    // 默认开 + stripGps：Exif/ICC 保留，APP13/COM 丢弃
    ExportPolicy policy;
    policy.stripGps = true;
    const QByteArray out = MetadataMuxer::composeJpeg(makeSampleJpeg({}), source,
                                                      policy);
    bool hasIptc = false;
    bool hasCom = false;
    bool hasIcc = false;
    for (const JpegSegment &seg : JpegSegmentIO::parse(out)) {
        if (seg.marker == 0xED)
            hasIptc = true;
        if (seg.marker == 0xFE)
            hasCom = true;
        if (seg.marker == 0xE2)
            hasIcc = true;
    }
    QVERIFY(!hasIptc);   // stripGps：IPTC 嵌坐标风险，整段弃
    QVERIFY(!hasCom);
    QVERIFY(hasIcc);
    // Exif 在且 GPS 已摘（IFD0 count 4→3）
    const QByteArray tiff = MetadataMuxer::exifTiffFromJpeg(out);
    QCOMPARE(rd16(tiff, 8), quint16(3));
}

void TestMetadata::pngChunkOrderBeforeIdat() const
{
    // P1：辅助 chunk 必须插在首个 IDAT 前（libpng 忽略 IDAT 后的辅助段）
    const QByteArray png = makeSamplePng({ { "eXIf", QByteArray("t") } });
    const QByteArray out = PngChunkIO::setChunk(png, PngChunkIO::makePhys(72.0));
    int physIdx = -1;
    int idatIdx = -1;
    const auto chunks = PngChunkIO::parse(out);
    for (int i = 0; i < chunks.size(); ++i) {
        if (chunks[i].type == QByteArrayLiteral("pHYs"))
            physIdx = i;
        if (chunks[i].type == QByteArrayLiteral("IDAT") && idatIdx < 0)
            idatIdx = i;
    }
    QVERIFY(physIdx >= 0);
    QVERIFY(idatIdx > physIdx);   // pHYs 先于 IDAT
    // 删除同类型段功能不受影响（data 为空 = 仅删除）
    PngChunkIO::Chunk emptyPhys;
    emptyPhys.type = QByteArrayLiteral("pHYs");
    const QByteArray noPhys = PngChunkIO::setChunk(out, emptyPhys);
    QVERIFY(PngChunkIO::chunkData(noPhys, "pHYs").isEmpty());
}

void TestMetadata::pngCrcGeneratedMatchesZlib() const
{
    // P1：CRC 生成端用独立实现 + zlib 已知向量双重校验
    auto slowCrc32 = [](const QByteArray &data) {
        quint32 crc = 0xFFFFFFFFu;
        for (uchar ch : data) {
            crc ^= ch;
            for (int k = 0; k < 8; ++k)
                crc = (crc & 1) ? (0xEDB88320u ^ (crc >> 1)) : (crc >> 1);
        }
        return crc ^ 0xFFFFFFFFu;
    };
    QCOMPARE(slowCrc32(QByteArrayLiteral("123456789")), quint32(0xCBF43926u));

    const QByteArray out = PngChunkIO::setChunk(makeSamplePng({}),
                                                PngChunkIO::makePhys(72.0));
    int i = 8;
    const int n = out.size();
    while (i + 12 <= n) {
        const quint32 len = rd32be(out, i);
        const QByteArray type = out.mid(i + 4, 4);
        if (type == QByteArrayLiteral("pHYs")) {
            // chunk CRC = type+data 的 CRC-32（大端存储）
            QCOMPARE(rd32be(out, int(i + 8 + len)),
                     slowCrc32(out.mid(i + 4, int(4 + len))));
            return;
        }
        i += 12 + int(len);
    }
    QFAIL("未找到 pHYs chunk");
}

QTEST_MAIN(TestMetadata)
#include "test_metadata.moc"
