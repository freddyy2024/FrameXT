#include "metadata/BrandRegistry.h"

#include "AppPaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>

namespace fxt {

namespace {
// 内置品牌表：logo 对应 resources/logos/camera/ 现有文件（qrc 前缀由
// app 层 resources.qrc 挂载；core 只持有路径字符串）。
// 别名覆盖 Exif 实测变体（NIKON CORPORATION / FUJIFILM / OM Digital Solutions 等）。
QList<BrandDef> makeBuiltin()
{
    return {
        { QStringLiteral("Canon"), QStringLiteral("Canon"),
          { QStringLiteral("Canon") }, {}, QStringLiteral(":/logos/camera/Canon_1440_301.png") },
        { QStringLiteral("Nikon"), QStringLiteral("Nikon"),
          { QStringLiteral("Nikon") }, {},
          QStringLiteral(":/logos/camera/Nikon-700-700.png") },
        { QStringLiteral("Sony"), QStringLiteral("Sony"),
          { QStringLiteral("Sony") }, { QStringLiteral("ILCE"), QStringLiteral("NEX") },
          QStringLiteral(":/logos/camera/sony-logo-1100-194.png") },
        { QStringLiteral("Fujifilm"), QStringLiteral("Fujifilm"),
          { QStringLiteral("FUJIFILM"), QStringLiteral("Fujifilm") },
          { QStringLiteral("X100"), QStringLiteral("X-T"), QStringLiteral("X-E"),
            QStringLiteral("X-S"), QStringLiteral("X-Pro"), QStringLiteral("X-A"),
            QStringLiteral("X-H"), QStringLiteral("GFX") },
          QStringLiteral(":/logos/camera/fujifilm-logo-1100-183.png") },
        { QStringLiteral("Olympus"), QStringLiteral("OM System"),
          { QStringLiteral("Olympus"), QStringLiteral("OM Digital Solutions"),
            QStringLiteral("OMDS"), QStringLiteral("OM SYSTEM") },
          { QStringLiteral("OM-1"), QStringLiteral("OM-5"), QStringLiteral("OM-D"),
            QStringLiteral("E-M1"), QStringLiteral("E-M5"), QStringLiteral("E-M10"),
            QStringLiteral("PEN") },
          // M5.5 资产复核（R3）：OM Digital 机器显示官方现行 OM SYSTEM
          // 方形徽标，替换旧 OLYMPUS 文字标
          QStringLiteral(":/logos/camera/omsystem-logo-1280-1280.png") },
        { QStringLiteral("Panasonic"), QStringLiteral("LUMIX"),
          { QStringLiteral("Panasonic"), QStringLiteral("LUMIX") },
          { QStringLiteral("DC-"), QStringLiteral("DMC-") },
          // R3（5 赞）：相机产品线显示 LUMIX 标而非 Panasonic 集团标
          QStringLiteral(":/logos/camera/lumix-logo-1920-438.png") },
        { QStringLiteral("Leica"), QStringLiteral("Leica"),
          { QStringLiteral("Leica") }, {}, QStringLiteral(":/logos/camera/leica-logo-700-700.png") },
        { QStringLiteral("Hasselblad"), QStringLiteral("Hasselblad"),
          { QStringLiteral("Hasselblad") }, {},
          QStringLiteral(":/logos/camera/hasselblad-logo-1400-146.png") },
        // 理光/宾得共用真机 make 串 "RICOH IMAGING COMPANY, LTD."（GR 系与
        // K 系新机都如此），仅靠表序无法消歧——match() 在 make 命中多个
        // 品牌时按 model 再判（GR → Ricoh / K-|PENTAX → Pentax）
        { QStringLiteral("Ricoh"), QStringLiteral("Ricoh"),
          { QStringLiteral("Ricoh"), QStringLiteral("RICOH IMAGING") },
          { QStringLiteral("GR") },
          QStringLiteral(":/logos/camera/ricoh-logo-1440-260.png") },
        { QStringLiteral("Pentax"), QStringLiteral("Pentax"),
          { QStringLiteral("Pentax"), QStringLiteral("PENTAX"),
            QStringLiteral("RICOH IMAGING") },
          { QStringLiteral("K-"), QStringLiteral("KP"), QStringLiteral("PENTAX") },
          QStringLiteral(":/logos/camera/Pentax_Logo-662-133.png") },
        { QStringLiteral("Sigma"), QStringLiteral("Sigma"),
          { QStringLiteral("Sigma") }, { QStringLiteral("fp"), QStringLiteral("dp") },
          QStringLiteral(":/logos/camera/sigma-logo-2500-532.png") },
        { QStringLiteral("Phase One"), QStringLiteral("Phase One"),
          { QStringLiteral("Phase One") }, { QStringLiteral("IQ"), QStringLiteral("XF") },
          QStringLiteral(":/logos/camera/Phase_One_logo-1280-177.png") },
        { QStringLiteral("DJI"), QStringLiteral("DJI"),
          { QStringLiteral("DJI") }, {}, QStringLiteral(":/logos/camera/dji-logo-700-413.png") },
        { QStringLiteral("GoPro"), QStringLiteral("GoPro"),
          { QStringLiteral("GoPro") }, {}, QStringLiteral(":/logos/camera/gopro-logo-1600-492.png") },
        // —— 手机品牌：品牌文本规范化，Logo 暂共用水兜底（专属 Logo M5 补）——
        { QStringLiteral("Apple"), QStringLiteral("Apple"),
          { QStringLiteral("Apple"), QStringLiteral("iPhone") }, {},
          QStringLiteral(":/logos/camera/others.png") },
        { QStringLiteral("Huawei"), QStringLiteral("Huawei"),
          { QStringLiteral("HUAWEI"), QStringLiteral("Huawei") },
          { QStringLiteral("P50"), QStringLiteral("Mate"), QStringLiteral("HUAWEI") },
          QStringLiteral(":/logos/camera/others.png") },
        { QStringLiteral("Xiaomi"), QStringLiteral("Xiaomi"),
          { QStringLiteral("Xiaomi"), QStringLiteral("Redmi"), QStringLiteral("MI ") },
          {}, QStringLiteral(":/logos/camera/others.png") },
        { QStringLiteral("Samsung"), QStringLiteral("Samsung"),
          { QStringLiteral("SAMSUNG"), QStringLiteral("Samsung") }, {},
          QStringLiteral(":/logos/camera/others.png") },
        { QStringLiteral("OPPO"), QStringLiteral("OPPO"),
          { QStringLiteral("OPPO") }, {}, QStringLiteral(":/logos/camera/others.png") },
        { QStringLiteral("vivo"), QStringLiteral("vivo"),
          { QStringLiteral("vivo") }, {}, QStringLiteral(":/logos/camera/others.png") },
    };
}

// 大小写不敏感包含匹配
bool containsCI(const QString &haystack, const QString &needle)
{
    return !needle.isEmpty()
        && haystack.contains(needle, Qt::CaseInsensitive);
}

bool startsWithCI(const QString &haystack, const QString &prefix)
{
    return !prefix.isEmpty() && haystack.startsWith(prefix, Qt::CaseInsensitive);
}

// Others 兜底声明（JSON fallback 缺失时的原样语义）
BrandDef makeOthers()
{
    BrandDef def;
    def.value = QStringLiteral("Others");
    def.label = QStringLiteral("Others");
    def.logoPath = QStringLiteral(":/logos/camera/others.png");
    return def;
}

// brands.json 单条解析（字段缺失/非法 → ok=false 整条丢弃）
bool brandFromJson(const QJsonObject &o, BrandDef &out)
{
    out.value = o["value"].toString();
    out.label = o["label"].toString();
    for (const QJsonValue &v : o["aliases"].toArray())
        out.makeAliases.append(v.toString());
    for (const QJsonValue &v : o["modelPrefixes"].toArray())
        out.modelPrefixes.append(v.toString());
    out.logoPath = o["logo"].toString();
    // value/label/logo 是展示与匹配的最低要求；别名数组允许为空
    return !out.value.isEmpty() && !out.label.isEmpty()
        && !out.logoPath.isEmpty();
}

// 完整品牌表：qrc 基底 + 用户覆盖层合并结果 + fallback
struct BrandTable
{
    QList<BrandDef> brands;
    BrandDef fallback;
};

// 解析一份品牌 JSON 文档（基底/覆盖同格式）；结构非法返回 false
bool tableFromJson(const QJsonDocument &doc, BrandTable &out)
{
    if (!doc.isObject())
        return false;
    const QJsonObject o = doc.object();
    BrandTable t;
    t.fallback = makeOthers();
    for (const QJsonValue &v : o["brands"].toArray()) {
        BrandDef b;
        if (brandFromJson(v.toObject(), b))
            t.brands.append(b);
    }
    const QJsonObject fb = o["fallback"].toObject();
    if (!fb.isEmpty()) {
        BrandDef b;
        if (brandFromJson(fb, b))
            t.fallback = b;
    }
    if (t.brands.isEmpty())
        return false;   // 空表视为损坏（识别必然全落 Others，无意义）
    out = t;
    return true;
}

QString &userFile()
{
    static QString s_file = [] {
        // 便携模式（M6.1）下随程序目录（<exeDir>/Data），否则 %APPDATA%
        const QString base = AppPaths::appDataRoot();
        return base + QLatin1String("/brands.json");
    }();
    return s_file;
}

// 加载完整品牌表：qrc 基底 + 用户覆盖层合并
BrandTable loadBrandTable()
{
    // 1) qrc 基底（唯一准绳）；解析失败回落 C++ 内置表（应急兜底，
    //    防资源损坏破坏识别——两处同步由 test_brand 强制）
    BrandTable t;
    t.fallback = makeOthers();
    QFile base(QStringLiteral(":/brands/brands.json"));
    if (base.open(QIODevice::ReadOnly))
        tableFromJson(QJsonDocument::fromJson(base.readAll()), t);
    if (t.brands.isEmpty())
        t.brands = makeBuiltin();
    // 2) 用户覆盖层：同 value 整条替换、新 value 追加表尾、fallback
    //    可整体替换。文件缺失=无覆盖；损坏忽略并告警（不崩）
    QFile uf(userFile());
    if (uf.open(QIODevice::ReadOnly)) {
        BrandTable user;
        if (tableFromJson(QJsonDocument::fromJson(uf.readAll()), user)) {
            for (const BrandDef &ub : user.brands) {
                auto it = std::find_if(t.brands.begin(), t.brands.end(),
                                       [&ub](const BrandDef &b) {
                                           return b.value == ub.value;
                                       });
                if (it != t.brands.end())
                    *it = ub;
                else
                    t.brands.append(ub);
            }
            t.fallback = user.fallback;
        } else {
            qWarning("用户品牌覆盖文件解析失败，已忽略：%s",
                     qPrintable(userFile()));
        }
    }
    return t;
}

// 品牌表内存缓存（match 每次 Exif 展示都会调用，不能反复读盘）。
// setUserBrandsFile 写操作后 refresh。
BrandTable &table()
{
    static BrandTable s_table = loadBrandTable();
    return s_table;
}

void refreshTable() { table() = loadBrandTable(); }

} // namespace

QList<BrandDef> BrandRegistry::builtin()
{
    static const QList<BrandDef> brands = makeBuiltin();
    return brands;
}

BrandDef BrandRegistry::match(const QString &make, const QString &model)
{
    // 第一轮：make 别名包含（"NIKON CORPORATION" contains "Nikon"）。
    // 命中多个品牌时不下结论（理光 GR 与宾得 K 系新机共用 make 串
    // "RICOH IMAGING"），交 model 消歧（评审 P1）
    const QList<BrandDef> brands = table().brands;
    QList<BrandDef> byMake;
    for (const BrandDef &brand : brands) {
        for (const QString &alias : brand.makeAliases) {
            if (containsCI(make, alias)) {
                byMake.append(brand);
                break;
            }
        }
    }
    if (byMake.size() == 1)
        return byMake.first();
    if (byMake.size() > 1) {
        // model 包含消歧（"GR III" → Ricoh；"PENTAX K-3" → Pentax）
        for (const BrandDef &brand : byMake) {
            for (const QString &prefix : brand.modelPrefixes)
                if (containsCI(model, prefix))
                    return brand;
        }
        return byMake.first();   // model 也无法消歧：表序优先
    }
    // 第二轮：make 未命中（空/异常）时 model 前缀兜底（修 #13 场景：
    // OM-1 的 Exif make 为 "OM Digital Solutions" 已走第一轮，此处兜住
    // 缺 make 的旧机型/特殊机身）
    for (const BrandDef &brand : brands) {
        for (const QString &prefix : brand.modelPrefixes)
            if (startsWithCI(model, prefix))
                return brand;
    }
    return others();
}

BrandDef BrandRegistry::others() { return table().fallback; }

void BrandRegistry::setUserBrandsFile(const QString &path)
{
    userFile() = path;
    refreshTable();
}

QString BrandRegistry::userBrandsFile() { return userFile(); }

} // namespace fxt
