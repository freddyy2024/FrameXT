// M1.5 RenderPipeline + RenderCacheManager 单测：
// 端到端出图、改文字命中背景缓存（<80ms 指标根基）、LRU 容量淘汰
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>
#include <QtTest>

#include "model/TemplateState.h"
#include "render/BuiltinTemplates.h"
#include "render/RenderPipeline.h"

using namespace fxt;
using namespace fxt::BuiltinTemplates;

namespace {

// 写一张测试 JPEG 并返回路径
QString writeTestJpeg(const QTemporaryDir &dir, const QSize &size)
{
    QImage img(size, QImage::Format_RGB32);
    img.fill(QColor(120, 130, 140));
    const QString path = dir.filePath(QStringLiteral("test_%1x%2.jpg")
                                          .arg(size.width())
                                          .arg(size.height()));
    QImageWriter writer(path, "jpeg");
    writer.setQuality(92);
    writer.write(img);
    return path;
}

// 固定字段桩：make/lens/paramString/dateTime 有值
FieldValueResolver stubResolver()
{
    return [](const QString &key) {
        if (key == QLatin1String("make"))
            return QStringLiteral("Canon");
        if (key == QLatin1String("lens"))
            return QStringLiteral("RF 24-70mm");
        if (key == QLatin1String("paramString"))
            return QStringLiteral("42mm · f/5.6 · 1/160s · ISO 140");
        if (key == QLatin1String("dateTime"))
            return QStringLiteral("2026-10-10");
        return QString();
    };
}

LogoResolver stubLogoResolver()
{
    return [](const QString &) {
        QImage img(200, 100, QImage::Format_ARGB32);
        img.fill(QColor(200, 40, 40));
        return img;
    };
}

RenderRequest makeRequest(const QUuid &photoId, const QString &path,
                          const TemplateState &state,
                          RenderPurpose purpose = RenderPurpose::Preview)
{
    RenderRequest req;
    req.photoId = photoId;
    req.filePath = path;
    req.templateState = state;
    req.scale = 1.0;
    req.purpose = purpose;
    return req;
}

} // namespace

class TestPipeline : public QObject
{
    Q_OBJECT

private slots:
    void endToEndBottom() const;
    void textChangeHitsBackdropCache() const;
    void lruEviction() const;
    void decodeFailurePropagates() const;
    void unknownTemplateFails() const;
    void allFieldsEmptyStillRenders() const;
    void exportDoesNotPollutePreviewCache() const;
    void cacheHitNotPollutedByComposite() const;
};

void TestPipeline::endToEndBottom() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    state.elements = defaultSpec().elements;

    RenderCacheManager cache;
    const auto result = RenderPipeline::run(makeRequest(QUuid::createUuid(), path, state),
                                            stubResolver(), stubLogoResolver(), &cache);
    QVERIFY2(result.ok, qPrintable(result.error));
    // 画布 = H + 0.09W = 480 + 57.6 → qRound = 538（精确值，防回归漂移）
    QCOMPARE(result.image.width(), 640);
    QCOMPARE(result.image.height(), 538);
    QVERIFY(result.image.pixelColor(5, 5) != QColor(Qt::white));   // 原图已合成

    // 信息带应含绘制的黑色文字（品牌文本区域存在非白像素）
    bool foundDark = false;
    for (int y = 481; y < result.image.height() && !foundDark; ++y)
        for (int x = 0; x < 300; ++x)
            if (result.image.pixel(x, y) != QColor(Qt::white).rgba()) {
                foundDark = true;
                break;
            }
    QVERIFY2(foundDark, "信息带未绘制水印文本");
}

void TestPipeline::textChangeHitsBackdropCache() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    state.elements = defaultSpec().elements;
    const QUuid photoId = QUuid::createUuid();

    RenderCacheManager cache;
    const auto r1 = RenderPipeline::run(makeRequest(photoId, path, state),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r1.ok);
    const int missesAfterFirst = cache.misses();

    // 修改参数串内容（仅 S5/S6 相关）→ S3 必须命中缓存
    for (Element &e : state.elements)
        if (e.fieldKey == QLatin1String("paramString"))
            e.style.bold = !e.style.bold;
    const auto r2 = RenderPipeline::run(makeRequest(photoId, path, state),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r2.ok);
    QCOMPARE(cache.hits(), 1);                 // 第二次 S3 命中
    QCOMPARE(cache.misses(), missesAfterFirst);
}

