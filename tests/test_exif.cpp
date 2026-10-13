// M2.1/2.2 单测：ExifFormatter 边界值 + FieldRegistry 映射 + ExifReader(exiv2) 集成
// 集成用例以 exiv2 API 写入测试 JPEG 再读回（顺带补上带 Orientation 的测试图缺口）
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

#include "metadata/ExifFormatter.h"
#include "metadata/ExifReader.h"
#include "metadata/FieldRegistry.h"

#include <exiv2/exiv2.hpp>

using namespace fxt;

namespace {

QString writeTestJpeg(const QTemporaryDir &dir, const QSize &size)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor(120, 130, 140));
    const QString path = dir.filePath(QStringLiteral("exif_%1x%2.jpg")
                                          .arg(size.width())
                                          .arg(size.height()));
    QImageWriter writer(path, "jpeg");
    writer.setQuality(92);
    writer.write(img);
    return path;
}

} // namespace

class TestExif : public QObject
{
    Q_OBJECT

private slots:
    // —— ExifFormatter 边界 ——
    void shutterBoundaries() const;
    void fNumberAndTrim() const;
    void exposureCompSign() const;
    void gpsDmsFormat() const;
    void dateTimeNormalizes() const;
    void invalidInputsReturnEmpty() const;
    // —— 评审回归（M2 评审修复锁定）——
    void enumFieldsLegalValues() const;
    void apexFNumberConverts() const;
    void formatterEdgeCorrections() const;
    // —— FieldRegistry ——
    void registryMapsTagsToDisplay() const;
    void registryFallsBackAlongTagChain() const;
    void registryOverridesWin() const;
    void makerFieldsMissingSilently() const;
    void registryApexAndMakeDisplay() const;
    void registryGpsRefMissingOmits() const;
    // —— ExifReader（exiv2 集成）——
    void readerRoundTripsWrittenExif() const;
    void readerRejectsNonImage() const;
    void readerOnPlainJpegNoCrash() const;
};

void TestExif::shutterBoundaries() const
{
    // <1s：分数化 + 常见值吸附
    QCOMPARE(ExifFormatter::shutter(1.0 / 160), QStringLiteral("1/160s"));
    QCOMPARE(ExifFormatter::shutter(1.0 / 159), QStringLiteral("1/160s"));   // 5% 吸附
    QCOMPARE(ExifFormatter::shutter(0.5), QStringLiteral("1/2s"));
    QCOMPARE(ExifFormatter::shutter(1.0 / 3), QStringLiteral("1/3s"));
    // ≥1s：小数 s
    QCOMPARE(ExifFormatter::shutter(1.6), QStringLiteral("1.6s"));
    QCOMPARE(ExifFormatter::shutter(30.0), QStringLiteral("30s"));
    QCOMPARE(ExifFormatter::shutter(1.0), QStringLiteral("1s"));
    // 非法
    QVERIFY(ExifFormatter::shutter(0).isEmpty());
    QVERIFY(ExifFormatter::shutter(-0.5).isEmpty());
}

void TestExif::fNumberAndTrim() const
{
    QCOMPARE(ExifFormatter::fNumber(5.6), QStringLiteral("f/5.6"));
    QCOMPARE(ExifFormatter::fNumber(8.0), QStringLiteral("f/8.0"));       // 固定一位小数（f/4.0，用户指令 2026-10-14）
    QCOMPARE(ExifFormatter::fNumber(1.4), QStringLiteral("f/1.4"));
    QCOMPARE(ExifFormatter::fNumber(22.0), QStringLiteral("f/22.0"));
    QVERIFY(ExifFormatter::fNumber(0).isEmpty());
    QCOMPARE(ExifFormatter::iso(140), QStringLiteral("ISO 140"));
    QVERIFY(ExifFormatter::iso(0).isEmpty());
    QCOMPARE(ExifFormatter::focalLength(42.3), QStringLiteral("42mm"));
    QCOMPARE(ExifFormatter::focalLengthEq(63.0), QStringLiteral("63mm eq."));
}

void TestExif::exposureCompSign() const
{
    QCOMPARE(ExifFormatter::exposureComp(0.3), QStringLiteral("+0.3 EV"));
    QCOMPARE(ExifFormatter::exposureComp(-1.0), QStringLiteral("-1 EV"));
    QCOMPARE(ExifFormatter::exposureComp(0.0), QStringLiteral("0 EV"));
    QCOMPARE(ExifFormatter::exposureComp(-0.3), QStringLiteral("-0.3 EV"));
}

