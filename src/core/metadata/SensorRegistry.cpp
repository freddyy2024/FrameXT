#include "metadata/SensorRegistry.h"

#include <cmath>

namespace fxt {

namespace {
// 机型传感器规格库（子串匹配，先长后短防误命中；未收录回落全画幅）。
// 覆盖常见热门机身；长尾机型回落 1.0（宁缺勿错）。
struct SensorRule
{
    const char *modelKey;   // model 子串
    SensorRegistry::CropBase base;
};

const SensorRule kRules[] = {
    // —— 中画幅（先判，防被品牌默认吞掉）——
    { "GFX", SensorRegistry::CropBase::Medium4433 },
    { "X1D", SensorRegistry::CropBase::Medium4433 },
    { "X2D", SensorRegistry::CropBase::Medium4433 },
    { "907X", SensorRegistry::CropBase::Medium4433 },
    // —— M4/3（松下 G 系/Olympus 全系）——
    { "GH", SensorRegistry::CropBase::M43 },
    { "G9", SensorRegistry::CropBase::M43 },
    { "G95", SensorRegistry::CropBase::M43 },
    { "G85", SensorRegistry::CropBase::M43 },
    { "GX", SensorRegistry::CropBase::M43 },
    { "GM", SensorRegistry::CropBase::M43 },
    { "LX100", SensorRegistry::CropBase::M43 },
    { "OM-1", SensorRegistry::CropBase::M43 },
    { "OM-5", SensorRegistry::CropBase::M43 },
    { "E-M1", SensorRegistry::CropBase::M43 },
    { "E-M5", SensorRegistry::CropBase::M43 },
    { "E-M10", SensorRegistry::CropBase::M43 },
    { "E-PL", SensorRegistry::CropBase::M43 },
    { "PEN", SensorRegistry::CropBase::M43 },
    // —— 佳能 APSC（1.6×；R 全幅系靠默认 1.0 兜底）——
    { "R7", SensorRegistry::CropBase::CanonApsC },
    { "R10", SensorRegistry::CropBase::CanonApsC },
    { "R50", SensorRegistry::CropBase::CanonApsC },
    { "R100", SensorRegistry::CropBase::CanonApsC },
    { "90D", SensorRegistry::CropBase::CanonApsC },
    { "80D", SensorRegistry::CropBase::CanonApsC },
    { "70D", SensorRegistry::CropBase::CanonApsC },
    { "850D", SensorRegistry::CropBase::CanonApsC },
    { "800D", SensorRegistry::CropBase::CanonApsC },
    { "77D", SensorRegistry::CropBase::CanonApsC },
    { "7D", SensorRegistry::CropBase::CanonApsC },
    { "M50", SensorRegistry::CropBase::CanonApsC },
    { "M6", SensorRegistry::CropBase::CanonApsC },
    // —— 尼康 APSC（1.5×）——
    { "Z50", SensorRegistry::CropBase::ApsC },
    { "Z30", SensorRegistry::CropBase::ApsC },
    { "Z fc", SensorRegistry::CropBase::ApsC },
    { "Zfc", SensorRegistry::CropBase::ApsC },
    { "D500", SensorRegistry::CropBase::ApsC },
    { "D7500", SensorRegistry::CropBase::ApsC },
    { "D7200", SensorRegistry::CropBase::ApsC },
    { "D7100", SensorRegistry::CropBase::ApsC },
    { "D5600", SensorRegistry::CropBase::ApsC },
    { "D5300", SensorRegistry::CropBase::ApsC },
    { "D3500", SensorRegistry::CropBase::ApsC },
    { "D3400", SensorRegistry::CropBase::ApsC },
    // —— 索尼 APSC（1.5×；A7/A9/A1 全幅靠默认兜底）——
    { "A6000", SensorRegistry::CropBase::ApsC },
    { "A6100", SensorRegistry::CropBase::ApsC },
    { "A6300", SensorRegistry::CropBase::ApsC },
    { "A6400", SensorRegistry::CropBase::ApsC },
    { "A6500", SensorRegistry::CropBase::ApsC },
    { "A6600", SensorRegistry::CropBase::ApsC },
    { "A6700", SensorRegistry::CropBase::ApsC },
    { "ZV-E10", SensorRegistry::CropBase::ApsC },
    { "A5000", SensorRegistry::CropBase::ApsC },
    { "A5100", SensorRegistry::CropBase::ApsC },
    { "NEX", SensorRegistry::CropBase::ApsC },
    // —— 富士：GFX 已在中画幅，其余 X 系全 APSC ——
    { "X100", SensorRegistry::CropBase::ApsC },
    { "X-T", SensorRegistry::CropBase::ApsC },
    { "X-E", SensorRegistry::CropBase::ApsC },
    { "X-S", SensorRegistry::CropBase::ApsC },
    { "X-Pro", SensorRegistry::CropBase::ApsC },
    { "X-A", SensorRegistry::CropBase::ApsC },
    { "X-H", SensorRegistry::CropBase::ApsC },
    // —— 理光 GR（APSC）——
    { "GR III", SensorRegistry::CropBase::ApsC },
    { "GR II", SensorRegistry::CropBase::ApsC },
    { "GR IIIx", SensorRegistry::CropBase::ApsC },
};
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
    const QString m = model;
    if (m.isEmpty())
        return 1.0;
    // 中画幅品牌默认（未含机型串的 Hasselblad 回退）
    if (m.contains(QLatin1String("GFX"), Qt::CaseInsensitive))
        return factorOf(CropBase::Medium4433);
    // Olympus/OM 全系 M4/3
    if (make.contains(QLatin1String("Olympus"), Qt::CaseInsensitive)
        || make.contains(QLatin1String("OM Digital"), Qt::CaseInsensitive)
        || make.contains(QLatin1String("OMDS"), Qt::CaseInsensitive))
        return factorOf(CropBase::M43);
    // 富士非 GFX 全 APSC（GFX 已被上面拦截）
    if (make.contains(QLatin1String("FUJIFILM"), Qt::CaseInsensitive))
        return factorOf(CropBase::ApsC);

    for (const SensorRule &rule : kRules) {
        if (m.contains(QLatin1String(rule.modelKey), Qt::CaseInsensitive))
            return factorOf(rule.base);
    }
    return 1.0;
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
