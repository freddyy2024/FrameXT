// M1.5 RenderPipeline + RenderCacheManager 单测：
// 端到端出图、改文字命中背景缓存（<80ms 指标根基）、LRU 容量淘汰
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
                          const TemplateState &state)
{
    RenderRequest req;
    req.photoId = photoId;
    req.filePath = path;
    req.templateState = state;
    req.scale = 1.0;
    req.purpose = RenderPurpose::Export;
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
    // 画布 = H + 0.09W = 480 + 57.6 ≈ 538
    QCOMPARE(result.image.width(), 640);
    QCOMPARE(qRound(qreal(result.image.height()) / 10.0), 54);
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

QTEST_MAIN(TestPipeline)
#include "test_pipeline.moc"
