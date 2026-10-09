// 领域模型单测：默认值契约 + JSON 往返一致性 + 容错回落（03-开发计划 1.1）
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

#include "model/ModelIo.h"
#include "model/PhotoDocument.h"

using namespace fxt;

namespace {

Element makeSampleElement()
{
    Element e;
    e.id = QUuid::createUuid();
    e.kind = ElementKind::Text;
    e.fieldKey = QStringLiteral("focalLength");
    e.literal = QStringLiteral("42mm");
    e.anchor = Anchor::BottomLeft;
    e.offset = QPointF(0.02, -0.01);
    e.style.fontFamily = QStringLiteral("Arial");
    e.style.fontScale = 0.031;
    e.style.bold = true;
    e.style.color = QColor(0x10, 0x22, 0x33, 0xAA);
    e.style.opacity = 0.85;
    e.logoScale = 0.55;
    e.visible = false;
    e.locked = true;
    e.hasCustomPosition = true;
    return e;
}

} // namespace

class TestModel : public QObject
{
    Q_OBJECT

private slots:
    void elementDefaults() const;
    void elementJsonRoundTrip() const;
    void templateStateJsonRoundTrip() const;
    void photoDocumentJsonRoundTrip() const;
    void missingFieldsFallBackToDefaults() const;
    void outOfRangeEnumFallsBack() const;
    void schemaVersionRoundTrip() const;
};

void TestModel::elementDefaults() const
{
    const Element e;
    QVERIFY(e.id.isNull());  // id 由工厂 / fromJson 兜底生成
    QCOMPARE(e.kind, ElementKind::Text);
    QCOMPARE(e.anchor, Anchor::BottomLeft);
    QVERIFY(e.visible);
    QVERIFY(!e.locked);
    QVERIFY(!e.hasCustomPosition);
    QVERIFY(e.style.opacity > 0.0);
    QVERIFY(!e.style.fontFamily.isEmpty());
}

void TestModel::elementJsonRoundTrip() const
{
    const Element source = makeSampleElement();
    const QJsonObject json = ModelIo::toJson(source);
    const Element restored = ModelIo::elementFromJson(json);

    // 逐字段定位：整体比较失败时能直接看出差异字段
    QVERIFY2(restored.id == source.id, "id");
    QVERIFY2(restored.kind == source.kind, "kind");
    QVERIFY2(restored.fieldKey == source.fieldKey, "fieldKey");
    QVERIFY2(restored.literal == source.literal, "literal");
    QVERIFY2(restored.anchor == source.anchor, "anchor");
    QVERIFY2(restored.offset == source.offset, "offset");
    QVERIFY2(restored.style.fontFamily == source.style.fontFamily, "style.fontFamily");
    QVERIFY2(restored.style.fontScale == source.style.fontScale, "style.fontScale");
    QVERIFY2(restored.style.bold == source.style.bold, "style.bold");
    QVERIFY2(restored.style.color == source.style.color,
             qPrintable(QStringLiteral("style.color src=%1 json=%2 restored=%3")
                            .arg(source.style.color.name(QColor::HexArgb),
                                 QString::fromUtf8(
                                     QJsonDocument(json).toJson(
                                         QJsonDocument::Compact)),
                                 restored.style.color.name(QColor::HexArgb))));
    QVERIFY2(restored.style.opacity == source.style.opacity, "style.opacity");
    QVERIFY2(restored.logoScale == source.logoScale, "logoScale");
    QVERIFY2(restored.visible == source.visible, "visible");
    QVERIFY2(restored.locked == source.locked, "locked");
    QVERIFY2(restored.hasCustomPosition == source.hasCustomPosition,
             "hasCustomPosition");
    QCOMPARE(restored, source);
}

void TestModel::templateStateJsonRoundTrip() const
{
    TemplateState t;
    t.templateId = QStringLiteral("bottom");
    t.templateParams.insert(QStringLiteral("paramPattern"), QStringLiteral("classic"));
    t.templateParams.insert(QStringLiteral("cornerRadius"), 5.0);
    t.elements.append(makeSampleElement());
    t.elements.append(makeSampleElement());

    const TemplateState restored =
        ModelIo::templateStateFromJson(ModelIo::toJson(t));
    QCOMPARE(restored == t, true);
}

void TestModel::photoDocumentJsonRoundTrip() const
{
    PhotoDocument d;
    d.id = QUuid::createUuid();
    d.sourcePath = QStringLiteral("D:/pics/IMG_0001.jpg");
    d.meta.pixelSize = QSize(8192, 5464);
    d.meta.format = QStringLiteral("jpeg");
    d.meta.bitDepth = 8;
    d.meta.dpi = QSizeF(72.0, 72.0);
    d.meta.exif.make = QStringLiteral("Canon");
    d.meta.exif.model = QStringLiteral("Canon EOS R5");
    d.meta.hasGps = true;
    d.meta.filePath = d.sourcePath;
    d.templateState.templateId = QStringLiteral("blur");
    d.templateState.elements.append(makeSampleElement());
    d.overrides.values.insert(QStringLiteral("dateTime"),
                              QStringLiteral("2026-10-10 12:00"));

    const PhotoDocument restored =
        ModelIo::photoDocumentFromJson(ModelIo::toJson(d));
    QCOMPARE(restored == d, true);
}

void TestModel::missingFieldsFallBackToDefaults() const
{
    // 空 JSON：不崩溃、字段回落默认值、id 自动补 UUID（schema 演进容错）
    const Element e = ModelIo::elementFromJson(QJsonObject());
    QVERIFY(!e.id.isNull());
    QCOMPARE(e.kind, ElementKind::Text);
    QVERIFY(e.visible);

    const PhotoDocument d = ModelIo::photoDocumentFromJson(QJsonObject());
    QVERIFY(!d.id.isNull());
    QCOMPARE(d.meta.bitDepth, 8);
    QVERIFY(d.overrides.isEmpty());
}

void TestModel::outOfRangeEnumFallsBack() const
{
    // 手改 JSON / 损坏文件注入越界枚举：回落默认而非产出无效枚举
    // （无效值会让 measure/painter 的 switch 静默跳过，数据污染不报错）
    QJsonObject o;
    o["id"] = QUuid::createUuid().toString(QUuid::WithoutBraces);
    o["kind"] = 99;
    o["anchor"] = -7;
    const Element e = ModelIo::elementFromJson(o);
    QCOMPARE(e.kind, ElementKind::Text);
    QCOMPARE(e.anchor, Anchor::BottomLeft);

    // 边界合法值仍可往返
    QJsonObject o2;
    o2["kind"] = int(ElementKind::Free);
    o2["anchor"] = int(Anchor::LogoLineStart);
    const Element e2 = ModelIo::elementFromJson(o2);
    QCOMPARE(e2.kind, ElementKind::Free);
    QCOMPARE(e2.anchor, Anchor::LogoLineStart);
}

void TestModel::schemaVersionRoundTrip() const
{
    // §6：JSON 均带 schemaVersion；读取端对缺失容忍（向后兼容）
    TemplateState t;
    t.templateId = QStringLiteral("bottom");
    const QJsonObject json = ModelIo::toJson(t);
    QCOMPARE(json.value("schemaVersion").toInt(0), 1);
    const TemplateState restored = ModelIo::templateStateFromJson(json);
    QCOMPARE(restored.templateId, t.templateId);
}

QTEST_MAIN(TestModel)
#include "test_model.moc"
