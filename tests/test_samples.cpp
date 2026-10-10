// M2.5 往返测试集（03-开发计划 2.5 / 验收 §8.4）：
// 真实样张（外部目录，不入库）× 三项断言：
//   1. ExifReader 全量读取不崩、品牌字段可读（各品牌样张）
//   2. MetadataMuxer 段搬运往返：拍摄 Exif 关键集一致 + ICC 字节一致
//   3. stripGps 策略生效（含 GPS 的样本 GPS 字段摘除）
// 样本目录：环境变量 FXT_SAMPLE_DIR（如 E:\test photos）；未设置/不存在时
// 全部用例 QSKIP——样本集 M6 前不入 CI，个人机跑全量。
#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

#include "metadata/ExifReader.h"
#include "metadata/FieldRegistry.h"
#include "platform/JpegSegmentIO.h"
#include "platform/MetadataMuxer.h"

using namespace fxt;
using namespace fxt::Platform;

namespace {

// 拍摄关键 tag 集（往返保真断言范围；Dimensions/Orientation 按设计会变化，
// 不在比对集——验收 §8.4 "拍摄 Exif tag 一致（Dimensions/Orientation 除外）"）
QStringList keyExifTags()
{
    return {
        QStringLiteral("Exif.Image.Make"),
        QStringLiteral("Exif.Image.Model"),
        QStringLiteral("Exif.Photo.DateTimeOriginal"),
        QStringLiteral("Exif.Image.DateTime"),
        QStringLiteral("Exif.Photo.ExposureTime"),
        QStringLiteral("Exif.Photo.FNumber"),
        QStringLiteral("Exif.Photo.ISOSpeedRatings"),
        QStringLiteral("Exif.Photo.FocalLength"),
        QStringLiteral("Exif.Photo.LensModel"),
    };
}

// 模拟导出产物：无元数据的编码 JPEG（元数据由 Muxer 搬运注入）
QByteArray encodePlainJpeg(const QSize &size)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor(200, 200, 205));
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "jpeg");
    writer.setQuality(92);
    writer.write(img);
    buffer.close();
    return bytes;
}

QByteArray iccPayloadOf(const QByteArray &jpeg)
{
    // 全部 APP2 ICC 段 payload 顺序拼接（搬运是字节级原样拷贝，拼接结果
    // 相等即段集合与顺序均一致）
    QByteArray joined;
    for (const JpegSegment &seg : JpegSegmentIO::parse(jpeg)) {
        if (seg.marker == 0xE2
            && seg.payload.startsWith(QByteArray("ICC_PROFILE\0", 12)))
            joined += seg.payload;
    }
    return joined;
}

} // namespace

class TestSamples : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void exifReadableAcrossBrands() const;
    void muxerRoundTripPreservesKeyExif() const;
    void muxerRoundTripPreservesIcc() const;
    void muxerStripGpsRemovesLocation() const;

private:
    QList<QString> sampleJpegs() const;
    QString m_dir;
};

void TestSamples::initTestCase()
{
    m_dir = qEnvironmentVariable("FXT_SAMPLE_DIR");
    if (m_dir.isEmpty() || !QDir(m_dir).exists()) {
        QSKIP("未设置 FXT_SAMPLE_DIR（真实样张目录），样本集测试跳过");
    }
}

QList<QString> TestSamples::sampleJpegs() const
{
    QList<QString> out;
    const QDir dir(m_dir);
    for (const QFileInfo &info :
         dir.entryInfoList({ QStringLiteral("*.jpg"), QStringLiteral("*.jpeg") },
                           QDir::Files, QDir::Name)) {
        out.append(info.absoluteFilePath());
    }
    return out;
}

void TestSamples::exifReadableAcrossBrands() const
{
    const auto files = sampleJpegs();
    QVERIFY2(files.size() >= 10, "样本集不足 10 张，请检查 FXT_SAMPLE_DIR");

    int readable = 0;
    int withMake = 0;
    QStringList failures;
    for (const QString &path : files) {
        ExifReader reader(path);
        if (!reader.read()) {
            // 微信转发图可能已剥离元数据/非 JPEG——记录而非失败
            failures << QStringLiteral("%1: %2")
                            .arg(QFileInfo(path).fileName(), reader.error());
            continue;
        }
        ++readable;
        const auto display = FieldRegistry::resolveDisplay(reader.raw());
        if (!display.value(QStringLiteral("make")).isEmpty())
            ++withMake;
        // 可读样本至少解析出 2 个展示字段（make/model/时间/参数任取）
        if (display.size() < 2)
            failures << QStringLiteral("%1: 展示字段不足 2 个")
                            .arg(QFileInfo(path).fileName());
    }
    qInfo() << QStringLiteral("样本 %1 张：可读 %2，含厂商字段 %3")
                   .arg(files.size())
                   .arg(readable)
                   .arg(withMake);
    QVERIFY2(withMake >= 5, "含厂商字段样本不足 5 张（品牌覆盖缺口）");
    QVERIFY2(failures.size() <= files.size() / 3, "超过 1/3 样本读取异常");
}

