// M5.1 模板参数化测试：参数声明/JSON 往返/画布几何参数覆盖/缓存键安全。
// 黄金图一致性（默认参数 = 旧常量）由既有 test_golden 零改动全绿背书。
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "model/TemplateSpec.h"
#include "render/BackdropRenderer.h"
#include "render/CanvasGeometry.h"

using namespace fxt;

namespace {

QSize landscape() { return { 3000, 2000 }; }   // 横 3:2
QSize portrait() { return { 2000, 3000 }; }    // 竖 2:3
QSize square() { return { 2400, 2400 }; }

} // namespace

class TestTemplates : public QObject
{
    Q_OBJECT

private slots:
    // 参数键缺失/空 params = 内置常量：三模板 × 横竖方逐字段相等
    void emptyParamsMatchLegacyConstants_data()
    {
        QTest::addColumn<QString>("templateId");
        QTest::newRow("bottom") << QStringLiteral("bottom");
        QTest::newRow("around") << QStringLiteral("around");
        QTest::newRow("blur") << QStringLiteral("blur");
    }

    void emptyParamsMatchLegacyConstants()
    {
        QFETCH(QString, templateId);
        const QSize sizes[] = { landscape(), portrait(), square() };
        for (const QSize &size : sizes) {
            const CanvasPlan legacy =
                CanvasGeometry::plan(templateId, size);
            const CanvasPlan parameterized = CanvasGeometry::plan(
                templateId, size, QVariantMap());
            QCOMPARE(parameterized.canvasSize, legacy.canvasSize);
            QCOMPARE(parameterized.photoRect, legacy.photoRect);
            QVERIFY(qFuzzyCompare(parameterized.cornerRadius + 1.0,
                                  legacy.cornerRadius + 1.0));
            QVERIFY(qFuzzyCompare(parameterized.exBandHeight + 1.0,
                                  legacy.exBandHeight + 1.0));
            QCOMPARE(parameterized.roundedPhoto, legacy.roundedPhoto);
        }
    }

    // 信息带高度参数生效（横/竖键各自独立）
    void exBandRatioOverrides()
    {
        QVariantMap params;
        params.insert(QStringLiteral("exBandRatioLandscape"), 0.12);
        params.insert(QStringLiteral("exBandRatioPortrait"), 0.15);
        const CanvasPlan l = CanvasGeometry::plan(
            QStringLiteral("bottom"), landscape(), params);
        QVERIFY(qFuzzyCompare(l.exBandHeight, 0.12 * 3000));
        QCOMPARE(l.canvasSize.height(), 2000 + qRound(0.12 * 3000));

        const CanvasPlan p = CanvasGeometry::plan(
            QStringLiteral("bottom"), portrait(), params);
        QVERIFY(qFuzzyCompare(p.exBandHeight, 0.15 * 2000));
    }

    // blur 圆角比例覆盖（默认 1/60，测试 5%）
    void blurCornerRadiusOverride()
    {
        QVariantMap params;
        params.insert(QStringLiteral("blurCornerRadiusRatio"), 0.05);
        const CanvasPlan plan = CanvasGeometry::plan(
            QStringLiteral("blur"), landscape(), params);
        QVERIFY(qFuzzyCompare(plan.cornerRadius, 0.05 * 3000));
        QCOMPARE(plan.roundedPhoto, true);
    }

    // blur 模糊强度：不同 strength 产出不同背景纹理；缺省 1.0 = 旧常量
    void blurStrengthAffectsBackdrop()
    {
        const CanvasPlan plan = CanvasGeometry::plan(
            QStringLiteral("blur"), QSize(900, 600));
        // 渐变 + 棋盘混合图（纯色图模糊恒等，测不出强度差异）
        QImage photo(900, 600, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < photo.height(); ++y) {
            for (int x = 0; x < photo.width(); ++x) {
                const int v = ((x / 40 + y / 40) % 2) ? 30 : 220;
                photo.setPixel(x, y, QColor(v, x % 256, y % 256).rgba());
            }
        }

        const QImage soft =
            BackdropRenderer::backdrop(plan, QStringLiteral("blur"), photo, 0.4);
        const QImage strong =
            BackdropRenderer::backdrop(plan, QStringLiteral("blur"), photo, 2.0);
        const QImage def =
            BackdropRenderer::backdrop(plan, QStringLiteral("blur"), photo, 1.0);
        const QImage legacy =
            BackdropRenderer::backdrop(plan, QStringLiteral("blur"), photo);
        QVERIFY(soft != strong);   // 强度不同 → 纹理不同
        QVERIFY(def == legacy);    // 缺省参数 = 旧签名输出
    }

