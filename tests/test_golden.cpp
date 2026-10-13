// M1.6 黄金图测试（03-开发计划 1.6）：
// 3 内置模板 × 3 尺寸（横 900×600 / 竖 600×900 / 方 800×800）基线 diff。
// 约定：
//  - 基准图位于 tests/golden/<template>_<WxH>.png（源码树内，随仓库入库）
//  - 首次运行自动生成基准并 SKIP，需人工目检后提交（G1 质量门）
//  - 文字渲染用随包嵌入字体（思源黑体，qrc 挂载）：系统字体光栅化会随
//    Windows 字体缓存/更新漂移（2026-10-13 实证：微软雅黑过夜微变导致
//    9 基准全红、数千超差点），嵌入字体让本测试跨机跨日可复现，M6 进
//    CI 的前提
#include <QFontDatabase>
#include <QImageWriter>
#include <QPainter>
#include <QTemporaryDir>
#include <QtTest>

#include "render/BuiltinTemplates.h"
#include "render/RenderPipeline.h"

using namespace fxt;
using namespace fxt::BuiltinTemplates;

namespace {

constexpr int kPreviewMaxEdge = 2048;   // 与 PreviewCanvas 预览缩放规则一致

// 黄金图专用字体：随包嵌入，与系统环境解耦（进程内必须先注册资源）
const QFont &goldenFont()
{
    static const QFont s_font = [] {
        const int id = QFontDatabase::addApplicationFont(
            QStringLiteral(":/fonts/SourceHanSansSC-Regular.otf"));
        const QStringList families = QFontDatabase::applicationFontFamilies(id);
        return QFont(families.value(0, QStringLiteral("Source Han Sans SC")));
    }();
    return s_font;
}

QString goldenDir()
{
    return QStringLiteral(GOLDEN_SOURCE_DIR) + QStringLiteral("/golden");
}

// 固定内容桩（确定性：文字/Logo/颜色全部固定）
FieldValueResolver goldenResolver()
{
    return [](const QString &key) {
        if (key == QLatin1String("make"))
            return QStringLiteral("Canon");
        if (key == QLatin1String("lens"))
            return QStringLiteral("RF 24-70mm F2.8");
        if (key == QLatin1String("paramString"))
            return QStringLiteral("42mm · f/5.6 · 1/160s · ISO 140");
        if (key == QLatin1String("dateTime"))
            return QStringLiteral("2026-10-10 12:00");
        return QString();
    };
}

LogoResolver goldenLogoResolver()
{
    return [](const QString &) {
        QImage img(300, 150, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setPen(Qt::black);
        p.setFont(QFont(goldenFont().family(), 40, QFont::Bold));
        p.drawText(img.rect(), Qt::AlignCenter, QStringLiteral("BRAND"));
        p.end();
        return img;
    };
}

QImage renderGolden(const QString &templateId, const QSize &photoSize)
{
    QTemporaryDir dir;
    Q_ASSERT(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("golden_src.jpg"));

    QImage photo(photoSize, QImage::Format_RGB32);
    photo.fill(QColor(96, 112, 128));
    QImageWriter writer(path, "jpeg");
    writer.setQuality(95);
    writer.write(photo);

    TemplateState state;
    state.templateId = templateId;
    // 按模板取 spec：blur 模板为白字（与产品视觉一致），bottom/around 黑字
    state.elements = defaultSpec(templateId).elements;
    // 元素默认字体为系统字体（Element.h "Microsoft YaHei UI"）——黄金图
    // 覆写为随包嵌入字体，根除系统字体光栅化漂移（见文件头说明）
    for (fxt::Element &el : state.elements)
        el.style.fontFamily = goldenFont().family();

    RenderRequest req;
    req.photoId = QUuid::createUuid();
    req.filePath = path;
    req.templateState = state;
    req.purpose = RenderPurpose::Export;
    const qreal maxEdge = qMax(photoSize.width(), photoSize.height());
    req.scale = qMin(1.0, qreal(kPreviewMaxEdge) / maxEdge);

    RenderCacheManager cache;
    const auto result = RenderPipeline::run(req, goldenResolver(), goldenLogoResolver(),
                                            &cache);
    Q_ASSERT(result.ok);
    return result.image;
}

void checkGolden(const QString &templateId, const QSize &photoSize)
{
    const QImage actual = renderGolden(templateId, photoSize);
    QVERIFY(!actual.isNull());

    const QString baseName = QStringLiteral("%1_%2x%3.png")
                                 .arg(templateId)
                                 .arg(photoSize.width())
                                 .arg(photoSize.height());
    const QString goldenPath = goldenDir() + QLatin1Char('/') + baseName;

    if (!QFileInfo::exists(goldenPath)) {
        // FXT_GOLDEN_STRICT=1（CI/回归跑）：基准缺失直接失败，禁止静默
        // 生成掩盖"基准被误删/未拉取"的假绿
        if (qEnvironmentVariableIsSet("FXT_GOLDEN_STRICT")) {
            QFAIL(qPrintable(QStringLiteral("FXT_GOLDEN_STRICT：基准图缺失 %1，"
                                            "拒绝静默生成（先人工目检入库）")
                                 .arg(baseName)));
        }
        QVERIFY(QDir().mkpath(goldenDir()));
        QVERIFY(actual.save(goldenPath));
        QSKIP(qPrintable(QStringLiteral("基准图首次生成：%1（请人工目检后入库）")
                             .arg(baseName)));
    }

    QImage golden(goldenPath);
    QVERIFY2(!golden.isNull(), qPrintable(QStringLiteral("基准图读取失败：%1").arg(goldenPath)));
    golden = golden.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    // 与基准同格式化，保证逐字节对比与 constScanLine<QRgb> 访问有效
    const QImage actualPre = actual.convertToFormat(QImage::Format_ARGB32_Premultiplied);

    QVERIFY2(actual.size() == golden.size(),
             qPrintable(QStringLiteral("尺寸不一致：%1 实际 %2x%3 vs 基准 %4x%5")
                            .arg(baseName)
                            .arg(actual.width())
                            .arg(actual.height())
                            .arg(golden.width())
                            .arg(golden.height())));

    // 逐像素 ±2/通道 容差对比：吸收 Windows 文字光栅化跨进程的 ±1~3 抗锯齿
    // 抖动（实测噪声模式：信息带文字边缘个别像素，且每次进程抖动点不同）。
    // 通道容差之外仍允许极少量超差点（kMaxOverPixels）——光栅噪声实测 1~2 个
    // 像素；布局/颜色/字号级回归会产生成百上千超差点，不会漏检。
    constexpr int kTolerance = 2;
    constexpr int kMaxOverPixels = 8;
    int jitterCount = 0;    // 容差内抖动像素（记录量级，不作失败依据）
    int overCount = 0;      // 超容差像素（超过 kMaxOverPixels 才失败）
    int firstOverX = -1;
    int firstOverY = -1;
    for (int y = 0; y < golden.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(actualPre.constScanLine(y));
        const QRgb *rg = reinterpret_cast<const QRgb *>(golden.constScanLine(y));
        for (int x = 0; x < golden.width(); ++x) {
            const QRgb pa = ra[x];
            const QRgb pg = rg[x];
            if (pa == pg)
                continue;
            ++jitterCount;
            if (qAbs(qRed(pa) - qRed(pg)) > kTolerance
                || qAbs(qGreen(pa) - qGreen(pg)) > kTolerance
                || qAbs(qBlue(pa) - qBlue(pg)) > kTolerance
                || qAbs(qAlpha(pa) - qAlpha(pg)) > kTolerance) {
                ++overCount;
                if (firstOverX < 0) {
                    firstOverX = x;
                    firstOverY = y;
                }
            }
        }
    }

    if (overCount <= kMaxOverPixels)
        return;

    // diff 失败：落盘实际图与差异掩码，供定位差异来源
    actual.save(QDir::temp().absoluteFilePath(
        QStringLiteral("golden_actual_%1").arg(baseName)));
    QImage diffMask(golden.size(), QImage::Format_Grayscale8);
    diffMask.fill(Qt::black);
    const int total = golden.width() * golden.height();
    for (int y = 0; y < golden.height(); ++y) {
        const QRgb *ra = reinterpret_cast<const QRgb *>(actualPre.constScanLine(y));
        const QRgb *rg = reinterpret_cast<const QRgb *>(golden.constScanLine(y));
        for (int x = 0; x < golden.width(); ++x) {
            const QRgb pa = ra[x];
            const QRgb pg = rg[x];
            if (pa == pg)
                continue;
            const bool over = qAbs(qRed(pa) - qRed(pg)) > kTolerance
                || qAbs(qGreen(pa) - qGreen(pg)) > kTolerance
                || qAbs(qBlue(pa) - qBlue(pg)) > kTolerance
                || qAbs(qAlpha(pa) - qAlpha(pg)) > kTolerance;
            if (over)
                diffMask.setPixel(x, y, 255);
        }
    }
    diffMask.save(QDir::temp().absoluteFilePath(
        QStringLiteral("golden_diff_%1").arg(baseName)));

    QFAIL(qPrintable(QStringLiteral("黄金图 diff：%1 超容差像素 %2/%3，容差内抖动 %4，"
                                    "首个超差点 (%5,%6)；实际图与差异掩码已存 %TEMP%"
                                    "（golden_actual_/golden_diff_）")
                         .arg(baseName)
                         .arg(overCount)
                         .arg(total)
                         .arg(jitterCount)
                         .arg(firstOverX)
                         .arg(firstOverY)));
}

} // namespace

class TestGolden : public QObject
{
    Q_OBJECT

private slots:
    void goldenBottomLandscape() const;
    void goldenBottomPortrait() const;
    void goldenAroundLandscape() const;
    void goldenAroundPortrait() const;
    void goldenBlurLandscape() const;
    void goldenBlurPortrait() const;
    void goldenBottomSquare() const;
    void goldenAroundSquare() const;
    void goldenBlurSquare() const;
};

void TestGolden::goldenBottomLandscape() const
{
    checkGolden(QStringLiteral("bottom"), {900, 600});
}
void TestGolden::goldenBottomPortrait() const
{
    checkGolden(QStringLiteral("bottom"), {600, 900});
}
void TestGolden::goldenAroundLandscape() const
{
    checkGolden(QStringLiteral("around"), {900, 600});
}
void TestGolden::goldenAroundPortrait() const
{
    checkGolden(QStringLiteral("around"), {600, 900});
}
void TestGolden::goldenBlurLandscape() const
{
    checkGolden(QStringLiteral("blur"), {900, 600});
}
void TestGolden::goldenBlurPortrait() const
{
    checkGolden(QStringLiteral("blur"), {600, 900});
}
void TestGolden::goldenBottomSquare() const
{
    checkGolden(QStringLiteral("bottom"), {800, 800});
}
void TestGolden::goldenAroundSquare() const
{
    checkGolden(QStringLiteral("around"), {800, 800});
}
void TestGolden::goldenBlurSquare() const
{
    checkGolden(QStringLiteral("blur"), {800, 800});
}

QTEST_MAIN(TestGolden)
#include "test_golden.moc"