void TestExif::gpsDmsFormat() const
{
    // 25.035° N → 25°02'06.0"N
    QCOMPARE(ExifFormatter::gpsLatitude(25.035, QLatin1Char('N')),
             QStringLiteral("25°02'06.0\"N"));
    // 南纬/西经符号
    QCOMPARE(ExifFormatter::gpsLatitude(-33.5, QLatin1Char('S')),
             QStringLiteral("33°30'00.0\"S"));
    QCOMPARE(ExifFormatter::gpsLongitude(-122.4194, QLatin1Char('W')),
             QStringLiteral("122°25'09.8\"W"));
    // 秒进位：25.036111 → 25°02'10.0"
    QCOMPARE(ExifFormatter::gpsLatitude(25.0361111, QLatin1Char('N')),
             QStringLiteral("25°02'10.0\"N"));
    QVERIFY(ExifFormatter::gpsDms(qInf(), true, QLatin1Char('N'),
                                  QLatin1Char('S'))
                .isEmpty());
    // 海拔：传入绝对值，belowSeaLevel 决定符号（Exif 语义：Ref=1 → 海平面以下）
    QCOMPARE(ExifFormatter::gpsAltitude(420.0, true), QStringLiteral("-420 m"));
    QCOMPARE(ExifFormatter::gpsAltitude(8848.0, false), QStringLiteral("8848 m"));
}

void TestExif::dateTimeNormalizes() const
{
    QCOMPARE(ExifFormatter::dateTime(QStringLiteral("2026:10:10 12:00:00")),
             QStringLiteral("2026-10-10 12:00"));
    QCOMPARE(ExifFormatter::dateTime(QStringLiteral("2026:10:10 12:00:00"), true),
             QStringLiteral("2026-10-10 12:00:00"));
    // 非标格式原样（有值总比丢值好）
    QCOMPARE(ExifFormatter::dateTime(QStringLiteral("unknown")),
             QStringLiteral("unknown"));
}

void TestExif::invalidInputsReturnEmpty() const
{
    QVERIFY(ExifFormatter::focalLength(0).isEmpty());
    QVERIFY(ExifFormatter::focalLengthEq(-1).isEmpty());
    QVERIFY(ExifFormatter::meteringMode(99).isEmpty());
    QVERIFY(ExifFormatter::whiteBalance(5).isEmpty());
    QVERIFY(ExifFormatter::exposureProgram(42).isEmpty());
    QVERIFY(ExifFormatter::sceneCaptureType(9).isEmpty());
    QVERIFY(ExifFormatter::trimFloat(qInf()).isEmpty());
}

// —— 评审回归 ——

void TestExif::enumFieldsLegalValues() const
{
    // P1：枚举字段合法值此前零覆盖（flash 位运算逻辑未执行过）
    QCOMPARE(ExifFormatter::flash(0), QStringLiteral("未闪光"));
    QCOMPARE(ExifFormatter::flash(0x01), QStringLiteral("闪光"));
    QCOMPARE(ExifFormatter::flash(0x07), QStringLiteral("强制闪光"));
    QCOMPARE(ExifFormatter::flash(0x09), QStringLiteral("补光闪光"));
    QCOMPARE(ExifFormatter::flash(0x0D), QStringLiteral("防红眼闪光"));
    QCOMPARE(ExifFormatter::whiteBalance(0), QStringLiteral("自动白平衡"));
    QCOMPARE(ExifFormatter::whiteBalance(1), QStringLiteral("手动白平衡"));
    QCOMPARE(ExifFormatter::meteringMode(3), QStringLiteral("点测"));
    QCOMPARE(ExifFormatter::meteringMode(5), QStringLiteral("图案测光"));
    QCOMPARE(ExifFormatter::exposureProgram(3), QStringLiteral("光圈优先"));
    QCOMPARE(ExifFormatter::sceneCaptureType(2), QStringLiteral("风景"));
    // 富士胶片模拟（探针核对 FilmMode 枚举后的映射）
    QCOMPARE(ExifFormatter::fujiFilmMode(0), QStringLiteral("标准 (Provia)"));
    QCOMPARE(ExifFormatter::fujiFilmMode(1), QStringLiteral("鲜艳 (Velvia)"));
    QCOMPARE(ExifFormatter::fujiFilmMode(2), QStringLiteral("柔和 (Astia)"));
    QCOMPARE(ExifFormatter::fujiFilmMode(256), QStringLiteral("Pro Neg. Hi"));
    QVERIFY(ExifFormatter::fujiFilmMode(99).isEmpty());   // 未确证值不出
}

void TestExif::apexFNumberConverts() const
{
    // P0：MaxApertureValue 是 APEX（f=2^(v/2)），直接按 FNumber 会错
    QCOMPARE(ExifFormatter::apexFNumber(3.0), QStringLiteral("f/2.8"));
    QCOMPARE(ExifFormatter::apexFNumber(4.0), QStringLiteral("f/4.0"));
    QCOMPARE(ExifFormatter::apexFNumber(5.0), QStringLiteral("f/5.7"));
    QVERIFY(ExifFormatter::apexFNumber(0).isEmpty());
    QVERIFY(ExifFormatter::apexFNumber(-1).isEmpty());
}

