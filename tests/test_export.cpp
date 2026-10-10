// M4 导出与批量单测：
//   编码（4:4:4 落盘验证 / PSNR≥45dB 准入钉子 / PNG）
//   命名引擎（4 模式 + 占位符 + 序号 + 非法字符）
//   exportSingle 往返（同格式段搬运 / 跨格式重建 / 覆写语义）
//   健壮性（零字节/截断/假 PNG → 失败归约不崩溃）
//   冲突策略（ExportController 预扫分配：AutoRename/Skip/Overwrite）
// 交叉验证原则（M2 评审教训）：源图用 exiv2 写、自家链路读回——
// 避免自家 parse 自证循环。
#include <QtTest>

#include <QBuffer>
#include <QEventLoop>
#include <QTimer>

#include <cmath>

#include "controllers/ExportController.h"
#include "export/ExportEngine.h"
#include "export/JpegEncoder.h"
#include "metadata/FieldRegistry.h"
#include "platform/JpegSegmentIO.h"
#include "platform/PngChunkIO.h"
#include "render/BuiltinTemplates.h"

#include <exiv2/exiv2.hpp>

using namespace fxt;
using namespace fxt::Platform;

namespace {

// —— 测试图：水平渐变 + 高饱和色块（PSNR 对色度子采样最敏感的组合）——
QImage makeTestImage(const QSize &size)
{
    QImage img(size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y) {
        for (int x = 0; x < size.width(); ++x) {
            const int g = x * 255 / qMax(1, size.width() - 1);
            QRgb px;
            if (x > size.width() * 3 / 4 && y < size.height() / 4)
                px = qRgb(255, 0, 128);        // 品红色块（色度高频）
            else if (x < size.width() / 4 && y > size.height() * 3 / 4)
                px = qRgb(0, 220, 60);         // 绿色块
            else
                px = qRgb(g, 255 - g, (x + y) % 256);
            img.setPixel(x, y, px);
        }
    }
    return img;
}

// SOF0 采样因子提取：返回 Y 分量的 H*16+V（444→0x11，420→0x22）。
// 解析失败返回 0。仅扫 SOF0/1（基线/扩展顺序）
int sof0LumaSampling(const QByteArray &jpeg)
{
    for (int i = 2; i + 4 < jpeg.size();) {
        if (uchar(jpeg.at(i)) != 0xFF)
            return 0;
        const uchar marker = uchar(jpeg.at(i + 1));
        if (marker == 0xD8 || (marker >= 0xD0 && marker <= 0xD7)
            || marker == 0x01) {
            i += 2;
            continue;
        }
        const int len = (uchar(jpeg.at(i + 2)) << 8) | uchar(jpeg.at(i + 3));
        if (marker == 0xC0 || marker == 0xC1) {
            const int ncomp = uchar(jpeg.at(i + 9));
            if (ncomp < 1)
                return 0;
            // 分量结构：id(1) + H<<4|V(1) + Tq(1)，Y 在 i+10..12 的第二个字节
            return uchar(jpeg.at(i + 11));   // 首分量（Y）采样因子
        }
        i += 2 + len;
    }
    return 0;
}

// exiv2 写一张带 Make/Model/GPS/DateTime/PixelDimension 的源 JPEG
// （独立实现写路径；PixelDimension 模拟相机必写 tag——Orientation=6
// 的转向语义下记录的是转向前传感器尺寸）
QString writeExifSourceJpeg(const QString &path, const QImage &image,
                            uint16_t orientation)
{
    image.save(path, "jpeg");
    Exiv2::Image::UniquePtr img =
        Exiv2::ImageFactory::open(path.toStdString());
    if (!img.get())
        return {};
    img->readMetadata();
    Exiv2::ExifData &exif = img->exifData();
    exif["Exif.Image.Make"] = "OM Digital Solutions";
    exif["Exif.Image.Model"] = "OM-1";
    exif["Exif.Image.Orientation"] = orientation;
    exif["Exif.Image.Software"] = "SomeOtherTool 9.9";
    exif["Exif.Photo.DateTimeOriginal"] = "2026:05:01 08:30:00";
    exif["Exif.Photo.PixelXDimension"] =
        uint16_t(orientation == 6 ? image.height() : image.width());
    exif["Exif.Photo.PixelYDimension"] =
        uint16_t(orientation == 6 ? image.width() : image.height());
    exif["Exif.GPSInfo.GPSLatitude"] = "25/1 2/1 60000/10000";
    exif["Exif.GPSInfo.GPSLatitudeRef"] = "N";
    try {
        img->writeMetadata();
    } catch (const std::exception &) {
        return {};
    }
    return path;
}

// 造一份最小可导文档（真实文件 + 手填 meta）
PhotoDocument makeDoc(const QString &sourcePath, const QSize &px)
{
    PhotoDocument doc;
    doc.id = QUuid::createUuid();
    doc.sourcePath = sourcePath;
    doc.meta.pixelSize = px;
    doc.meta.format = QStringLiteral("jpeg");
    const auto spec = BuiltinTemplates::defaultSpec(QStringLiteral("bottom"));
    doc.templateState.templateId = QStringLiteral("bottom");
    doc.templateState.elements = spec.elements;
    return doc;
}

// resolver：fields 空表（布局数值不受影响），overrides 空
FieldValueResolver emptyResolver()
{
    return [](const QString &) { return QString(); };
}

} // namespace

