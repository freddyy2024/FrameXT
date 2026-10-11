#include "model/TemplateSpec.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
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
    // 版本闸：高于已知版本拒绝解析（调用方提示更新应用，不静默降级）。
    // schemaVersion 迁移链（M5.3 约定）：低于当前版本的输入在解析前
    // 逐级升级——v1 为首个版本暂无历史步骤；未来升版时在此追加
    // migrateV1toV2 等纯函数（旧字段→新字段），保证旧分享包永远可导入。
    // 缺 schemaVersion 记 0，宽容按当前语义读取（与 ModelIo 缺字段回落
    // 默认值约定一致）
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

bool exportTemplate(const TemplateSpec &spec, const QString &filePath,
                    QString *error)
{
    if (error)
        error->clear();
    // QSaveFile 原子写：commit 失败不留半截文件（分享包损坏比导出失败更糟）
    QSaveFile f(filePath);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = QStringLiteral("无法写入文件：%1").arg(filePath);
        return false;
    }
    f.write(specToJson(spec));
    if (!f.commit()) {
        if (error)
            *error = QStringLiteral("写入失败：%1").arg(f.errorString());
        return false;
    }
    return true;
}

QString importTemplate(const QString &filePath, QString *error)
{
    if (error)
        error->clear();
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = QStringLiteral("无法读取文件：%1").arg(filePath);
        return {};
    }
    const QByteArray json = f.readAll();
    bool ok = false;
    int version = 0;
    const TemplateSpec parsed = specFromJson(json, &ok, &version);
    if (!ok) {
        if (error) {
            // 版本闸单独提示（用户可操作：升级应用）；其余归为文件损坏
            *error = version > kSchemaVersion
                ? QStringLiteral("模板包版本较新（schema v%1），请更新应用后再导入")
                      .arg(version)
                : QStringLiteral("模板包内容无效或已损坏");
        }
        return {};
    }
    // 冲突避让：id 撞内置/现有用户模板时清空 → saveUserTemplate 生成
    // 新 UUID（specFor 未命中回落 bottom，其 id 与目标 id 不同即未命中）
    TemplateSpec toInstall = parsed;
    toInstall.builtin = false;   // 分享包永远是用户模板身份
    if (specFor(toInstall.id).id == toInstall.id)
        toInstall.id.clear();
    const QString newId = saveUserTemplate(toInstall);
    if (newId.isEmpty()) {
        if (error)
            *error = QStringLiteral("模板安装失败（用户模板目录不可写）");
        return {};
    }
    return newId;
}

} // namespace TemplateCatalog
} // namespace fxt
