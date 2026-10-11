// M5.1/M5.2/M5.3 模板参数化、模板管理与 .ftpl 分享包测试：参数声明/
// JSON 往返/画布几何参数覆盖/用户模板持久化闭环/kind 解算/导入导出。
// 黄金图一致性（默认参数 = 旧常量）由既有 test_golden 零改动全绿背书。
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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

    // 未知模板 id 回落 bottom spec（删除模板后照片回落默认样式的宽容
    // 语义，M5.2 起）；空尺寸仍为空 plan（防御路径不回归）
    void invalidInputsProduceEmptyPlan()
    {
        QCOMPARE(CanvasGeometry::plan(QStringLiteral("nope"), landscape())
                     .canvasSize,
                 CanvasGeometry::plan(QStringLiteral("bottom"), landscape())
                     .canvasSize);
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

    // M5.2 用户模板闭环：保存 → 目录扫描 → specFor → 删除。
    // 临时目录注入（不污染真实 %APPDATA%）
    void userTemplateSaveLoadDelete()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        TemplateCatalog::setUserTemplatesDir(dir.path());
        QCOMPARE(TemplateCatalog::allSpecs().size(), 3);   // 干净起点

        // 保存：id 空 → 生成 UUID；builtin 标记被强制为 false
        TemplateSpec mine = TemplateCatalog::specFor(QStringLiteral("blur"));
        mine.id.clear();
        mine.name = QStringLiteral("我的拍立得");
        const QString id = TemplateCatalog::saveUserTemplate(mine);
        QVERIFY(!id.isEmpty());
        QVERIFY(id != QLatin1String("blur"));

        // 读取闭环：allSpecs 含用户模板且排在内置之后；specFor 命中
        const QList<TemplateSpec> all = TemplateCatalog::allSpecs();
        QCOMPARE(all.size(), 4);
        QCOMPARE(all.last().id, id);
        QVERIFY(!all.last().builtin);
        QCOMPARE(TemplateCatalog::specFor(id).name, QStringLiteral("我的拍立得"));

        // 再存一份 + 同 id 覆盖（保存修改语义）
        TemplateSpec mine2 = mine;
        mine2.name = QStringLiteral("重命名模板");
        mine2.id = id;
        QCOMPARE(TemplateCatalog::saveUserTemplate(mine2), id);
        QCOMPARE(TemplateCatalog::allSpecs().size(), 4);   // 覆盖不新增
        QCOMPARE(TemplateCatalog::specFor(id).name,
                 QStringLiteral("重命名模板"));

        // 内置 id 不可被用户模板覆盖
        TemplateSpec fake = mine;
        fake.id = QStringLiteral("bottom");
        QVERIFY(TemplateCatalog::saveUserTemplate(fake).isEmpty());

        // 删除闭环；未知 id 删除返回 false
        QVERIFY(TemplateCatalog::deleteUserTemplate(id));
        QVERIFY(!TemplateCatalog::deleteUserTemplate(id));
        QVERIFY(!TemplateCatalog::deleteUserTemplate(QStringLiteral("blur")));
        QCOMPARE(TemplateCatalog::allSpecs().size(), 3);
        // 已删除 id 回落 bottom（宽容，不崩）
        QCOMPARE(TemplateCatalog::specFor(id).kind, QStringLiteral("bottom"));

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }

    // 用户模板 id 的几何按 kind 解算（M5.2 CanvasGeometry 关键接线）：
    // kind=blur 的用户模板 id 必须走 blur 分支（圆角/roundedPhoto），
    // 参数覆盖与内置 blur 同语义
    void userTemplateIdResolvesByKind()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        TemplateCatalog::setUserTemplatesDir(dir.path());

        TemplateSpec mine = TemplateCatalog::specFor(QStringLiteral("blur"));
        mine.id.clear();
        mine.name = QStringLiteral("模糊变体");
        const QString id = TemplateCatalog::saveUserTemplate(mine);
        QVERIFY(!id.isEmpty());

        // 默认参数：与内置 blur plan 逐字段一致（id 不同、kind 相同）
        const CanvasPlan builtin =
            CanvasGeometry::plan(QStringLiteral("blur"), landscape());
        const CanvasPlan user = CanvasGeometry::plan(id, landscape());
        QCOMPARE(user.canvasSize, builtin.canvasSize);
        QCOMPARE(user.photoRect, builtin.photoRect);
        QCOMPARE(user.roundedPhoto, builtin.roundedPhoto);
        QVERIFY(qFuzzyCompare(user.cornerRadius + 1.0,
                              builtin.cornerRadius + 1.0));

        // 参数覆盖走用户 id 同样生效
        QVariantMap params;
        params.insert(QStringLiteral("blurCornerRadiusRatio"), 0.04);
        const CanvasPlan tuned = CanvasGeometry::plan(id, landscape(), params);
        QVERIFY(qFuzzyCompare(tuned.cornerRadius, 0.04 * 3000));

        // 非法 kind 的模板包：显式空 plan（不猜测几何）
        TemplateSpec bogus;
        bogus.id = QStringLiteral("bogus");
        bogus.kind = QStringLiteral("weird");
        bogus.name = QStringLiteral("bad");
        const QString bogusId = TemplateCatalog::saveUserTemplate(bogus);
        QVERIFY(!bogusId.isEmpty());
        QVERIFY(CanvasGeometry::plan(bogusId, landscape())
                    .canvasSize.isEmpty());

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }

    // —— M5.3 .ftpl 导入导出（跨实例分享）——

    // 导出→换实例目录→导入往返：kind/name/参数逐项保真，id 重新生成，
    // builtin 强制 false；导入后 specFor 命中、几何按 kind 解算
    void ftplExportImportRoundTrip()
    {
        QTemporaryDir instanceA;
        QTemporaryDir shared;
        QVERIFY(instanceA.isValid() && shared.isValid());
        TemplateCatalog::setUserTemplatesDir(instanceA.path());

        // 实例 A：从 blur 另存一个改过默认参数的用户模板并导出
        TemplateSpec mine = TemplateCatalog::specFor(QStringLiteral("blur"));
        mine.id.clear();
        mine.name = QStringLiteral("分享模糊");
        for (TemplateParamDecl &p : mine.params)
            if (p.key == QLatin1String("blurStrength"))
                p.defaultValue = 1.5;
        const QString originId = TemplateCatalog::saveUserTemplate(mine);
        QVERIFY(!originId.isEmpty());
        const QString pack = shared.filePath("share.ftpl");
        QVERIFY(TemplateCatalog::exportTemplate(
            TemplateCatalog::specFor(originId), pack));

        // 实例 B（空目录）：导入后逐字段对账
        QTemporaryDir instanceB;
        QVERIFY(instanceB.isValid());
        TemplateCatalog::setUserTemplatesDir(instanceB.path());
        QString error;
        const QString importedId =
            TemplateCatalog::importTemplate(pack, &error);
        QVERIFY2(!importedId.isEmpty(), qPrintable(error));
        // 实例 B 无 id 冲突 → 原样保留（同源模板跨实例同 id，互享不重复）；
        // 冲突时换新副本的分支由 ftplImportDuplicateMakesCopy 覆盖
        QCOMPARE(importedId, originId);
        const TemplateSpec got = TemplateCatalog::specFor(importedId);
        QCOMPARE(got.kind, QStringLiteral("blur"));
        QCOMPARE(got.name, QStringLiteral("分享模糊"));
        QCOMPARE(got.builtin, false);
        QCOMPARE(got.params.size(),
                 TemplateCatalog::specFor(QStringLiteral("blur")).params.size());
        QVERIFY(qFuzzyCompare(got.param(QStringLiteral("blurStrength"))
                                  ->defaultValue,
                              1.5));
        // kind 解算贯通：导入模板 id 的画布走 blur 分支
        QCOMPARE(CanvasGeometry::plan(importedId, landscape()).roundedPhoto,
                 true);

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }

    // 重复导入同一分享包：生成独立副本（导入永不覆盖已有模板）
    void ftplImportDuplicateMakesCopy()
    {
        QTemporaryDir dir;
        QTemporaryDir shared;
        QVERIFY(dir.isValid() && shared.isValid());
        TemplateCatalog::setUserTemplatesDir(dir.path());

        TemplateSpec mine = TemplateCatalog::specFor(QStringLiteral("around"));
        mine.id.clear();
        mine.name = QStringLiteral("环绕变体");
        const QString id = TemplateCatalog::saveUserTemplate(mine);
        QVERIFY(!id.isEmpty());
        const QString pack = shared.filePath("dup.ftpl");
        QVERIFY(TemplateCatalog::exportTemplate(
            TemplateCatalog::specFor(id), pack));

        // 第一次导入：id 撞已有用户模板 → 新副本，原模板参数不被覆盖
        QString error;
        const QString copy1 = TemplateCatalog::importTemplate(pack, &error);
        QVERIFY2(!copy1.isEmpty(), qPrintable(error));
        QVERIFY(copy1 != id);
        QCOMPARE(TemplateCatalog::allSpecs().size(), 5);   // 3 内置+2 用户
        // 第二次导入：再生成一份，互不影响
        const QString copy2 = TemplateCatalog::importTemplate(pack, &error);
        QVERIFY2(!copy2.isEmpty(), qPrintable(error));
        QVERIFY(copy2 != copy1 && copy2 != id);
        QCOMPARE(TemplateCatalog::allSpecs().size(), 6);
        QCOMPARE(TemplateCatalog::specFor(id).name, QStringLiteral("环绕变体"));

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }

    // 坏输入拒绝：高版本包提示更新应用（区别于损坏）；垃圾内容/缺文件报错可读
    void ftplImportRejectsBadInput()
    {
        QTemporaryDir dir;
        QTemporaryDir shared;
        QVERIFY(dir.isValid() && shared.isValid());
        TemplateCatalog::setUserTemplatesDir(dir.path());

        // 高版本：version > kSchemaVersion → 明确提示升级，不静默降级
        QJsonObject bumped;
        bumped["schemaVersion"] = TemplateCatalog::kSchemaVersion + 1;
        bumped["id"] = QStringLiteral("future");
        bumped["kind"] = QStringLiteral("bottom");
        bumped["name"] = QStringLiteral("未来模板");
        const QString newer = shared.filePath("newer.ftpl");
        QFile nf(newer);
        QVERIFY(nf.open(QIODevice::WriteOnly));
        nf.write(QJsonDocument(bumped).toJson(QJsonDocument::Compact));
        nf.close();
        QString error;
        QVERIFY(TemplateCatalog::importTemplate(newer, &error).isEmpty());
        QVERIFY2(error.contains(QStringLiteral("更新")),
                 qPrintable(error));

        // 垃圾内容：报错但不是版本提示
        const QString garbage = shared.filePath("garbage.ftpl");
        QFile gf(garbage);
        QVERIFY(gf.open(QIODevice::WriteOnly));
        gf.write("not json at all");
        gf.close();
        error.clear();
        QVERIFY(TemplateCatalog::importTemplate(garbage, &error).isEmpty());
        QVERIFY(!error.isEmpty() && !error.contains(QStringLiteral("更新")));

        // 文件不存在
        error.clear();
        QVERIFY(TemplateCatalog::importTemplate(
                    shared.filePath("missing.ftpl"), &error)
                .isEmpty());
        QVERIFY(!error.isEmpty());

        // 导出失败路径：不可写目录
        QVERIFY(!TemplateCatalog::exportTemplate(
            TemplateCatalog::specFor(QStringLiteral("bottom")),
            dir.filePath("no-such-dir/x.ftpl")));
        error.clear();
        QVERIFY(!TemplateCatalog::exportTemplate(
            TemplateCatalog::specFor(QStringLiteral("bottom")),
            dir.filePath("no-such-dir/x.ftpl"), &error));
        QVERIFY(!error.isEmpty());

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }

    // 分享包伪造内置 id（bottom）：导入换新 id 安装，内置三模板不被覆盖
    void ftplImportBuiltinIdReplaced()
    {
        QTemporaryDir dir;
        QTemporaryDir shared;
        QVERIFY(dir.isValid() && shared.isValid());
        TemplateCatalog::setUserTemplatesDir(dir.path());

        QJsonObject fake;
        fake["schemaVersion"] = TemplateCatalog::kSchemaVersion;
        fake["id"] = QStringLiteral("bottom");
        fake["kind"] = QStringLiteral("bottom");
        fake["name"] = QStringLiteral("假内置");
        QJsonArray params;
        QJsonObject p;
        p["key"] = QStringLiteral("exBandRatioLandscape");
        p["default"] = 0.5;
        params.append(p);
        fake["params"] = params;
        const QString pack = shared.filePath("fake.ftpl");
        QFile f(pack);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QJsonDocument(fake).toJson(QJsonDocument::Compact));
        f.close();

        QString error;
        const QString id = TemplateCatalog::importTemplate(pack, &error);
        QVERIFY2(!id.isEmpty(), qPrintable(error));
        QVERIFY(id != QLatin1String("bottom"));
        // 内置 bottom 仍为原声明（名称/默认值未被动过）
        const TemplateSpec builtin =
            TemplateCatalog::specFor(QStringLiteral("bottom"));
        QCOMPARE(builtin.builtin, true);
        QVERIFY(qFuzzyCompare(builtin.param(QStringLiteral("exBandRatioLandscape"))
                                  ->defaultValue,
                              0.09));

        TemplateCatalog::setUserTemplatesDir(
            QStringLiteral("/nonexistent-fxt-reset"));
    }
};

QTEST_MAIN(TestTemplates)
#include "test_templates.moc"