class TestExport : public QObject
{
    Q_OBJECT

private slots:
    // 4:4:4 落盘验证（视觉无损预设的核心语义，Qt JPEG 插件做不到）
    void jpegEncodes444();
    // 平衡预设 4:2:0 落盘验证
    void jpegEncodes420();
    // R2 准入钉子：视觉无损 q100+444 → PSNR ≥45dB
    void visualLosslessPsnr();
    // PNG 输出无损
    void pngLossless();
    // 命名引擎：4 模式 + 序号补零 + 占位符 + 非法字符
    void namingModes();
    // 同格式往返：段搬运 + 覆写语义（Orientation=1/PixelDimension/DPI/Software）
    void jpegToJpegRoundTrip();
    // 跨格式：JPEG 源 → PNG 出（eXIf/iCCP/pHYs 重建路径）
    void jpegToPngRoundTrip();
    // 健壮性：零字节/截断/假 PNG → 失败归约，绝不崩溃
    void edgeInputsFailGracefully();
    // Orientation=1–8 全参数化：源转向 → 导出后一律覆写为 1（画布已转正）
    void allOrientationsExportToUpright();
    // ExportController 冲突策略 + 完成汇总
    void conflictPolicies();
    // 取消语义：立即取消不崩溃、finished 必达（时序宽松——小图可能全完成）
    void cancelDoesNotHang();
};

void TestExport::jpegEncodes444()
{
    const QImage img = makeTestImage({ 320, 240 });
    const QByteArray jpeg = JpegEncoder::encode(img, 100,
                                                JpegSubsampling::s444);
    QVERIFY(!jpeg.isEmpty());
    QCOMPARE(sof0LumaSampling(jpeg), 0x11);
}

void TestExport::jpegEncodes420()
{
    const QImage img = makeTestImage({ 320, 240 });
    const QByteArray jpeg = JpegEncoder::encode(img, 90,
                                                JpegSubsampling::s420);
    QVERIFY(!jpeg.isEmpty());
    QCOMPARE(sof0LumaSampling(jpeg), 0x22);
}

void TestExport::visualLosslessPsnr()
{
    const QImage img = makeTestImage({ 1024, 768 });
    const QByteArray jpeg = JpegEncoder::encode(img, 100,
                                                JpegSubsampling::s444);
    QVERIFY(!jpeg.isEmpty());
    QImage decoded;
    QVERIFY(decoded.loadFromData(jpeg));
    const QImage ref = img.convertToFormat(QImage::Format_ARGB32);
    const QImage out = decoded.convertToFormat(QImage::Format_ARGB32);
    QCOMPARE(out.size(), ref.size());

    // PSNR = 10*log10(255²/MSE)
    double mse = 0;
    for (int y = 0; y < ref.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(ref.constScanLine(y));
        const QRgb *b = reinterpret_cast<const QRgb *>(out.constScanLine(y));
        for (int x = 0; x < ref.width(); ++x) {
            mse += std::pow(qRed(a[x]) - qRed(b[x]), 2);
            mse += std::pow(qGreen(a[x]) - qGreen(b[x]), 2);
            mse += std::pow(qBlue(a[x]) - qBlue(b[x]), 2);
        }
    }
    mse /= ref.width() * ref.height() * 3.0;
    const double psnr = mse <= 0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    QVERIFY2(psnr >= 45.0,
             qPrintable(QStringLiteral("PSNR=%1dB 低于 45dB 准入线")
                            .arg(psnr, 0, 'f', 2)));
}

