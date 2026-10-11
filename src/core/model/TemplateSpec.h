#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

namespace fxt {

// 模板参数声明（M5.1 模板参数化，03-开发计划 §7 / 04-UI §6）：
// 一个可调参数的取值范围与默认值。默认值 = 原版视觉常量（黄金图校准点），
// 运行时由 TemplateState::templateParams 按键覆盖（渲染层 paramOf 约定：
// 缺失/越界一律回落默认，保证旧文档与外部模板包向前兼容）。
struct TemplateParamDecl
{
    QString key;      // 参数键（渲染层/TemplateState::templateParams 统一约定）
    QString label;    // 显示名（简中源文，UI 层走 tr）
    double defaultValue = 0;
    double minimum = 0;
    double maximum = 1;
    double step = 0.01;   // 滑杆步进（UI 档位换算用）

    bool operator==(const TemplateParamDecl &) const = default;
};

// 模板规格。M5.1 只含参数层（内置三模板的画布/模糊可调项）；
// 5.2/5.3 的模板管理器与 .ftpl 再扩展 elements/rows 进完整模板包 JSON。
struct TemplateSpec
{
    QString id;     // 与 TemplateState::templateId 对应
    QString kind;   // 画布路径：bottom / around / blur（CanvasGeometry 分支）
    QString name;   // 显示名
    bool builtin = true;
    QList<TemplateParamDecl> params;

    QVariantMap defaultParams() const;   // key -> defaultValue 快捷表
    const TemplateParamDecl *param(const QString &key) const;
    bool operator==(const TemplateSpec &) const = default;
};

// 模板注册表（M5.2：内置三模板 + 用户模板 %APPDATA%/templates/*.json）。
// 用户模板 = 内置 kind + 参数集（元素行组编辑挂账，需行组一等存储前置）。
// specFor 渲染每帧调用——用户模板带内存缓存，写操作后 refresh。
namespace TemplateCatalog {

QList<TemplateSpec> builtinSpecs();
// 内置 + 用户模板（用户模板按 name 排序在后）
QList<TemplateSpec> allSpecs();
TemplateSpec specFor(const QString &templateId);   // 未知 id 回落 bottom

// 用户模板持久化。目录缺省 = QStandardPaths::AppLocalDataLocation
// /templates；测试注入临时目录（内存缓存同步失效）。
void setUserTemplatesDir(const QString &dir);
QString userTemplatesDir();
// 保存（id 已存在则覆盖；新模板 id 为空时生成 UUID）。返回实际 id。
QString saveUserTemplate(const TemplateSpec &spec);
bool deleteUserTemplate(const QString &templateId);   // 内置 id 返回 false

// 模板 JSON（schemaVersion=1）：
// { "schemaVersion": 1, "id", "kind", "name", "builtin",
//   "params": [ { key, label, default, min, max, step } ] }
QByteArray specToJson(const TemplateSpec &spec, int schemaVersion = 1);
// 解析失败返回空 spec（*ok = false）；schemaVersion 高于已知时同样失败
// （拒绝静默降级——由调用方提示更新应用）
TemplateSpec specFromJson(const QByteArray &json, bool *ok = nullptr,
                          int *schemaVersion = nullptr);
constexpr int kSchemaVersion = 1;

} // namespace TemplateCatalog
} // namespace fxt
