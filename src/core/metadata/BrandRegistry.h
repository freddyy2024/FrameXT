#pragma once

#include <QList>
#include <QString>

namespace fxt {

// 品牌定义（02-技术设计 §4.6）：别名/前缀匹配 → 规范化显示文本 + Logo。
// 数据源：内置表（下）+ resources/brands.json 覆盖层（M5 Logo 管理器
// 读写同一格式；M2 阶段 JSON 未接线，内置表为准）。
struct BrandDef
{
    QString value;             // 规范化品牌值（Exif 写回/匹配 key 用）
    QString label;             // 展示文本（Panasonic → "LUMIX" 等）
    QStringList makeAliases;   // make 包含匹配（大小写不敏感；如 "NIKON"）
    QStringList modelPrefixes; // model 前缀匹配（如 "OM-1" 命中 "OM-1 Mark II"）
    QString logoPath;          // 相对资源路径（":/logos/camera/..."）
};

// 品牌识别注册表（§4.6）：原 16 品牌 + OM System（修 #13）+ 手机常见品牌
// 文本规范化。未命中回落 Others（others.png）。
namespace BrandRegistry {

QList<BrandDef> builtin();

// make+model → 命中品牌（未命中返回 Others 兜底定义，永不返回空）。
// 匹配序：make 别名包含 → model 前缀（先判专用前缀再判通用别名）。
BrandDef match(const QString &make, const QString &model);

// Others 兜底（品牌未识别时 Logo 回落 + 文本用原始 make）
BrandDef others();

} // namespace BrandRegistry

} // namespace fxt