void TestExif::formatterEdgeCorrections() const
{
    // P2 修正锁定：0.98s → "1s"（非 "1/1"）
    QCOMPARE(ExifFormatter::shutter(0.98), QStringLiteral("1s"));
    QCOMPARE(ExifFormatter::shutter(0.5), QStringLiteral("1/2s"));
    // EV 负零：|ev|<0.05 → "0 EV"
    QCOMPARE(ExifFormatter::exposureComp(0.04), QStringLiteral("0 EV"));
    QCOMPARE(ExifFormatter::exposureComp(-0.04), QStringLiteral("0 EV"));
    // 未设时钟不格式化成假日期
    QVERIFY(ExifFormatter::dateTime(QStringLiteral("0000:00:00 12:00:00"))
                .isEmpty());
    QVERIFY(ExifFormatter::dateTime(QStringLiteral("2026:00:10 12:00:00"))
                .isEmpty());
}

void TestExif::registryMapsTagsToDisplay() const
{
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("Canon"));
    raw.insert(QStringLiteral("Exif.Photo.ExposureTime"),
               QStringLiteral("1/160"));   // Rational 字符串（exiv2 解析输入）
    raw.insert(QStringLiteral("Exif.Photo.FNumber"), QStringLiteral("56/10"));
    raw.insert(QStringLiteral("Exif.Photo.ISOSpeedRatings"),
               QStringLiteral("140"));

    const auto display = FieldRegistry::resolveDisplay(raw);
    QCOMPARE(display.value(QStringLiteral("make")), QStringLiteral("Canon"));
    QCOMPARE(display.value(QStringLiteral("shutter")), QStringLiteral("1/160s"));
    QCOMPARE(display.value(QStringLiteral("fNumber")), QStringLiteral("f/5.6"));
    QCOMPARE(display.value(QStringLiteral("iso")), QStringLiteral("ISO 140"));
    // 未注册的 raw tag 不出现在展示集
    QVERIFY(!display.contains(QStringLiteral("software")));
}

void TestExif::registryFallsBackAlongTagChain() const
{
    // ISO 取值链：ISOSpeedRatings → ISOSpeed（前者缺失回落后者）
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Photo.ISOSpeed"), QStringLiteral("200"));
    const auto display = FieldRegistry::resolveDisplay(raw);
    QCOMPARE(display.value(QStringLiteral("iso")), QStringLiteral("ISO 200"));
}

void TestExif::registryOverridesWin() const
{
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("Canon"));
    QHash<QString, QString> overrides;
    overrides.insert(QStringLiteral("make"), QStringLiteral("佳能"));

    const auto display = FieldRegistry::resolveDisplay(raw, overrides);
    QCOMPARE(display.value(QStringLiteral("make")), QStringLiteral("佳能"));
    // 覆盖层缺失的字段仍走正常解析
    QCOMPARE(display.value(QStringLiteral("shutter")), QString());
}

void TestExif::makerFieldsMissingSilently() const
{
    // MakerNote 字段缺失（非富士机身）：静默置空（§4.5 设计内行为）
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("SONY"));
    const auto display = FieldRegistry::resolveDisplay(raw);
    QVERIFY(!display.contains(QStringLiteral("fujiFilmSimulation")));
    QVERIFY(display.contains(QStringLiteral("make")));
}

void TestExif::registryApexAndMakeDisplay() const
{
    // P0：maxAperture 走 APEX 换算（此前按 FNumber 直读会 f/2.8→"f/3"）
    // P1：make 走品牌别名映射展示（"NIKON CORPORATION" → "Nikon"）
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Image.Make"),
               QStringLiteral("NIKON CORPORATION"));
    raw.insert(QStringLiteral("Exif.Image.Model"), QStringLiteral("NIKON Z 7_2"));
    raw.insert(QStringLiteral("Exif.Photo.MaxApertureValue"),
               QStringLiteral("3/1"));
    const auto display = FieldRegistry::resolveDisplay(raw);
    QCOMPARE(display.value(QStringLiteral("maxAperture")),
             QStringLiteral("f/2.8"));
    QCOMPARE(display.value(QStringLiteral("make")), QStringLiteral("Nikon"));
    // 未知品牌保留原始 make（有信息总比 "Others" 好）
    RawExif raw2;
    raw2.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("MysteryCam"));
    const auto display2 = FieldRegistry::resolveDisplay(raw2);
    QCOMPARE(display2.value(QStringLiteral("make")), QStringLiteral("MysteryCam"));
}

