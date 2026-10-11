// M2 收尾单测：BrandRegistry（品牌识别/别名/前缀/回落）+ SensorRegistry
//（画幅裁切倍率/等效焦距回退换算，R8）
// M5.4 增补：brands.json 数据源接线（qrc 与内置表一致性 + 用户覆盖层）
#include <QFile>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
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
    // —— M5.4 数据源接线 ——
    void jsonResourceMatchesBuiltin() const;
    void userOverlayOverridesAndExtends() const;
    void userOverlayCorruptIgnored() const;
    // —— 评审回归 ——
    void ricohPentaxDisambiguation() const;
    void phoneBrandsAndCaseVariants() const;
    // —— SensorRegistry ——
    void cropFactorTable() const;
    void effectiveFocalSemantics() const;
    void focalEqFallbackInRegistry() const;
    void cropFactorNegativeCases() const;
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
    // 修 #13：OM Digital Solutions → OM System 展示（M5.5 起为官方
    // OM SYSTEM 方形徽标，替换旧 OLYMPUS 文字标）
    const BrandDef om = BrandRegistry::match(
        QStringLiteral("OM Digital Solutions"), QStringLiteral("OM-1 Mark II"));
    QCOMPARE(om.value, QStringLiteral("Olympus"));
    QCOMPARE(om.label, QStringLiteral("OM System"));
    QCOMPARE(om.logoPath,
             QStringLiteral(":/logos/camera/omsystem-logo-1280-1280.png"));
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
    // 反馈源 R3（5 赞）：松下 Logo 应为 LUMIX 标而非 Panasonic 集团标
    const BrandDef p = BrandRegistry::match(QStringLiteral("Panasonic"),
                                            QStringLiteral("DC-S5"));
    QCOMPARE(p.label, QStringLiteral("LUMIX"));
    QCOMPARE(p.logoPath,
             QStringLiteral(":/logos/camera/lumix-logo-1920-438.png"));
}

// —— M5.4 数据源接线 ——

namespace {
// 构造用户覆盖层 JSON 文档
QByteArray overlayJson(const QJsonObject &overrideBrand,
                       const QJsonObject &extraBrand,
                       const QJsonObject &fallback)
{
    QJsonArray brands;
    if (!overrideBrand.isEmpty())
        brands.append(overrideBrand);
    if (!extraBrand.isEmpty())
        brands.append(extraBrand);
    QJsonObject o;
    o["schemaVersion"] = 1;
    o["brands"] = brands;
    if (!fallback.isEmpty())
        o["fallback"] = fallback;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}
} // namespace

// qrc brands.json（唯一准绳）与 C++ 内置表逐条一致——两处漂移即红
//（内置表仅作 JSON 损坏时的应急兜底，同步靠本测试强制）
void TestBrand::jsonResourceMatchesBuiltin() const
{
    QFile f(QStringLiteral(":/brands/brands.json"));
    QVERIFY2(f.open(QIODevice::ReadOnly), qPrintable(f.fileName()));
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    QVERIFY(doc.isObject());

    const QList<BrandDef> builtin = BrandRegistry::builtin();
    QHash<QString, BrandDef> byValue;
    for (const BrandDef &b : builtin)
        byValue.insert(b.value, b);

    const QJsonArray arr = doc.object()["brands"].toArray();
    QCOMPARE(arr.size(), builtin.size());
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        const QString value = o["value"].toString();
        QVERIFY2(byValue.contains(value), qPrintable(value));
        const BrandDef &b = byValue[value];
        QCOMPARE(o["label"].toString(), b.label);
        QCOMPARE(o["logo"].toString(), b.logoPath);
        QStringList aliases;
        for (const QJsonValue &a : o["aliases"].toArray())
            aliases.append(a.toString());
        QCOMPARE(aliases, b.makeAliases);
        QStringList prefixes;
        for (const QJsonValue &p : o["modelPrefixes"].toArray())
            prefixes.append(p.toString());
        QCOMPARE(prefixes, b.modelPrefixes);
    }

    // fallback 条目 = others() 兜底
    const QJsonObject fb = doc.object()["fallback"].toObject();
    QCOMPARE(fb["value"].toString(),
             BrandRegistry::others().value);
    QCOMPARE(fb["logo"].toString(), BrandRegistry::others().logoPath);
}

