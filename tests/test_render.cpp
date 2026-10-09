// 渲染管线 S1-S4 单测（03-开发计划 1.2）：
// 画布规则数值断言（对齐 §4.4 常量表）、背景/合成像素级断言、解码往返
#include <QFile>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

#include "render/BackdropRenderer.h"
#include "render/CanvasGeometry.h"
#include "render/PhotoDecoder.h"

using namespace fxt;
using namespace fxt::BuiltinTemplate;

namespace {

// 生成确定性测试图（横向渐变，模糊/合成断言可用像素坐标推算）
QImage makeTestPhoto(const QSize &size)
{
    QImage img(size, QImage::Format_RGB32);
    for (int y = 0; y < size.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < size.width(); ++x)
            line[x] = qRgb(x % 256, y % 256, 128);
    }
    return img;
}

// 绘制管线存在 RGB32→Premultiplied 转换的 ±1 分量舍入
bool nearlyEqual(const QColor &a, const QColor &b, int tol = 1)
{
    return qAbs(a.red() - b.red()) <= tol && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol && qAbs(a.alpha() - b.alpha()) <= tol;
}

} // namespace

class TestRender : public QObject
{
    Q_OBJECT

private slots:
    void canvasPlanBottomLandscape() const;
    void canvasPlanBottomPortrait() const;
    void canvasPlanAround() const;
    void canvasPlanAroundPortrait() const;
    void canvasPlanBlur() const;
    void canvasPlanEdgeSizes() const;
    void backdropWhiteFill() const;
    void blurCompositeHasRoundedCorners() const;
    void blurExtremeAspectRatioKeepsBrightness() const;
    void decodeRoundTrip() const;
};

void TestRender::canvasPlanBottomLandscape() const
{
    // 横图 6000×4000：exH = 0.09×6000 = 540
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBottom), {6000, 4000});
    QCOMPARE(p.canvasSize, QSize(6000, 4540));
    QCOMPARE(p.photoRect, QRect(0, 0, 6000, 4000));
    QCOMPARE(p.exBandHeight, 540.0);
    QCOMPARE(p.roundedPhoto, false);
}

void TestRender::canvasPlanBottomPortrait() const
{
    // 竖图 4000×6000：exH = 0.10×4000 = 400
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBottom), {4000, 6000});
    QCOMPARE(p.canvasSize, QSize(4000, 6400));
    QCOMPARE(p.photoRect, QRect(0, 0, 4000, 6000));
    QCOMPARE(p.exBandHeight, 400.0);
}

void TestRender::canvasPlanAround() const
{
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kAround), {6000, 4000});
    QCOMPARE(p.canvasSize, QSize(6240, 4660));   // W×1.04 ; H + exH(540) + 2%W(120)
    QCOMPARE(p.photoRect, QRect(120, 120, 6000, 4000));
    QCOMPARE(p.exBandHeight, 540.0);
}

void TestRender::canvasPlanAroundPortrait() const
{
    // 竖图 around：exH = 0.10W = 400，inset = 2%W = 80，
    // 画布宽由 inset 反推（w + 2×inset）保证左右白边对称
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kAround), {4000, 6000});
    QCOMPARE(p.canvasSize, QSize(4160, 6480));   // H + exH(400) + 2%W(80)
    QCOMPARE(p.photoRect, QRect(80, 80, 4000, 6000));
    QCOMPARE(p.exBandHeight, 400.0);
}

void TestRender::canvasPlanEdgeSizes() const
{
    // 素数宽（取整不对称场景）：canvasW = w + 2×inset，左右白边必须等宽
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kAround), {1013, 700});
    const int inset = p.photoRect.left();
    QCOMPARE(p.canvasSize.width(), 1013 + inset * 2);
    QCOMPARE(p.photoRect.width(), 1013);
    QCOMPARE(p.canvasSize.width() - p.photoRect.right() - 1, inset);   // 右白边 = 左白边

    // 1×1：不崩溃且尺寸有效
    const CanvasPlan tiny = CanvasGeometry::plan(QLatin1String(kBottom), {1, 1});
    QVERIFY(!tiny.canvasSize.isEmpty());

    // 无效尺寸 → 空 plan（由 RenderPipeline 兜底报错）
    const CanvasPlan bad = CanvasGeometry::plan(QLatin1String(kBottom), {0, 0});
    QVERIFY(bad.canvasSize.isEmpty());

    // 未知模板 → 空 plan
    const CanvasPlan unknown = CanvasGeometry::plan(QStringLiteral("custom"), {6000, 4000});
    QVERIFY(unknown.canvasSize.isEmpty());
}

void TestRender::canvasPlanBlur() const
{
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBlur), {6000, 4000});
    // T = exH/2 = 270 ; B = exH + 1%H = 540 + 40 = 580
    QCOMPARE(p.canvasSize, QSize(6960, 4850));
    QCOMPARE(p.photoRect, QRect(480, 270, 6000, 4000));
    QCOMPARE(p.cornerRadius, 100.0);             // W/60
    QCOMPARE(p.roundedPhoto, true);
}

