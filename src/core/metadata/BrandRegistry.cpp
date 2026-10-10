#include "metadata/BrandRegistry.h"

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
          QStringLiteral(":/logos/camera/Olympus-Logo-1440-331.png") },
        { QStringLiteral("Panasonic"), QStringLiteral("LUMIX"),
          { QStringLiteral("Panasonic"), QStringLiteral("LUMIX") },
          { QStringLiteral("DC-"), QStringLiteral("DMC-") },
          QStringLiteral(":/logos/camera/panasonic-logo-1440-228.png") },
        { QStringLiteral("Leica"), QStringLiteral("Leica"),
          { QStringLiteral("Leica") }, {}, QStringLiteral(":/logos/camera/leica-logo-700-700.png") },
        { QStringLiteral("Hasselblad"), QStringLiteral("Hasselblad"),
          { QStringLiteral("Hasselblad") }, {},
          QStringLiteral(":/logos/camera/hasselblad-logo-1400-146.png") },
        { QStringLiteral("Pentax"), QStringLiteral("Pentax"),
          { QStringLiteral("Pentax"), QStringLiteral("RICOH IMAGING") },
          { QStringLiteral("K-"), QStringLiteral("KP") },
          QStringLiteral(":/logos/camera/Pentax_Logo-662-133.png") },
        { QStringLiteral("Ricoh"), QStringLiteral("Ricoh"),
          { QStringLiteral("Ricoh") }, { QStringLiteral("GR") },
          QStringLiteral(":/logos/camera/ricoh-logo-1440-260.png") },
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

} // namespace

QList<BrandDef> BrandRegistry::builtin()
{
    static const QList<BrandDef> brands = makeBuiltin();
    return brands;
}

BrandDef BrandRegistry::match(const QString &make, const QString &model)
{
    // 第一轮：make 别名包含（"NIKON CORPORATION" contains "Nikon"）
    for (const BrandDef &brand : builtin()) {
        for (const QString &alias : brand.makeAliases)
            if (containsCI(make, alias))
                return brand;
    }
    // 第二轮：make 未命中（空/异常）时 model 前缀兜底（修 #13 场景：
    // OM-1 的 Exif make 为 "OM Digital Solutions" 已走第一轮，此处兜住
    // 缺 make 的旧机型/特殊机身）
    for (const BrandDef &brand : builtin()) {
        for (const QString &prefix : brand.modelPrefixes)
            if (startsWithCI(model, prefix))
                return brand;
    }
    return others();
}

BrandDef BrandRegistry::others()
{
    BrandDef def;
    def.value = QStringLiteral("Others");
    def.label = QStringLiteral("Others");
    def.logoPath = QStringLiteral(":/logos/camera/others.png");
    return def;
}

} // namespace fxt