void TestExport::pngLossless()
{
    const QImage img = makeTestImage({ 320, 240 });
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    QVERIFY(img.save(&buffer, "png"));
    QImage decoded;
    QVERIFY(decoded.loadFromData(png));
    QCOMPARE(decoded.size(), img.size());
    QCOMPARE(decoded.convertToFormat(QImage::Format_ARGB32),
             img);   // PNG 无损：像素级一致
}

void TestExport::namingModes()
{
    PhotoDocument doc = makeDoc(QStringLiteral("Z:/in/DSC_2261.jpg"),
                                { 4000, 3000 });
    doc.meta.exif.model = QStringLiteral("OM-1");
    doc.meta.exif.fields.insert(QStringLiteral("dateTime"),
                                QStringLiteral("2026-05-01 08:30"));

    ExportOptions opt;
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 0),
             QStringLiteral("DSC_2261.jpg"));

    opt.naming = NamingMode::OriginalPlusIndex;
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 0),
             QStringLiteral("DSC_2261-0001.jpg"));
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 9),
             QStringLiteral("DSC_2261-0010.jpg"));
    opt.indexStart = 100;
    opt.indexDigits = 3;
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 0),
             QStringLiteral("DSC_2261-100.jpg"));

    opt = ExportOptions{};
    opt.naming = NamingMode::DatePlusOriginal;
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 0),
             QStringLiteral("20260501_DSC_2261.jpg"));

    opt.naming = NamingMode::CustomPattern;
    opt.customPattern = QStringLiteral("{date}_{model}_{name}_{index}");
    QCOMPARE(ExportEngine::buildFileName(doc, opt, 0),
             QStringLiteral("20260501_OM-1_DSC_2261_0001.jpg"));

    // 非法字符与尾点清理（Windows 保留集）
    PhotoDocument bad = makeDoc(QStringLiteral("Z:/in/a<b>:c?.jpg"),
                                { 4000, 3000 });
    const QString cleaned = ExportEngine::buildFileName(bad, opt, 0);
    QVERIFY(!cleaned.contains(QLatin1Char('<')));
    QVERIFY(!cleaned.contains(QLatin1Char(':')));
    QVERIFY(!cleaned.endsWith(QLatin1Char('.')));
}

void TestExport::jpegToJpegRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 源：exiv2 写入（独立实现）+ Orientation=6 竖拍（导出应覆写为 1）
    QImage source(320, 240, QImage::Format_ARGB32);
    source.fill(Qt::darkCyan);
    const QString srcPath = writeExifSourceJpeg(
        dir.filePath("src.jpg"), source, 6);
    QVERIFY(!srcPath.isEmpty());

    ExportOptions opt;
    opt.outputDir = dir.filePath("out");
    QVERIFY(QDir().mkpath(opt.outputDir));

    PhotoDocument doc = makeDoc(srcPath, source.size());
    ExportEngine::ItemRequest req;
    req.doc = doc;
    req.options = opt;
    req.outputPath = QDir(opt.outputDir).filePath("src.jpg");
    req.resolver = emptyResolver();

    const auto result = ExportEngine::exportSingle(req);
    QVERIFY2(result.ok, qPrintable(result.stage + ": " + result.error));
    QVERIFY(result.outputBytes > 0);

    // —— 回读断言（自家段解析 + exiv2 双通道）——
    QFile out(result.outputPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray bytes = out.readAll();

    // Exif TIFF 提取 → Orientation/PixelDimension/Software
    const QByteArray tiff = MetadataMuxer::exifTiffFromJpeg(bytes);
    QVERIFY(!tiff.isEmpty());
    Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(
        reinterpret_cast<const Exiv2::byte *>(tiff.constData()), tiff.size());
    QVERIFY(img.get() != nullptr);
    img->readMetadata();
    auto &exif = img->exifData();   // operator[] 非 const（缺失即插入）
    // Orientation 覆写为 1（画布已转正）
    QCOMPARE(exif["Exif.Image.Orientation"].value().toInt64(), qint64(1));
    // Make/Model 段搬运保真
    QCOMPARE(exif["Exif.Image.Make"].toString().c_str(), "OM Digital Solutions");
    QCOMPARE(exif["Exif.Image.Model"].toString().c_str(), "OM-1");
    // Software 同长度覆写（源有 Software="SomeOtherTool 9.9" 15 字节）
    const std::string sw = exif["Exif.Image.Software"].toString();
    QVERIFY2(sw.rfind("FrameXT", 0) == 0,
             qPrintable(QStringLiteral("Software=%1").arg(sw.c_str())));

    // GPS 默认保留（stripGps=false，E2 开关关闭时不摘除）
    QVERIFY(!exif["Exif.GPSInfo.GPSLatitude"].toString().empty());

    // 输出为模板画布（几何由 test_layout/golden 套件管辖；此处只钉
    // 「PixelDimension 覆写与实际输出尺寸一致」的导出语义）
    QImage canvas;
    QVERIFY(canvas.loadFromData(bytes));
    QVERIFY(!canvas.size().isEmpty());
    QCOMPARE(exif["Exif.Photo.PixelXDimension"].value().toInt64(),
             qint64(canvas.width()));
    QCOMPARE(exif["Exif.Photo.PixelYDimension"].value().toInt64(),
             qint64(canvas.height()));
}