// 用户覆盖层：同 value 整条替换（LUMIX 改展示文本）、新 value 追加
//（自造品牌可识别）、fallback 可替换；重置后恢复内置语义
void TestBrand::userOverlayOverridesAndExtends() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString overlay = dir.filePath("brands.json");

    // 1) 同 value 覆盖：Panasonic label 改名 + logo 换备选
    QJsonObject lumix;
    lumix["value"] = QStringLiteral("Panasonic");
    lumix["label"] = QStringLiteral("LUMIX S");
    lumix["aliases"] = QJsonArray{ QStringLiteral("Panasonic"),
                                   QStringLiteral("LUMIX") };
    lumix["modelPrefixes"] = QJsonArray{ QStringLiteral("DC-"),
                                         QStringLiteral("DMC-") };
    lumix["logo"] = QStringLiteral(":/logos/camera/panasonic-logo2-1603-245.png");
    // 2) 新 value 追加：别名直接命中（未收录相机的用户自救路径）
    QJsonObject extra;
    extra["value"] = QStringLiteral("YiCamera");
    extra["label"] = QStringLiteral("Yi Camera");
    extra["aliases"] = QJsonArray{ QStringLiteral("YI Technology") };
    extra["modelPrefixes"] = QJsonArray{ QStringLiteral("YI-") };
    extra["logo"] = QStringLiteral(":/logos/camera/others.png");
    // 3) fallback 替换
    QJsonObject fb;
    fb["value"] = QStringLiteral("Unknown");
    fb["label"] = QStringLiteral("Unknown");
    fb["logo"] = QStringLiteral(":/logos/camera/others.png");

    QFile f(overlay);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(overlayJson(lumix, extra, fb));
    f.close();
    BrandRegistry::setUserBrandsFile(overlay);

    const BrandDef p = BrandRegistry::match(QStringLiteral("Panasonic"),
                                            QStringLiteral("DC-S5"));
    QCOMPARE(p.label, QStringLiteral("LUMIX S"));
    QCOMPARE(p.logoPath,
             QStringLiteral(":/logos/camera/panasonic-logo2-1603-245.png"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("YI TECHNOLOGY"),
                                  QStringLiteral("YI-M1"))
                 .value,
             QStringLiteral("YiCamera"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("SomeRandomCam"),
                                  QStringLiteral("XYZ-100"))
                 .value,
             QStringLiteral("Unknown"));

    // 重置（还原缺省路径）→ 覆盖失效，内置语义恢复
    BrandRegistry::setUserBrandsFile(QString());
    QCOMPARE(BrandRegistry::match(QStringLiteral("Panasonic"),
                                  QStringLiteral("DC-S5"))
                 .label,
             QStringLiteral("LUMIX"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("SomeRandomCam"),
                                  QStringLiteral("XYZ-100"))
                 .value,
             QStringLiteral("Others"));
}

// 覆盖文件损坏：忽略并保持基底表工作（品牌识别零崩溃红线）
void TestBrand::userOverlayCorruptIgnored() const
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString overlay = dir.filePath("bad.json");
    QFile f(overlay);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write("{ not valid json");
    f.close();
    BrandRegistry::setUserBrandsFile(overlay);
    // 基底照常 + 兜底未被破坏
    QCOMPARE(BrandRegistry::match(QStringLiteral("NIKON CORPORATION"),
                                  QStringLiteral("NIKON D850"))
                 .value,
             QStringLiteral("Nikon"));
    QCOMPARE(BrandRegistry::others().value, QStringLiteral("Others"));
    // 空数组表同样视为损坏（识别必然全落 Others，无意义）
    QFile f2(overlay);
    QVERIFY(f2.open(QIODevice::WriteOnly | QIODevice::Truncate));
    f2.write(QJsonDocument(QJsonObject{ { "brands", QJsonArray{} } })
                 .toJson(QJsonDocument::Compact));
    f2.close();
    BrandRegistry::setUserBrandsFile(overlay);
    QCOMPARE(BrandRegistry::match(QStringLiteral("Panasonic"),
                                  QStringLiteral("DC-S5"))
                 .label,
             QStringLiteral("LUMIX"));

    BrandRegistry::setUserBrandsFile(QString());
}

