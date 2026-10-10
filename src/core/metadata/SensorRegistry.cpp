#include "metadata/SensorRegistry.h"

#include <QList>
#include <QStringList>

#include <cmath>

namespace fxt {

namespace {
// 机型传感器规格库（评审 P1 重构）：
//  - makeHint 品牌约束（'|' 分隔多候选，make 包含任一才生效）——跨厂
//    子串误命中的根治（"G9"×Canon G9 X、"PEN"×PENTAX K-1、"7D" 等）
//  - 品牌级宽泛规则已移除（Olympus 全系/T Fujifilm 非 GFX 的假设对
//    TG/STYLUS 便携机、X10/X20 小底机不成立——宁缺勿错，评审 P1）
//  - 未收录回落 1.0（不出 eq.）
struct SensorRule
{
    const char *makeHint;   // 品牌约束（'|' 分隔；空 = 不限品牌）
    const char *modelKey;   // model 子串（大小写不敏感）
    SensorRegistry::CropBase base;
};

const SensorRule kRules[] = {
    // —— 中画幅 44×33（0.79×，先判防被同厂 APSC 规则吞）——
    { "fujifilm", "GFX", SensorRegistry::CropBase::Medium4433 },
    { "hasselblad", "X1D", SensorRegistry::CropBase::Medium4433 },
    { "hasselblad", "X2D", SensorRegistry::CropBase::Medium4433 },
    { "hasselblad", "907X", SensorRegistry::CropBase::Medium4433 },
    // —— 松下 M4/3（"G1" 覆盖 G100/G110，"G9" 覆盖 G90/G95）——
    { "panasonic", "GH", SensorRegistry::CropBase::M43 },
    { "panasonic", "GX", SensorRegistry::CropBase::M43 },
    { "panasonic", "GM", SensorRegistry::CropBase::M43 },
    { "panasonic", "G1", SensorRegistry::CropBase::M43 },
    { "panasonic", "G2", SensorRegistry::CropBase::M43 },
    { "panasonic", "G3", SensorRegistry::CropBase::M43 },
    { "panasonic", "G5", SensorRegistry::CropBase::M43 },
    { "panasonic", "G6", SensorRegistry::CropBase::M43 },
    { "panasonic", "G7", SensorRegistry::CropBase::M43 },
    { "panasonic", "G8", SensorRegistry::CropBase::M43 },
    { "panasonic", "G9", SensorRegistry::CropBase::M43 },
    { "panasonic", "LX100", SensorRegistry::CropBase::M43 },
    { "panasonic", "BGH", SensorRegistry::CropBase::M43 },
    // —— 奥之心/Olympus M4/3（"E-M"/"E-P" 覆盖 E-M1/E-M5/E-M10 与
    //    E-P5/E-PL 系；TG/STYLUS 便携机不命中——1/2.3" 小底不出 eq.）——
    { "olympus|om digital|omds", "OM-", SensorRegistry::CropBase::M43 },
    { "olympus|om digital|omds", "E-M", SensorRegistry::CropBase::M43 },
    { "olympus|om digital|omds", "E-P", SensorRegistry::CropBase::M43 },
    { "olympus|om digital|omds", "PEN", SensorRegistry::CropBase::M43 },
    // —— 佳能 APSC（1.6×；R/5D/6D 等全幅无规则命中回落 1.0）——
    { "canon", "R7", SensorRegistry::CropBase::CanonApsC },
    { "canon", "R10", SensorRegistry::CropBase::CanonApsC },
    { "canon", "R50", SensorRegistry::CropBase::CanonApsC },
    { "canon", "R100", SensorRegistry::CropBase::CanonApsC },
    { "canon", "90D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "80D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "77D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "70D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "850D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "800D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "7D", SensorRegistry::CropBase::CanonApsC },
    { "canon", "M50", SensorRegistry::CropBase::CanonApsC },
    { "canon", "M6", SensorRegistry::CropBase::CanonApsC },
    // —— 尼康 APSC（1.5×；D3/D4/D5/D6/D700/D800/D810/D850/Z 全幅系
    //    无规则命中回落 1.0）——
    { "nikon", "Z50", SensorRegistry::CropBase::ApsC },
    { "nikon", "Z30", SensorRegistry::CropBase::ApsC },
    { "nikon", "Z fc", SensorRegistry::CropBase::ApsC },
    { "nikon", "Zfc", SensorRegistry::CropBase::ApsC },
    { "nikon", "D500", SensorRegistry::CropBase::ApsC },   // D5000 系同命中，同 APSC
    { "nikon", "D7000", SensorRegistry::CropBase::ApsC },
    { "nikon", "D7500", SensorRegistry::CropBase::ApsC },
    { "nikon", "D7200", SensorRegistry::CropBase::ApsC },
    { "nikon", "D7100", SensorRegistry::CropBase::ApsC },
    { "nikon", "D5600", SensorRegistry::CropBase::ApsC },
    { "nikon", "D5300", SensorRegistry::CropBase::ApsC },
    { "nikon", "D5200", SensorRegistry::CropBase::ApsC },
    { "nikon", "D5100", SensorRegistry::CropBase::ApsC },
    { "nikon", "D3500", SensorRegistry::CropBase::ApsC },
    { "nikon", "D3400", SensorRegistry::CropBase::ApsC },
    { "nikon", "D300", SensorRegistry::CropBase::ApsC },
    { "nikon", "D90", SensorRegistry::CropBase::ApsC },
    // —— 索尼 APSC（A7/A9/A1 全幅无规则命中回落 1.0）——
    { "sony", "A6000", SensorRegistry::CropBase::ApsC },
    { "sony", "A6100", SensorRegistry::CropBase::ApsC },
    { "sony", "A6300", SensorRegistry::CropBase::ApsC },
    { "sony", "A6400", SensorRegistry::CropBase::ApsC },
    { "sony", "A6500", SensorRegistry::CropBase::ApsC },
    { "sony", "A6600", SensorRegistry::CropBase::ApsC },
    { "sony", "A6700", SensorRegistry::CropBase::ApsC },
    { "sony", "ZV-E10", SensorRegistry::CropBase::ApsC },
    { "sony", "A5000", SensorRegistry::CropBase::ApsC },
    { "sony", "A5100", SensorRegistry::CropBase::ApsC },
    { "sony", "NEX", SensorRegistry::CropBase::ApsC },
    // —— 富士 X 系可换镜头 + X100 固定头（均 APSC；X10/X20/XF10 为
    //    2/3" 小底，无规则命中不出 eq.）——
    { "fujifilm", "X100", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-T", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-E", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-S", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-Pro", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-A", SensorRegistry::CropBase::ApsC },
    { "fujifilm", "X-H", SensorRegistry::CropBase::ApsC },
    // —— 理光 GR（APSC）——
    { "ricoh", "GR", SensorRegistry::CropBase::ApsC },
};

// 预编译规则（makeHint 拆分为候选列表；首次调用构建，只读共享）
struct CompiledRule
{
    QStringList makeHints;   // 空 = 不限品牌
    QString modelKey;
    SensorRegistry::CropBase base;
};

const QList<CompiledRule> &compiledRules()
{
    static const QList<CompiledRule> compiled = [] {
        QList<CompiledRule> out;
        out.reserve(sizeof(kRules) / sizeof(kRules[0]));
        for (const SensorRule &r : kRules) {
            CompiledRule c;
            const QString hints = QString::fromLatin1(r.makeHint);
            if (!hints.isEmpty())
                c.makeHints = hints.split(QLatin1Char('|'));
            c.modelKey = QString::fromLatin1(r.modelKey);
            c.base = r.base;
            out.append(c);
        }
        return out;
    }();
    return compiled;
}
} // namespace

double SensorRegistry::factorOf(CropBase base)
{
    switch (base) {
    case CropBase::FullFrame: return 1.0;
    case CropBase::ApsC: return 1.5;
    case CropBase::CanonApsC: return 1.6;
    case CropBase::M43: return 2.0;
    case CropBase::Medium4433: return 0.79;
    }
    return 1.0;
}

double SensorRegistry::cropFactorFor(const QString &make, const QString &model)
{
    const QString m = model.trimmed();
    if (m.isEmpty())
        return 1.0;
    for (const CompiledRule &rule : compiledRules()) {
        // 品牌约束：make 须包含任一 hint（大小写不敏感），否则跳过——
        // 这是防跨厂子串误命中的关键
        bool hintOk = rule.makeHints.isEmpty();
        for (const QString &hint : rule.makeHints) {
            if (make.contains(hint, Qt::CaseInsensitive)) {
                hintOk = true;
                break;
            }
        }
        if (!hintOk)
            continue;
        if (m.contains(rule.modelKey, Qt::CaseInsensitive))
            return factorOf(rule.base);
    }
    return 1.0;   // 未收录：不出 eq.（宁缺勿错）
}

double SensorRegistry::effectiveFocal(double actualMm, double exif35mm,
                                      double cropFactor)
{
    if (exif35mm > 0)
        return exif35mm;   // 全画幅基准直接取
    if (actualMm > 0 && cropFactor > 0 && !qFuzzyCompare(cropFactor, 1.0))
        return actualMm * cropFactor;
    return 0;   // 无换算依据：不出 eq.（宁缺勿错）
}

} // namespace fxt