void TestSamples::muxerRoundTripPreservesKeyExif() const
{
    const auto files = sampleJpegs();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    ExportPolicy policy;
    policy.outputSize = QSize(2048, 1365);

    int compared = 0;
    for (const QString &path : files) {
        QFile sourceFile(path);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray source = sourceFile.readAll();
        sourceFile.close();

        const QByteArray encoded = encodePlainJpeg({2048, 1365});
        const QByteArray out = MetadataMuxer::composeJpeg(encoded, source, policy);
        QVERIFY(!out.isEmpty());

        // 写临时文件后用 ExifReader 走完整读回路径
        const QString outPath = dir.filePath(
            QStringLiteral("rt_%1").arg(QFileInfo(path).fileName()));
        QFile outFile(outPath);
        QVERIFY(outFile.open(QIODevice::WriteOnly));
        outFile.write(out);
        outFile.close();

        ExifReader srcReader(path);
        ExifReader outReader(outPath);
        if (!srcReader.read())
            continue;   // 无元数据样本跳过（微信图）
        QVERIFY2(outReader.read(), qPrintable(outReader.error()));
        ++compared;

        for (const QString &tag : keyExifTags()) {
            const QString sv = srcReader.raw().value(tag);
            const QString ov = outReader.raw().value(tag);
            // 源缺失的 tag 允许输出缺失；源有而输出丢失 = 保真失败
            QVERIFY2(ov == sv,
                     qPrintable(QStringLiteral("%1 的 %2 搬运失真：源[%3] → 出[%4]")
                                    .arg(QFileInfo(path).fileName(), tag, sv, ov)));
        }
    }
    qInfo() << "关键 Exif 往返比对样本数：" << compared;
    QVERIFY2(compared >= 5, "可比对样本不足 5 张");
}

void TestSamples::muxerRoundTripPreservesIcc() const
{
    const auto files = sampleJpegs();

    ExportPolicy policy;
    int withIcc = 0;
    for (const QString &path : files) {
        QFile sourceFile(path);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray source = sourceFile.readAll();
        sourceFile.close();

        const QByteArray srcIcc = iccPayloadOf(source);
        if (srcIcc.isEmpty())
            continue;   // 无 ICC 样本

        const QByteArray out =
            MetadataMuxer::composeJpeg(encodePlainJpeg({1024, 680}), source, policy);
        const QByteArray outIcc = iccPayloadOf(out);
        QVERIFY2(outIcc == srcIcc,
                 qPrintable(QStringLiteral("%1 的 ICC 搬运失真")
                                .arg(QFileInfo(path).fileName())));
        ++withIcc;
    }
    qInfo() << "含 ICC 样本数：" << withIcc;
    // 假绿护栏（评审 P0）：样本集必须有足量含 ICC 样本，否则本用例
    // 从未真正执行过断言（替换样张集时同步维护）
    QVERIFY2(withIcc >= 5, "样张集含 ICC 样本不足 5 张，ICC 保真断言覆盖失效");
}

void TestSamples::muxerStripGpsRemovesLocation() const
{
    const auto files = sampleJpegs();

    ExportPolicy strip;
    strip.stripGps = true;

    int withGps = 0;
    for (const QString &path : files) {
        ExifReader reader(path);
        if (!reader.read())
            continue;
        const bool hasGps = reader.raw().contains(
            QStringLiteral("Exif.GPSInfo.GPSLatitude"));
        if (!hasGps)
            continue;
        ++withGps;

        QFile sourceFile(path);
        QVERIFY(sourceFile.open(QIODevice::ReadOnly));
        const QByteArray source = sourceFile.readAll();
        sourceFile.close();

        const QByteArray out = MetadataMuxer::composeJpeg(
            encodePlainJpeg({800, 600}), source, strip);

        const QString outPath = QDir::temp().absoluteFilePath(
            QStringLiteral("gps_%1").arg(QFileInfo(path).fileName()));
        QFile outFile(outPath);
        QVERIFY(outFile.open(QIODevice::WriteOnly));
        outFile.write(out);
        outFile.close();

        ExifReader stripped(outPath);
        QVERIFY2(stripped.read(), qPrintable(stripped.error()));
        QVERIFY2(!stripped.raw().contains(QStringLiteral("Exif.GPSInfo.GPSLatitude")),
                 qPrintable(QStringLiteral("%1 GPS 未摘除")
                                .arg(QFileInfo(path).fileName())));
    }
    qInfo() << "含 GPS 样本数：" << withGps;
    // 假绿护栏（评审 P0）：GPS 摘除是隐私红线，样本集必须真实覆盖
    QVERIFY2(withGps >= 1, "样张集无含 GPS 样本，GPS 摘除断言从未执行");
}

QTEST_MAIN(TestSamples)
#include "test_samples.moc"