// —— 评审回归 ——

void TestBrand::ricohPentaxDisambiguation() const
{
    // P1：理光 GR 与宾得 K 系新机共用 make 串 "RICOH IMAGING COMPANY, LTD."
    //（原实现 GR 系会被 Pentax 别名抢先误判）
    const BrandDef gr = BrandRegistry::match(
        QStringLiteral("RICOH IMAGING COMPANY, LTD."), QStringLiteral("GR III"));
    QCOMPARE(gr.value, QStringLiteral("Ricoh"));
    QCOMPARE(gr.label, QStringLiteral("Ricoh"));
    const BrandDef k3 = BrandRegistry::match(
        QStringLiteral("RICOH IMAGING COMPANY, LTD."),
        QStringLiteral("PENTAX K-3 Mark III"));
    QCOMPARE(k3.value, QStringLiteral("Pentax"));
    // 缺 "PENTAX" 字样的宾得 model：K- 前缀消歧
    QCOMPARE(BrandRegistry::match(QStringLiteral("RICOH IMAGING COMPANY, LTD."),
                                  QStringLiteral("K-1 Mark II"))
                 .value,
             QStringLiteral("Pentax"));
    // 老宾得 make
    QCOMPARE(BrandRegistry::match(QStringLiteral("PENTAX Corporation"),
                                  QStringLiteral("PENTAX K-5"))
                 .value,
             QStringLiteral("Pentax"));
}

void TestBrand::phoneBrandsAndCaseVariants() const
{
    // P1：手机品牌与大小写混合别名此前零覆盖
    QCOMPARE(BrandRegistry::match(QStringLiteral("Apple"),
                                  QStringLiteral("iPhone 15 Pro"))
                 .value,
             QStringLiteral("Apple"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("HUAWEI"),
                                  QStringLiteral("P50 Pro"))
                 .value,
             QStringLiteral("Huawei"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("samsung"),
                                  QStringLiteral("SM-S9180"))
                 .value,
             QStringLiteral("Samsung"));
    // 全小写 make 别名包含仍命中
    QCOMPARE(BrandRegistry::match(QStringLiteral("nikon corporation"),
                                  QStringLiteral("NIKON Z 6_2"))
                 .value,
             QStringLiteral("Nikon"));
    QCOMPARE(BrandRegistry::match(QStringLiteral("ricoh"),
                                  QStringLiteral("GR IIIx"))
                 .value,
             QStringLiteral("Ricoh"));
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

void TestBrand::cropFactorNegativeCases() const
{
    // P1：跨厂子串误命中负例（makeHint 品牌约束的回归锁定）
    // Canon PowerShot G9 X（1 英寸底）不得命中 Panasonic "G9" → 2.0
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Canon"),
                                           QStringLiteral("PowerShot G9 X")),
             1.0);
    // PENTAX K-1（全幅）不得命中 Olympus "PEN" → 2.0
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("PENTAX"),
                                           QStringLiteral("PENTAX K-1")),
             1.0);
    // 理光 GR IIIx：GR 规则命中（1.5×）
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("RICOH IMAGING"),
                                           QStringLiteral("GR IIIx")),
             1.5);
    // 松下全幅 S5 不误判 M43
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Panasonic"),
                                           QStringLiteral("DC-S5")),
             1.0);
    // Olympus 便携机 TG-6（1/2.3"）不享受 M43 倍率（宁缺勿错 → 1.0）
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("OLYMPUS"),
                                           QStringLiteral("TG-6")),
             1.0);
    // 富士小底 X10（2/3"）不享受 APSC 倍率
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("FUJIFILM"),
                                           QStringLiteral("X10")),
             1.0);
    // 尼康 D7000 系（APSC 表内）
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("NIKON CORPORATION"),
                                           QStringLiteral("NIKON D7000")),
             1.5);
    // Canon EOS M6 Mark II 命中 "M6"（1.6×）；Nikon Z fc 命中（1.5×）
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("Canon"),
                                           QStringLiteral("Canon EOS M6 Mark II")),
             1.6);
    QCOMPARE(SensorRegistry::cropFactorFor(QStringLiteral("NIKON CORPORATION"),
                                           QStringLiteral("NIKON Z fc")),
             1.5);
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