void TestExport::jpegToPngRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    QImage source(200, 150, QImage::Format_ARGB32);
    source.fill(Qt::darkYellow);
    const QString srcPath = writeExifSourceJpeg(dir.filePath("src.jpg"),
                                                source, 1);
    QVERIFY(!srcPath.isEmpty());

    ExportOptions opt;
    opt.outputDir = dir.filePath("out");
    opt.format = OutputFormat::Png;
    QVERIFY(QDir().mkpath(opt.outputDir));

    PhotoDocument doc = makeDoc(srcPath, source.size());
    ExportEngine::ItemRequest req;
    req.doc = doc;
    req.options = opt;
    req.outputPath = QDir(opt.outputDir).filePath("src.png");
    req.resolver = emptyResolver();

    const auto result = ExportEngine::exportSingle(req);
    QVERIFY2(result.ok, qPrintable(result.stage + ": " + result.error));

    QFile out(result.outputPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    const QByteArray bytes = out.readAll();

    // eXIf（跨格式重建路径）+ ICC 可选 + pHYs 必在
    const QByteArray tiff = PngChunkIO::chunkData(bytes, "eXIf");
    QVERIFY(!tiff.isEmpty());
    Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(
        reinterpret_cast<const Exiv2::byte *>(tiff.constData()), tiff.size());
    QVERIFY(img.get() != nullptr);
    img->readMetadata();
    QCOMPARE(img->exifData()["Exif.Image.Orientation"].value().toInt64(),
             qint64(1));
    QCOMPARE(img->exifData()["Exif.Image.Model"].toString().c_str(), "OM-1");
    QVERIFY(!PngChunkIO::chunkData(bytes, "pHYs").isEmpty());
}

void TestExport::edgeInputsFailGracefully()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    // 零字节文件
    {
        QFile z(dir.filePath("zero.jpg"));
        QVERIFY(z.open(QIODevice::WriteOnly));
    }
    // 截断 JPEG（真 JPEG 头 + 半截数据）
    {
        QImage img(64, 64, QImage::Format_ARGB32);
        img.fill(Qt::red);
        QByteArray full;
        QBuffer b(&full);
        b.open(QIODevice::WriteOnly);
        img.save(&b, "jpeg");
        QFile t(dir.filePath("trunc.jpg"));
        QVERIFY(t.open(QIODevice::WriteOnly));
        t.write(full.left(full.size() / 3));
    }
    // 假 PNG（PNG 签名 + 垃圾）
    {
        QFile f(dir.filePath("fake.png"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArray("\x89PNG\r\n\x1a\n", 8) + QByteArray(64, 'x'));
    }

    for (const char *name : { "zero.jpg", "trunc.jpg", "fake.png" }) {
        ExportOptions opt;
        opt.outputDir = dir.filePath("out");
        QVERIFY(QDir().mkpath(opt.outputDir));
        PhotoDocument doc = makeDoc(dir.filePath(name), { 64, 64 });
        ExportEngine::ItemRequest req;
        req.doc = doc;
        req.options = opt;
        req.outputPath = QDir(opt.outputDir).filePath(
            QStringLiteral("o_%1").arg(name));
        req.resolver = emptyResolver();
        const auto result = ExportEngine::exportSingle(req);
        QVERIFY2(!result.ok, qPrintable(
                     QStringLiteral("%1 不应导出成功").arg(name)));
        QVERIFY2(!result.stage.isEmpty(),
                 "失败必须带阶段标注（失败清单/诊断包依赖）");
        QVERIFY(!result.error.isEmpty());
    }
}

