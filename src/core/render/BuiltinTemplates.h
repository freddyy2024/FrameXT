#pragma once

#include <QList>

#include "model/Element.h"
#include "render/LayoutEngine.h"

namespace fxt::BuiltinTemplates {

// 内置三模板的默认元素集与行组（02-技术设计 §4.3/§4.4）。
// M5 模板参数化后由 resources/templates/*.json 装载；此前的视觉常量
// 集中在本文件（黄金图校准点），勿在别处散落魔法数。

// Exif 字段 key（与 M2 FieldRegistry 对齐）
namespace FieldKeys {
inline constexpr const char *kMake = "make";                 // 相机品牌
inline constexpr const char *kModel = "model";               // 机型
inline constexpr const char *kBrandLogo = "brandLogo";       // 品牌 Logo（专用）
inline constexpr const char *kParamString = "paramString";   // 参数串（焦距/光圈/快门/ISO）
inline constexpr const char *kLens = "lens";                 // 镜头
inline constexpr const char *kDateTime = "dateTime";         // 拍摄日期
inline constexpr const char *kDivider = "divider";           // 分隔线（非 Exif 字段，行组定位用）
} // namespace FieldKeys

// 默认模板规格：元素与行组一次成对生成（导入照片初始化 TemplateState 用）
struct BuiltinSpec
{
    QList<Element> elements;
    QList<LayoutRow> rows;
};

BuiltinSpec defaultSpec();

// 从既有元素列表按 fieldKey 约定重建行组（三模板同构）：
//   第 1 行(60%) = make + brandLogo + divider + paramString
//   第 2 行(40%) = lens + dateTime
// 行组结构是模板的派生事实（UUID 随元素实例化生成，不能预先固定），
// 未匹配到的元素（自定义/自由元素）不参与行组排布。
QList<LayoutRow> defaultRows(const QList<Element> &elements);

} // namespace fxt::BuiltinTemplates