void TestPipeline::lruEviction() const
{
    // 小容量缓存：2MB，灌入 3 张 1MB 级图 → 最旧的被淘汰
    RenderCacheManager cache(2 * 1024 * 1024);
    QImage img(512, 512, QImage::Format_ARGB32_Premultiplied);   // 1MB
    img.fill(Qt::red);

    cache.insertBackdrop("k1", img);
    cache.insertBackdrop("k2", img);
    QCOMPARE(cache.backdrop("k1").isNull(), false);   // 触达 k1 → k2 变最旧

    cache.insertBackdrop("k3", img);                  // 容量不足 → 淘汰 k2
    QCOMPARE(cache.backdrop("k2").isNull(), true);
    QCOMPARE(cache.backdrop("k1").isNull(), false);
    QCOMPARE(cache.backdrop("k3").isNull(), false);
    QVERIFY(cache.totalBytes() <= 2 * 1024 * 1024);
}

void TestPipeline::decodeFailurePropagates() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString bad = dir.filePath("bad.jpg");
    QFile f(bad);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("junk");
    f.close();

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    const auto result = RenderPipeline::run(makeRequest(QUuid::createUuid(), bad, state),
                                            stubResolver(), stubLogoResolver(),
                                            nullptr);
    QVERIFY(!result.ok);
    QVERIFY(!result.error.isEmpty());
}

void TestPipeline::unknownTemplateFails() const
{
    // 未知模板 id（M5 用户模板/拼写错误场景）必须显式报错，不得产出空白废图
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    TemplateState state;
    state.templateId = QStringLiteral("nonexistent-template");

    const auto result = RenderPipeline::run(makeRequest(QUuid::createUuid(), path, state),
                                            stubResolver(), stubLogoResolver(), nullptr);
    QVERIFY2(!result.ok, "未知模板必须失败而非静默成功");
    QVERIFY2(!result.error.isEmpty(), "失败必须携带可提示的错误信息");
}

void TestPipeline::allFieldsEmptyStillRenders() const
{
    // 无 Exif 网图/截图场景：全字段缺失 + Logo 缺图 → 正常出图（白带、无文字），
    // 行组全收紧后不得崩溃或空指针
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    state.elements = defaultSpec().elements;

    const auto emptyResolver = [](const QString &) { return QString(); };
    const auto emptyLogoResolver = [](const QString &) { return QImage(); };

    const auto result = RenderPipeline::run(makeRequest(QUuid::createUuid(), path, state),
                                            emptyResolver, emptyLogoResolver, nullptr);
    QVERIFY2(result.ok, qPrintable(result.error));
    QCOMPARE(result.image.width(), 640);
    QCOMPARE(result.image.height(), 538);
    // 信息带全空（行组全收紧，无文字/Logo 残留）
    bool foundNonWhite = false;
    for (int y = 481; y < result.image.height() && !foundNonWhite; ++y)
        for (int x = 0; x < result.image.width(); ++x)
            if (result.image.pixel(x, y) != QColor(Qt::white).rgba()) {
                foundNonWhite = true;
                break;
            }
    QVERIFY2(!foundNonWhite, "全字段缺失时信息带应为纯白（无残留元素）");
}

void TestPipeline::exportDoesNotPollutePreviewCache() const
{
    // §4.2：缓存为预览防抖服务；导出底图（全尺寸）不写入共享缓存
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = writeTestJpeg(dir, {640, 480});

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    state.elements = defaultSpec().elements;

    RenderCacheManager cache;
    const auto r1 = RenderPipeline::run(makeRequest(QUuid::createUuid(), path, state,
                                                    RenderPurpose::Export),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r1.ok);
    const auto r2 = RenderPipeline::run(makeRequest(QUuid::createUuid(), path, state,
                                                    RenderPurpose::Export),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r2.ok);
    QCOMPARE(cache.totalBytes(), 0);   // 导出两连渲，缓存仍为空
}

void TestPipeline::cacheHitNotPollutedByComposite() const
{
    // S4 合成直接画在缓存返回的共享 QImage 上——依赖 QPainter detach 保护
    // 缓存条目。带 alpha 的照片若 detach 失效会被"烤进"缓存背景（不透明
    // JPEG 探测不到），此用例将其钉死：第二次（命中缓存）渲染必须与第一次一致
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath("alpha_photo.png");
    QImage photo(320, 240, QImage::Format_ARGB32);
    photo.fill(QColor(80, 160, 200, 200));   // 半透明：污染必现像素差
    QVERIFY(photo.save(path));

    TemplateState state;
    state.templateId = QStringLiteral("bottom");
    state.elements = defaultSpec().elements;
    const QUuid photoId = QUuid::createUuid();

    RenderCacheManager cache;
    const auto r1 = RenderPipeline::run(makeRequest(photoId, path, state),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r1.ok);
    const auto r2 = RenderPipeline::run(makeRequest(photoId, path, state),
                                        stubResolver(), stubLogoResolver(), &cache);
    QVERIFY(r2.ok);
    QVERIFY2(cache.hits() >= 1, "第二次渲染应命中背景缓存");
    QCOMPARE(r1.image, r2.image);   // 缓存被污染（双重合成）则两次必然不同
}

QTEST_MAIN(TestPipeline)
#include "test_pipeline.moc"