void TestExport::allOrientationsExportToUpright()
{
    // R 样本集补缺（原版「竖版识别为横版」bug 的导出侧钉子）：
    // Orientation=1..8 全参数化——导出画布已含转向修正，
    // Exif 必须统一覆写为 1，绝不允许源值透传造成查看器二次旋转
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    for (uint16_t orientation = 1; orientation <= 8; ++orientation) {
        QImage source(120, 90, QImage::Format_ARGB32);
        source.fill(Qt::magenta);
        const QString srcPath = writeExifSourceJpeg(
            dir.filePath(QStringLiteral("o%1.jpg").arg(orientation)),
            source, orientation);
        QVERIFY(!srcPath.isEmpty());

        ExportOptions opt;
        opt.outputDir = dir.filePath(QStringLiteral("out%1").arg(orientation));
        QVERIFY(QDir().mkpath(opt.outputDir));
        PhotoDocument doc = makeDoc(srcPath, source.size());
        ExportEngine::ItemRequest req;
        req.doc = doc;
        req.options = opt;
        req.outputPath = QDir(opt.outputDir).filePath("o.jpg");
        req.resolver = emptyResolver();

        const auto result = ExportEngine::exportSingle(req);
        QVERIFY2(result.ok,
                 qPrintable(QStringLiteral("Orientation=%1: %2")
                                .arg(orientation)
                                .arg(result.error)));

        QFile out(result.outputPath);
        QVERIFY(out.open(QIODevice::ReadOnly));
        const QByteArray tiff =
            MetadataMuxer::exifTiffFromJpeg(out.readAll());
        QVERIFY(!tiff.isEmpty());
        Exiv2::Image::UniquePtr img = Exiv2::ImageFactory::open(
            reinterpret_cast<const Exiv2::byte *>(tiff.constData()),
            tiff.size());
        QVERIFY(img.get() != nullptr);
        img->readMetadata();
        QCOMPARE(img->exifData()["Exif.Image.Orientation"].value().toInt64(),
                 qint64(1));
    }
}

