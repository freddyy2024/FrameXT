#pragma once

#include <QString>

namespace fxt {

// 画幅基准与等效焦距换算（02-技术设计 §7.3 / 方案 §7.3，回应反馈 R8）：
// 等效展示 = Exif FocalLengthIn35mmFilm（全画幅基准，优先直接取），
// 缺失时按机型传感器规格换算：实际焦距 × cropFactor。
// 未收录机型回落全画幅（1.0，不换算——宁可不出 eq. 也不出错误 eq.）。
namespace SensorRegistry {

// 常见画幅基准倍率（相对全画幅）
enum class CropBase {
    FullFrame = 100,   // 1.0×（全画幅/35mm）
    ApsC = 150,        // 1.5×（Sony/尼康/富士 APSC）
    CanonApsC = 160,   // 1.6×（佳能 APSC）
    M43 = 200,         // 2.0×（M4/3）
    Medium4433 = 79,   // 0.79×（中画幅 44×33：GFX/X2D）
};

double factorOf(CropBase base);

// make+model → 裁切倍率（机型自动表；未收录回落 1.0）。
// 大小写不敏感，子串匹配（"OM-1 Mark II" 命中 "OM-1"）。
double cropFactorFor(const QString &make, const QString &model);

// 等效焦距合成：exif35mm 有效时直接取；否则 factor × 实际焦距
//（factor≈1.0 且无 exif35mm → 返回空，不产出 "50mm eq." 这种废话值）
double effectiveFocal(double actualMm, double exif35mm, double cropFactor);

} // namespace SensorRegistry

} // namespace fxt