void TestExif::registryGpsRefMissingOmits() const
{
    // P2：GPS Ref 缺失不出值（默认 N/E 会在南/西半球显示错半球）
    RawExif raw;
    raw.insert(QStringLiteral("Exif.GPSInfo.GPSLatitude"),
               QStringLiteral("-33.8688"));
    const auto display = FieldRegistry::resolveDisplay(raw);
    QVERIFY(!display.contains(QStringLiteral("gpsLatitude")));
    // Ref 齐全时正常出值
    raw.insert(QStringLiteral("Exif.GPSInfo.GPSLatitudeRef"),
               QStringLiteral("S"));
    const auto display2 = FieldRegistry::resolveDisplay(raw);
    QCOMPARE(display2.value(QStringLiteral("gpsLatitude")),
             QStringLiteral("33°52'07.7\"S"));
}

void TestExif::readerRoundTripsWrittenExif() const
{
    // exiv2 写入 → ExifReader 读回：全链路集成（含 Orientation 测试图补缺）
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    {
        Exiv2::Image::UniquePtr image = Exiv2::ImageFactory::open(path.toStdString());
        QVERIFY(image.get() != nullptr);
        image->readMetadata();
        Exiv2::ExifData &exif = image->exifData();
        exif["Exif.Image.Make"] = "OM Digital Solutions";
        exif["Exif.Image.Model"] = "OM-1";
        exif["Exif.Image.Orientation"] = uint16_t(6);   // 竖拍右转
        exif["Exif.Image.ImageWidth"] = uint16_t(640);
        exif["Exif.Image.ImageLength"] = uint16_t(480);
        exif["Exif.Photo.ExposureTime"] = Exiv2::Rational(1, 160);
        exif["Exif.Photo.FNumber"] = Exiv2::Rational(28, 10);
        exif["Exif.Photo.ISOSpeedRatings"] = uint16_t(1400);
        exif["Exif.Photo.DateTimeOriginal"] = "2026:10:10 12:00:00";
        exif["Exif.Photo.FocalLengthIn35mmFilm"] = uint16_t(63);
        exif["Exif.GPSInfo.GPSLatitude"] = "25/1 2/1 60000/10000";   // 25°02'06"
        exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
        try {
            image->writeMetadata();   // 0.28：失败抛 Exiv2::Error，成功返回 void
        } catch (const std::exception &e) {
            QFAIL(qPrintable(QStringLiteral("写入测试 Exif 失败：%1")
                                 .arg(QString::fromUtf8(e.what()))));
        }
    }

    ExifReader reader(path);
    QVERIFY2(reader.read(), qPrintable(reader.error()));

    const auto display = FieldRegistry::resolveDisplay(reader.raw());
    // make 按 §7.2 走品牌别名映射展示（"OM Digital Solutions" → "OM System"）
    QCOMPARE(display.value(QStringLiteral("make")),
             QStringLiteral("OM System"));
    QCOMPARE(display.value(QStringLiteral("model")), QStringLiteral("OM-1"));
    QCOMPARE(display.value(QStringLiteral("shutter")), QStringLiteral("1/160s"));
    QCOMPARE(display.value(QStringLiteral("fNumber")), QStringLiteral("f/2.8"));
    QCOMPARE(display.value(QStringLiteral("iso")), QStringLiteral("ISO 1400"));
    QCOMPARE(display.value(QStringLiteral("dateTime")),
             QStringLiteral("2026-10-10 12:00"));
    QCOMPARE(display.value(QStringLiteral("focalLengthEq")),
             QStringLiteral("63mm eq."));
    // GPS 三分式 → 十进制 → DMS
    QCOMPARE(display.value(QStringLiteral("gpsLatitude")),
             QStringLiteral("25°02'06.0\"N"));
    QCOMPARE(reader.pixelSize(), QSize(640, 480));
}

void TestExif::readerRejectsNonImage() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bad = dir.filePath("junk.jpg");
    QFile f(bad);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("definitely not an image");
    f.close();

    ExifReader reader(bad);
    QVERIFY(!reader.read());
    QVERIFY(!reader.error().isEmpty());
}

void TestExif::readerOnPlainJpegNoCrash() const
{
    // 无 Exif 的普通 JPEG：读成功 + 空结果
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {320, 240});

    ExifReader reader(path);
    QVERIFY2(reader.read(), qPrintable(reader.error()));
    QVERIFY(reader.raw().isEmpty()
            || !reader.raw()
                    .contains(QStringLiteral("Exif.Photo.DateTimeOriginal")));
    const auto display = FieldRegistry::resolveDisplay(reader.raw());
    QVERIFY(display.isEmpty());
}

QTEST_MAIN(TestExif)
#include "test_exif.moc"