void TestExport::conflictPolicies()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage img = makeTestImage({ 160, 120 });

    // 三份不同内容源、同基名（模拟不同目录同名导入）
    QList<PhotoDocument> docs;
    QList<FieldValueResolver> resolvers;
    QList<QImage> logos;
    QList<QPair<const char *, QRgb>> cases = {
        { "a", qRgb(255, 0, 0) }, { "b", qRgb(0, 255, 0) },
        { "c", qRgb(0, 0, 255) }
    };
    for (const auto &c : cases) {
        const QString p = dir.filePath(QStringLiteral("%1.jpg")
                                           .arg(c.first));
        QImage im(img.size(), QImage::Format_ARGB32);
        im.fill(c.second);
        im.save(p, "jpeg");
        docs.append(makeDoc(p, im.size()));
        resolvers.append(emptyResolver());
        logos.append(QImage());
    }

    ExportOptions opt;
    opt.outputDir = dir.filePath("out");
    opt.naming = NamingMode::OriginalPlusIndex;
    QVERIFY(QDir().mkpath(opt.outputDir));

    // 预置同名文件（AutoRename 应让位为 -0002 起）
    QImage(img).save(QDir(opt.outputDir).filePath("a-0001.jpg"), "jpeg");

    ExportController ctl;
    QEventLoop loop;
    QList<ExportEngine::ItemResult> results;
    bool canceledFlag = true;
    QObject::connect(
        &ctl, &ExportController::finished, &loop,
        [&](const QList<ExportEngine::ItemResult> &r, bool canceled,
            qint64) {
            results = r;
            canceledFlag = canceled;
            loop.quit();
        });
    QTimer::singleShot(20000, &loop, &QEventLoop::quit);
    ctl.start(docs, resolvers, logos, opt);
    loop.exec();

    QVERIFY(!canceledFlag);
    QCOMPARE(results.size(), 3);
    int okCount = 0;
    for (const auto &r : results)
        okCount += r.ok ? 1 : 0;
    QCOMPARE(okCount, 3);
    // a-0001.jpg 预置文件未被覆盖——首张让位为 a-0001(1).jpg（§5.3
    // 自动重命名语义 = 括号序号，非批量序号 -NNNN）
    QVERIFY(QFileInfo::exists(QDir(opt.outputDir).filePath("a-0001.jpg")));
    QVERIFY(QFileInfo::exists(QDir(opt.outputDir).filePath("a-0001(1).jpg")));
    QVERIFY(QFileInfo::exists(QDir(opt.outputDir).filePath("b-0001.jpg")));

    // —— Skip：命中同名即跳过 ——
    opt.conflict = ConflictPolicy::Skip;
    ExportController skipCtl;
    QEventLoop loop2;
    QList<ExportEngine::ItemResult> skipResults;
    QObject::connect(
        &skipCtl, &ExportController::finished, &loop2,
        [&](const QList<ExportEngine::ItemResult> &r, bool, qint64) {
            skipResults = r;
            loop2.quit();
        });
    QTimer::singleShot(20000, &loop2, &QEventLoop::quit);
    skipCtl.start(docs.mid(0, 1), resolvers.mid(0, 1), logos.mid(0, 1), opt);
    loop2.exec();
    QCOMPARE(skipResults.size(), 1);
    QVERIFY(skipResults.first().skipped);   // a-0001.jpg 已存在 → 跳过

    // —— Overwrite：直接覆盖 ——
    opt.conflict = ConflictPolicy::Overwrite;
    const qint64 before = QFileInfo(QDir(opt.outputDir).filePath("a-0001.jpg"))
                              .size();
    ExportController overCtl;
    QEventLoop loop3;
    QList<ExportEngine::ItemResult> overResults;
    QObject::connect(
        &overCtl, &ExportController::finished, &loop3,
        [&](const QList<ExportEngine::ItemResult> &r, bool, qint64) {
            overResults = r;
            loop3.quit();
        });
    QTimer::singleShot(20000, &loop3, &QEventLoop::quit);
    overCtl.start(docs.mid(0, 1), resolvers.mid(0, 1), logos.mid(0, 1), opt);
    loop3.exec();
    QCOMPARE(overResults.size(), 1);
    QVERIFY(overResults.first().ok);
    QVERIFY(QFileInfo(QDir(opt.outputDir).filePath("a-0001.jpg")).size()
            != before);   // 覆盖发生（内容变化——新内容带边框画布）
}

void TestExport::cancelDoesNotHang()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QList<PhotoDocument> docs;
    QList<FieldValueResolver> resolvers;
    QList<QImage> logos;
    for (int i = 0; i < 4; ++i) {
        const QString p = dir.filePath(QStringLiteral("c%1.jpg").arg(i));
        QImage im(1600, 1200, QImage::Format_ARGB32);
        im.fill(Qt::gray);
        im.save(p, "jpeg");
        docs.append(makeDoc(p, im.size()));
        resolvers.append(emptyResolver());
        logos.append(QImage());
    }
    ExportOptions opt;
    opt.outputDir = dir.filePath("out");
    QVERIFY(QDir().mkpath(opt.outputDir));

    ExportController ctl;
    QEventLoop loop;
    bool gotFinished = false;
    QObject::connect(&ctl, &ExportController::finished, &loop,
                     [&](const QList<ExportEngine::ItemResult> &, bool,
                         qint64) {
                         gotFinished = true;
                         loop.quit();
                     });
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    ctl.start(docs, resolvers, logos, opt);
    ctl.cancel();   // 立即取消（时序宽松：可能 0..4 张完成）
    loop.exec();
    QVERIFY(gotFinished);   // finished 必达（不悬挂）；文件系统无半写文件
}

QTEST_MAIN(TestExport)
#include "test_export.moc"
