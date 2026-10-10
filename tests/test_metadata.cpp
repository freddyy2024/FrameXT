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
    QCOMPARE(PngChunkIO::chunkData(out, "eXIf"), makeSampleTiff());   // 原样搬运
    QCOMPARE(PngChunkIO::chunkData(out, "iCCP"), QByteArray("zlib-icc-bytes"));
    // pHYs：350 dpi → 13780 ppm（大端读回）
    const QByteArray phys = PngChunkIO::chunkData(out, "pHYs");
    QCOMPARE(phys.size(), 9);
    const quint32 ppm = (quint32(uchar(phys.at(0))) << 24)
        | (quint32(uchar(phys.at(1))) << 16) | (quint32(uchar(phys.at(2))) << 8)
        | quint32(uchar(phys.at(3)));
    QCOMPARE(ppm, quint32(13780));
}

QTEST_MAIN(TestMetadata)
#include "test_metadata.moc"
