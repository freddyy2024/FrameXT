#pragma once

#include <QList>
#include <QString>

namespace fxt {

// 品牌定义（02-技术设计 §4.6）：别名/前缀匹配 → 规范化显示文本 + Logo。
// 数据源（M5.4 起）：resources/brands.json（qrc）为唯一准绳，C++ 内置表
//（builtin()）仅作 JSON 损坏时的应急兜底（两处漂移由 test_brand 强制同步）；
// 用户覆盖层 %APPDATA%/brands.json 按 value 合并（同 value 整条替换、
// 新 value 追加表尾），别名/Logo 修正用户可自行编辑。
struct BrandDef
{
    QString value;             // 规范化品牌值（Exif 写回/匹配 key 用）
    QString label;             // 展示文本（Panasonic → "LUMIX" 等）
    QStringList makeAliases;   // make 包含匹配（大小写不敏感；如 "NIKON"）
    QStringList modelPrefixes; // model 前缀匹配（如 "OM-1" 命中 "OM-1 Mark II"）
    QString logoPath;          // 相对资源路径（":/logos/camera/..."）

    bool operator==(const BrandDef &) const = default;
};

// 品牌识别注册表（§4.6）：原 16 品牌 + OM System（修 #13）+ 手机常见品牌
// 文本规范化。未命中回落 Others（others.png）。
namespace BrandRegistry {

// C++ 应急兜底表（仅 qrc brands.json 解析失败时被 loadBrandTable 采用；
// 正常运行时 match() 走 JSON 数据源）。公开给测试做资源一致性对账。
QList<BrandDef> builtin();

// make+model → 命中品牌（未命中返回 Others 兜底定义，永不返回空）。
// 匹配序：make 别名包含 → model 前缀（先判专用前缀再判通用别名）。
BrandDef match(const QString &make, const QString &model);

// Others 兜底（品牌未识别时 Logo 回落 + 文本用原始 make；可被用户
// 覆盖层的 fallback 条目替换）
BrandDef others();

// —— 用户覆盖层（M5.4，E5 品牌别名库）——
// 覆盖文件缺省 = AppPaths::appDataRoot()/brands.json（便携模式随程序
// 目录），通常由用户手动创建
// （或未来 Logo 管理器写入）。空串 = 还原缺省路径。文件缺失/损坏时
// 忽略覆盖层（qWarning），基底表照常工作——品牌识别零崩溃红线。
void setUserBrandsFile(const QString &path);
QString userBrandsFile();

} // namespace BrandRegistry

} // namespace fxt
