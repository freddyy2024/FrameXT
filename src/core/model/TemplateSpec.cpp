#include "model/TemplateSpec.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace fxt {

// 参数键约定（CanvasGeometry/BackdropRenderer/TemplateState::templateParams
// 三处同一拼写；默认值 = 原版视觉常量，改动会漂移黄金图，勿动）：
namespace TemplateParamKeys {
inline constexpr const char *kExBandLandscape = "exBandRatioLandscape";  // 信息带高（横图 ×W）
inline constexpr const char *kExBandPortrait = "exBandRatioPortrait";    // 信息带高（竖图 ×W）
inline constexpr const char *kAroundInset = "aroundInsetRatio";          // 环绕内边距 ×W
inline constexpr const char *kAroundBottom = "aroundBottomRatio";        // 环绕底边白 ×W
inline constexpr const char *kBlurMargin = "blurMarginRatio";            // 模糊边距 L/R ×W
inline constexpr const char *kBlurBottomGap = "blurBottomGapRatio";      // 模糊底边增量 ×H
inline constexpr const char *kBlurCorner = "blurCornerRadiusRatio";      // 原图圆角 ×W
inline constexpr const char *kBlurStrength = "blurStrength";             // 模糊强度系数（σ=W/20 × 值）
} // namespace TemplateParamKeys

QVariantMap TemplateSpec::defaultParams() const
{
    QVariantMap out;
    for (const TemplateParamDecl &p : params)
        out.insert(p.key, p.defaultValue);
    return out;
}

const TemplateParamDecl *TemplateSpec::param(const QString &key) const
{
    for (const TemplateParamDecl &p : params)
        if (p.key == key)
            return &p;
    return nullptr;
}

namespace TemplateCatalog {
namespace {

// 内置三模板参数表。范围/步进对齐 04-UI §6 滑杆；默认值逐项对应
// CanvasGeometry.cpp / BackdropRenderer.cpp 的视觉常量注释。
TemplateSpec bottomSpec()
{
    TemplateSpec s;
    s.id = QStringLiteral("bottom");
    s.kind = s.id;
    s.name = QStringLiteral("底部边框");
    s.builtin = true;
    s.params = {
        { QString::fromLatin1(TemplateParamKeys::kExBandLandscape),
          QStringLiteral("信息带高度（横图）"), 0.09, 0.04, 0.20, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kExBandPortrait),
          QStringLiteral("信息带高度（竖图）"), 0.10, 0.04, 0.20, 0.005 },
    };
    return s;
}

TemplateSpec aroundSpec()
{
    TemplateSpec s;
    s.id = QStringLiteral("around");
    s.kind = s.id;
    s.name = QStringLiteral("环绕边框");
    s.builtin = true;
    s.params = {
        { QString::fromLatin1(TemplateParamKeys::kExBandLandscape),
          QStringLiteral("信息带高度（横图）"), 0.09, 0.04, 0.20, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kExBandPortrait),
          QStringLiteral("信息带高度（竖图）"), 0.10, 0.04, 0.20, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kAroundInset),
          QStringLiteral("环绕内边距"), 0.02, 0.0, 0.08, 0.002 },
        { QString::fromLatin1(TemplateParamKeys::kAroundBottom),
          QStringLiteral("环绕底边"), 0.02, 0.0, 0.08, 0.002 },
    };
    return s;
}

TemplateSpec blurSpec()
{
    TemplateSpec s;
    s.id = QStringLiteral("blur");
    s.kind = s.id;
    s.name = QStringLiteral("高斯模糊");
    s.builtin = true;
    s.params = {
        { QString::fromLatin1(TemplateParamKeys::kExBandLandscape),
          QStringLiteral("信息带高度（横图）"), 0.09, 0.04, 0.20, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kExBandPortrait),
          QStringLiteral("信息带高度（竖图）"), 0.10, 0.04, 0.20, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kBlurMargin),
          QStringLiteral("模糊边距"), 0.08, 0.04, 0.16, 0.005 },
        { QString::fromLatin1(TemplateParamKeys::kBlurBottomGap),
          QStringLiteral("底边间隙"), 0.01, 0.0, 0.06, 0.002 },
        { QString::fromLatin1(TemplateParamKeys::kBlurCorner),
          QStringLiteral("圆角"), 1.0 / 60.0, 0.0, 0.05, 0.001 },
        { QString::fromLatin1(TemplateParamKeys::kBlurStrength),
          QStringLiteral("模糊强度"), 1.0, 0.4, 2.0, 0.05 },
    };
    return s;
}

QJsonObject paramToJson(const TemplateParamDecl &p)
{
    QJsonObject o;
    o["key"] = p.key;
    o["label"] = p.label;
    o["default"] = p.defaultValue;
    o["min"] = p.minimum;
    o["max"] = p.maximum;
    o["step"] = p.step;
    return o;
}

TemplateParamDecl paramFromJson(const QJsonObject &o, bool *ok)
{
    TemplateParamDecl p;
    p.key = o["key"].toString();
    p.label = o["label"].toString();
    p.defaultValue = o["default"].toDouble();
    p.minimum = o["min"].toDouble();
    p.maximum = o["max"].toDouble();
    p.step = o["step"].toDouble(0.01);
    if (ok)
        *ok = *ok && !p.key.isEmpty() && p.maximum >= p.minimum;
    return p;
}

} // namespace

QList<TemplateSpec> builtinSpecs()
{
    return { bottomSpec(), aroundSpec(), blurSpec() };
}

TemplateSpec specFor(const QString &templateId)
{
    for (const TemplateSpec &s : builtinSpecs())
        if (s.id == templateId)
            return s;
    return bottomSpec();   // 未知 id 回落（含空 id；调用方无需判空）
}

QByteArray specToJson(const TemplateSpec &spec, int schemaVersion)
{
    QJsonArray params;
    for (const TemplateParamDecl &p : spec.params)
        params.append(paramToJson(p));
    QJsonObject o;
    o["schemaVersion"] = schemaVersion;
    o["id"] = spec.id;
    o["kind"] = spec.kind;
    o["name"] = spec.name;
    o["builtin"] = spec.builtin;
    o["params"] = params;
    return QJsonDocument(o).toJson(QJsonDocument::Compact);
}

TemplateSpec specFromJson(const QByteArray &json, bool *ok, int *schemaVersion)
{
    const QJsonDocument doc = QJsonDocument::fromJson(json);
    const bool parseOk = doc.isObject();
    if (schemaVersion)
        *schemaVersion = 0;
    if (ok)
        *ok = false;
    if (!parseOk)
        return {};
    const QJsonObject o = doc.object();
    const int version = o["schemaVersion"].toInt();
    if (schemaVersion)
        *schemaVersion = version;
    // 版本闸：高于已知版本拒绝解析（调用方提示更新应用，不静默降级）
    if (version > kSchemaVersion)
        return {};
    bool paramsOk = true;
    TemplateSpec s;
    s.id = o["id"].toString();
    s.kind = o["kind"].toString();
    s.name = o["name"].toString();
    s.builtin = o["builtin"].toBool(false);
    for (const QJsonValue &v : o["params"].toArray())
        s.params.append(paramFromJson(v.toObject(), &paramsOk));
    const bool valid = paramsOk && !s.id.isEmpty() && !s.kind.isEmpty();
    if (ok)
        *ok = valid;
    return valid ? s : TemplateSpec{};
}

} // namespace TemplateCatalog
} // namespace fxt