    // 非法值（类型错误）回落默认——渲染层对模板包参数宽容
    void invalidParamValuesFallBack()
    {
        QVariantMap params;
        params.insert(QStringLiteral("exBandRatioLandscape"),
                      QStringLiteral("not-a-number"));
        const CanvasPlan plan = CanvasGeometry::plan(
            QStringLiteral("bottom"), landscape(), params);
        const CanvasPlan legacy =
            CanvasGeometry::plan(QStringLiteral("bottom"), landscape());
        QCOMPARE(plan.canvasSize, legacy.canvasSize);
    }

    // 未知模板 + 空尺寸 → 空 plan（防御路径不回归）
    void invalidInputsProduceEmptyPlan()
    {
        QVERIFY(CanvasGeometry::plan(QStringLiteral("nope"), landscape())
                    .canvasSize.isEmpty());
        QVERIFY(CanvasGeometry::plan(QStringLiteral("bottom"), QSize())
                    .canvasSize.isEmpty());
    }

    // 目录：内置三模板齐备、未知 id 回落 bottom
    void catalogBuiltinSpecs()
    {
        const QList<TemplateSpec> specs = TemplateCatalog::builtinSpecs();
        QCOMPARE(specs.size(), 3);
        QCOMPARE(TemplateCatalog::specFor(QStringLiteral("blur")).kind,
                 QStringLiteral("blur"));
        QCOMPARE(TemplateCatalog::specFor(QStringLiteral("ghost")).kind,
                 QStringLiteral("bottom"));
        // 每个参数声明自洽（范围有序、步进为正）
        for (const TemplateSpec &s : specs) {
            QVERIFY(!s.params.isEmpty());
            for (const TemplateParamDecl &p : s.params) {
                QVERIFY(p.maximum > p.minimum);
                QVERIFY(p.step > 0);
                QVERIFY(p.defaultValue >= p.minimum);
                QVERIFY(p.defaultValue <= p.maximum);
            }
        }
    }

    // spec JSON 往返 + 版本闸
    void specJsonRoundTrip()
    {
        const TemplateSpec spec =
            TemplateCatalog::specFor(QStringLiteral("blur"));
        const QByteArray json = TemplateCatalog::specToJson(spec);
        bool ok = false;
        int version = 0;
        const TemplateSpec parsed = TemplateCatalog::specFromJson(
            json, &ok, &version);
        QVERIFY(ok);
        QCOMPARE(version, TemplateCatalog::kSchemaVersion);
        QVERIFY(parsed == spec);

        // 高版本拒绝（不静默降级）
        QJsonObject o = QJsonDocument::fromJson(json).object();
        o["schemaVersion"] = TemplateCatalog::kSchemaVersion + 1;
        const QByteArray bumped =
            QJsonDocument(o).toJson(QJsonDocument::Compact);
        const TemplateSpec rejected = TemplateCatalog::specFromJson(
            bumped, &ok, &version);
        QVERIFY(!ok);
        QVERIFY(rejected.id.isEmpty());
    }

    // 资源入库一致性（5.1 准入）：resources/templates/*.json 与代码
    // builtinSpecs 逐项相等——两处漂移即测试红（默认值改动必须同步资源）
    void builtinResourceJsonMatchesCode()
    {
        for (const TemplateSpec &s : TemplateCatalog::builtinSpecs()) {
            QFile f(QStringLiteral(":/templates/%1.json").arg(s.id));
            QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
            bool ok = false;
            const TemplateSpec parsed =
                TemplateCatalog::specFromJson(f.readAll(), &ok);
            QVERIFY2(ok, qPrintable(f.fileName()));
            QVERIFY2(parsed == s, qPrintable(f.fileName()));
        }
    }
};

QTEST_MAIN(TestTemplates)
#include "test_templates.moc"
