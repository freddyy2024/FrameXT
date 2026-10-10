#pragma once

#include <QHash>
#include <QList>
#include <QString>

namespace fxt {

// 原始 Exif 值集：exiv2 key（如 "Exif.Image.Make"）→ 原始字符串值。
// 数值型 tag 以字符串承载（"1/160"、"0.3"），格式化器负责解析。
using RawExif = QHash<QString, QString>;

// 展示字段组别（方案 §7.2 六组中的 Exif 四组；文件组/自定义组不经此表）
enum class FieldGroup { Basic, Capture, Lens, Location, Maker };

// 字段格式化方式（ExifFormatter 的分发键）
enum class FieldFormat {
    Raw,            // 原样展示（make/model/software 等）
    Shutter,
    FNumber,
    FocalLength,
    FocalLengthEq,
    Iso,
    ExposureComp,
    MeteringMode,
    Flash,
    WhiteBalance,
    ExposureProgram,
    SceneCaptureType,
    DateTime,       // Exif 日期 → 展示格式
    GpsLatitude,
    GpsLongitude,
    GpsAltitude,
    ApertureValue,  // APEX 光圈值（MaxApertureValue：f=2^(v/2)）
    FujiFilmMode,   // 富士胶片模拟枚举 → 中文名
};

// 展示字段定义（数据驱动：取值链多 tag 回退 + 格式化器）
struct FieldDef
{
    QString key;                  // 展示 key（"make"/"shutter"/...，渲染 resolver 用）
    FieldGroup group = FieldGroup::Basic;
    QStringList tags;             // 取值链（exiv2 key，依次回退）
    FieldFormat format = FieldFormat::Raw;
    bool showByDefault = true;    // 右栏默认展示
};

// FieldRegistry（02-技术设计 §4.5）：方案 §7.2 清单的实现映射。
// 品牌特色字段（MakerNote）按 exiv2 的 Maker key 注册（如
// "Exif.Fujifilm.FilmModel"），解析失败该字段静默置空。
namespace FieldRegistry {

// 全量字段定义表（key → def，含顺序）
QList<FieldDef> allFields();

// 按 key 查找；未注册返回 nullptr
const FieldDef *find(const QString &key);

// RawExif + 注册表 → 展示值集（fieldKey → 格式化后文本；缺失字段不在表内）
QHash<QString, QString> resolveDisplay(const RawExif &raw);

// FieldValueResolver 装配（render 管线直接可用的 std::function）
// overrides：用户手改覆盖层（fieldKey → 值），优先级最高
QHash<QString, QString> resolveDisplay(const RawExif &raw,
                                       const QHash<QString, QString> &overrides);

// 渲染管线的 FieldValueResolver 形态（§4.5 装配点）
// 等价于 resolveDisplay(raw, overrides) 的 lambda 包装（延迟解析由调用方持有结果）

} // namespace FieldRegistry

} // namespace fxt
