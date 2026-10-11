#include "model/TemplateSpec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUuid>

#include <algorithm>

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

namespace {

QString &userDir()
{
    static QString s_dir = [] {
        const QString base =
            QStandardPaths::writableLocation(
                QStandardPaths::AppLocalDataLocation);
        return base + QLatin1String("/templates");
    }();
    return s_dir;
}

QList<TemplateSpec> loadUserTemplates()
{
    QList<TemplateSpec> out;
    QDir dir(userDir());
    if (!dir.exists())
        return out;
    // JSON 文件名即模板 id（save 时落 <id>.json），固定 .ftpl 扩展名分享由
    // M5.3 导入导出负责；此处只认自家 *.json
    for (const QFileInfo &info :
         dir.entryInfoList({ QStringLiteral("*.json") }, QDir::Files,
                           QDir::Name)) {
        QFile f(info.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly))
            continue;
        bool ok = false;
        const TemplateSpec s = specFromJson(f.readAll(), &ok);
        if (ok)
            out.append(s);
    }
    std::sort(out.begin(), out.end(),
              [](const TemplateSpec &a, const TemplateSpec &b) {
                  return a.name.localeAwareCompare(b.name) < 0;
              });
    return out;
}

// 用户模板内存缓存（specFor 渲染每帧调用，不能读盘）。
// 写操作（save/delete/setDir）后 refresh。
QList<TemplateSpec> &userCache()
{
    static QList<TemplateSpec> s_cache = loadUserTemplates();
    return s_cache;
}

void refreshUserCache() { userCache() = loadUserTemplates(); }

} // namespace

QList<TemplateSpec> allSpecs()
{
    QList<TemplateSpec> out = builtinSpecs();
    out.append(userCache());
    return out;
}

TemplateSpec specFor(const QString &templateId)
{
    for (const TemplateSpec &s : builtinSpecs())
        if (s.id == templateId)
            return s;
    for (const TemplateSpec &s : userCache())
        if (s.id == templateId)
            return s;
    return bottomSpec();   // 未知 id 回落（含空/已删除 id；宽容处理）
}

void setUserTemplatesDir(const QString &dir)
{
    userDir() = dir;
    refreshUserCache();
}

QString userTemplatesDir() { return userDir(); }

QString saveUserTemplate(const TemplateSpec &spec)
{
    TemplateSpec out = spec;
    out.builtin = false;
    if (out.id.isEmpty()) {
        out.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    } else if (out.id == QLatin1String("bottom")
               || out.id == QLatin1String("around")
               || out.id == QLatin1String("blur")) {
        return {};   // 内置 id 不可覆盖（另存为必须生成新 id）
    }
    QDir dir(userDir());
    if (!dir.exists())
        QDir().mkpath(userDir());
    QFile f(dir.filePath(out.id + QLatin1String(".json")));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return {};
    f.write(specToJson(out));
    // ⚠️ 必须 close 落盘再刷新缓存：Qt6 Windows 文件引擎对写入持有
    // 内存缓冲（close/flush 才写盘），先 refresh 会读到空文件、解析
    // 失败静默跳过（探针实证：二次 setDir 后才可见）
    f.close();
    refreshUserCache();
    return out.id;
}

bool deleteUserTemplate(const QString &templateId)
{
    if (templateId == QLatin1String("bottom")
        || templateId == QLatin1String("around")
        || templateId == QLatin1String("blur"))
        return false;
    const bool removed =
        QFile::remove(QDir(userDir()).filePath(templateId
                                               + QLatin1String(".json")));
    if (removed)
        refreshUserCache();
    return removed;
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