void TestRender::backdropWhiteFill() const
{
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBottom), {64, 48});
    const QImage photo = makeTestPhoto(p.photoRect.size());
    const QImage bg = BackdropRenderer::backdrop(p, QLatin1String(kBottom), photo);

    QCOMPARE(bg.size(), p.canvasSize);
    QCOMPARE(bg.format(), QImage::Format_ARGB32_Premultiplied);
    QCOMPARE(bg.pixelColor(0, 0), QColor(Qt::white));
    QCOMPARE(bg.pixelColor(32, 40), QColor(Qt::white));
}

void TestRender::blurCompositeHasRoundedCorners() const
{
    const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBlur), {64, 48});
    const QImage photo = makeTestPhoto(p.photoRect.size());

    QImage canvas = BackdropRenderer::backdrop(p, QLatin1String(kBlur), photo);
    QCOMPARE(canvas.size(), p.canvasSize);
    BackdropRenderer::composite(canvas, p, photo);

    // blur 背景铺满（非白底）后，原图圆角外的四角应保持背景而非被原图覆盖。
    // 验证方式：角像素与原图对应像素不同（原图左上角是 (0,0) 纯黑），
    // 且 photoRect 内中心区域与原图一致（合成发生）。
    const QPoint center = p.photoRect.center();
    QVERIFY2(nearlyEqual(canvas.pixelColor(center), photo.pixelColor(32, 24)),
             "合成后 photoRect 中心应与原图一致（±1 舍入容差）");
    // 圆角半径 = 64/60 ≈ 1px，太小难采样；用放大半径验证蒙版机制本身
    CanvasPlan big = p;
    big.cornerRadius = 12.0;
    QImage canvas2 = BackdropRenderer::backdrop(big, QLatin1String(kBlur), photo);
    BackdropRenderer::composite(canvas2, big, photo);
    const QColor corner = canvas2.pixelColor(big.photoRect.topLeft());
    QVERIFY(corner != photo.pixelColor(0, 0));   // 圆角外露出了模糊背景
}

namespace {
// 全图每通道均值（blur 亮度守恒断言用）
QColor meanColor(const QImage &image)
{
    qint64 r = 0, g = 0, b = 0;
    for (int y = 0; y < image.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            r += qRed(line[x]);
            g += qGreen(line[x]);
            b += qBlue(line[x]);
        }
    }
    const qint64 n = qint64(image.width()) * image.height();
    return QColor(int(r / n), int(g / n), int(b / n));
}
} // namespace

void TestRender::blurExtremeAspectRatioKeepsBrightness() const
{
    // 极端宽高比（长截图/全景图）：blur 滑窗 window 不得超过小图短边，
    // 否则除数失配背景整体变暗约 1/9。断言：blur 后全图均值与原图均值
    // 每通道偏差 ≤ 8（box blur 均值保持 + 缩放/舍入余量）
    for (const QSize &size : { QSize(10000, 100), QSize(100, 10000) }) {
        const CanvasPlan p = CanvasGeometry::plan(QLatin1String(kBlur), size);
        const QImage photo = makeTestPhoto(p.photoRect.size());
        const QImage bg = BackdropRenderer::backdrop(p, QLatin1String(kBlur), photo);
        QVERIFY(!bg.isNull());

        const QColor srcMean = meanColor(photo);
        const QColor bgMean = meanColor(bg);
        QVERIFY2(qAbs(srcMean.red() - bgMean.red()) <= 8
                     && qAbs(srcMean.green() - bgMean.green()) <= 8
                     && qAbs(srcMean.blue() - bgMean.blue()) <= 8,
                 qPrintable(QStringLiteral("%1x%2 blur 后亮度失守：src(%3,%4,%5) bg(%6,%7,%8)")
                                .arg(size.width())
                                .arg(size.height())
                                .arg(srcMean.red())
                                .arg(srcMean.green())
                                .arg(srcMean.blue())
                                .arg(bgMean.red())
                                .arg(bgMean.green())
                                .arg(bgMean.blue())));
    }
}

void TestRender::decodeRoundTrip() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("test.png");

    const QImage source = makeTestPhoto({64, 48});
    QVERIFY(QImageWriter(path, "png").write(source));

    const auto full = PhotoDecoder::decode(path);
    QVERIFY(full.ok);
    QCOMPARE(full.image.size(), QSize(64, 48));
    QCOMPARE(full.originalSize, QSize(64, 48));
    QCOMPARE(full.format, QStringLiteral("png"));

    // 预览下采样：scale=0.5 → 32×24，originalSize 仍记录原尺寸
    const auto half = PhotoDecoder::decode(path, 0.5);
    QVERIFY(half.ok);
    QCOMPARE(half.image.size(), QSize(32, 24));
    QCOMPARE(half.originalSize, QSize(64, 48));

    // 坏文件健壮性：不崩溃、ok=false、有错误信息
    const QString bad = dir.filePath("bad.png");
    QFile badFile(bad);
    QVERIFY(badFile.open(QIODevice::WriteOnly));
    badFile.write("not an image");
    badFile.close();
    const auto broken = PhotoDecoder::decode(bad);
    QVERIFY(!broken.ok);
    QVERIFY(!broken.error.isEmpty());
}

QTEST_MAIN(TestRender)
#include "test_render.moc"
