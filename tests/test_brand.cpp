// M2 收尾单测：BrandRegistry（品牌识别/别名/前缀/回落）+ SensorRegistry
//（画幅裁切倍率/等效焦距回退换算，R8）
#include <QtTest>

#include "metadata/BrandRegistry.h"
#include "metadata/FieldRegistry.h"
#include "metadata/SensorRegistry.h"

using namespace fxt;

class TestBrand : public QObject
{
    Q_OBJECT

private slots:
    // —— BrandRegistry ——
    void makeAliasMatching() const;
    void omSystemScenarios() const;
    void modelPrefixFallback() const;
    void unmatchedFallsBackToOthers() const;
    void panasonicShowsLumix() const;
    // —— SensorRegistry ——
    void cropFactorTable() const;
    void effectiveFocalSemantics() const;
    void focalEqFallbackInRegistry() const;
};

void TestBrand::makeAliasMatching() const
{
    // Exif 实测变体 → 规范化品牌
    QCOMPARE(BrandRegistry::match(QStringLiteral("NIKON CORPORATION"),
                                  QStringLiteral("NIKON D850"))
                 .value,
             QStringLiteral("Nikon"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("SONY"),
                                  QStringLiteral("ILCE-7M3"))
                 .value,
             QStringLiteral("Sony"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("FUJIFILM"),
                                  QStringLiteral("X100F"))
                 .value,
             QStringLiteral("Fujifilm"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("Canon"),
                                  QStringLiteral("Canon EOS R5"))
                 .value,
             QStringLiteral("Canon"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("Panasonic"),
                                  QStringLiteral("DC-TZ60"))
                 .label,
             QStringLiteral("LUMIX"));
}

void TestBrand::omSystemScenarios() const
{
    // 修 #13：OM Digital Solutions → OM System 展示（Olympus Logo）
    const BrandDef om = BrandRegistry::match(
        QStringLiteral("OM Digital Solutions"), QStringLiteral("OM-1 Mark II"));
    QCOMPARE(om.value, QStringLiteral("Olympus"));
    QCOMPARE(om.label, QStringLiteral("OM System"));
    // 旧机型 make=OLYMPUS
    QCOMPARE(BrandRegistry::match(QStringLiteral("OLYMPUS CORPORATION"),
                                  QStringLiteral("E-M1 Mark III"))
                 .label,
             QStringLiteral("OM System"));
    // make 缺失时 model 前缀兜底
    QCOMPARE(BrandRegistry::match(QString(), QStringLiteral("OM-5"))
                 .label,
             QStringLiteral("OM System"));
}

void TestBrand::modelPrefixFallback() const
{
    // make 空：model 前缀兜底（Sony ILCE / 富士 X100）
    QCOMPARE(BrandRegistry::match(QString(), QStringLiteral("ILCE-6000"))
                 .value,
             QStringLiteral("Sony"));
    QCOMPARE(BrandRegistry::match(QString(), QStringLiteral("X100V"))
                 .value,
             QStringLiteral("Fujifilm"));
}

void TestBrand::unmatchedFallsBackToOthers() const
{
    const BrandDef unknown =
        BrandRegistry::match(QStringLiteral("SomeRandomCam"),
                             QStringLiteral("XYZ-100"));
    QCOMPARE(unknown.value, QStringLiteral("Others"));
    QVERIFY(unknown.logoPath.endsWith(QStringLiteral("others.png")));
    // 空输入同样回落
    QCOMPARE(BrandRegistry::match(QString(), QString()).value,
             QStringLiteral("Others"));
}

void TestBrand::panasonicShowsLumix() const
{
    // 反馈源：松下 Logo 应为 LUMIX（展示文本层）
    const BrandDef p = BrandRegistry::match(QStringLiteral("Panasonic"),
                                            QStringLiteral("DC-S5"));
    QCOMPARE(p.label, QStringLiteral("LUMIX"));
    QVERIFY(p.logoPath.contains(QStringLiteral("panasonic")));
}

void TestBrand::cropFactorTable() const
{
    // 中画幅
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("FUJIFILM"),
                                           QStringLiteral("GFX 100S")),
             0.79);
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Hasselblad"),
                                           QStringLiteral("X2D 100C")),
             0.79);
    // M4/3
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("OM Digital Solutions"),
                                           QStringLiteral("OM-1 Mark II")),
             2.0);
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Panasonic"),
                                           QStringLiteral("DC-GH6")),
             2.0);
    // 佳能 APSC 1.6 / 全幅 1.0
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Canon"),
                                           QStringLiteral("Canon EOS R7")),
             1.6);
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Canon"),
                                           QStringLiteral("Canon EOS 5D Mark III")),
             1.0);
    // 尼康 APSC / 全幅
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("NIKON CORPORATION"),
                                           QStringLiteral("NIKON D500")),
             1.5);
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("NIKON CORPORATION"),
                                           QStringLiteral("NIKON D850")),
             1.0);
    // 富士 X 系 APSC（品牌级回落）
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("FUJIFILM"),
                                           QStringLiteral("X100F")),
             1.5);
    // 未收录回落全画幅
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("UnknownBrand"),
                                           QStringLiteral("XX-1")),
             1.0);
}

void TestBrand::effectiveFocalSemantics() const
{
    // Exif 35mm 等效优先直接取
    QCOMPARE(SensorRegistry::effectiveFocal(23.0, 35.0, 1.5), 35.0);
    // 无 Exif 值：按倍率换算
    QCOMPARE(SensorRegistry::effectiveFocal(50.0, 0, 1.5), 75.0);
    QCOMPARE(SensorRegistry::effectiveFocal(12.0, 0, 2.0), 24.0);
    // 倍率 1.0 且无 Exif 值 → 0（不出"50mm eq."废话值）
    QCOMPARE(SensorRegistry::effectiveFocal(50.0, 0, 1.0), 0.0);
    // 非法输入
    QCOMPARE(SensorRegistry::effectiveFocal(0, 0, 1.5), 0.0);
}

void TestBrand::focalEqFallbackInRegistry() const
{
    // 全链：无 FocalLengthIn35mmFilm 的 APSC 机身 → Registry 自动换算 eq.
    RawExif raw;
    raw.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("FUJIFILM"));
    raw.insert(QStringLiteral("Exif.Image.Model"), QStringLiteral("X100F"));
    raw.insert(QStringLiteral("Exif.Photo.FocalLength"), QStringLiteral("23/1"));

    const auto display = FieldRegistry::resolveDisplay(raw);
    QCOMPARE(display.value(QStringLiteral("focalLengthEq")),
             QStringLiteral("35mm eq."));
    // 有 Exif 35mm 值时直接取（不重复换算）
    RawExif raw2 = raw;
    raw2.insert(QStringLiteral("Exif.Photo.FocalLengthIn35mmFilm"),
                QStringLiteral("70/1"));
    const auto display2 = FieldRegistry::resolveDisplay(raw2);
    QCOMPARE(display2.value(QStringLiteral("focalLengthEq")),
             QStringLiteral("70mm eq."));
    // 未收录机型（回落 1.0）不出 eq.
    RawExif raw3 = raw;
    raw3.insert(QStringLiteral("Exif.Image.Make"), QStringLiteral("Mystery"));
    raw3.insert(QStringLiteral("Exif.Image.Model"), QStringLiteral("ZZ-9"));
    const auto display3 = FieldRegistry::resolveDisplay(raw3);
    QVERIFY(!display3.contains(QStringLiteral("focalLengthEq")));
}

QTEST_MAIN(TestBrand)
#include "test_brand.moc"
