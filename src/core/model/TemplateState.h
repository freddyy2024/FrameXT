#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include "Element.h"

namespace fxt {

// 一张照片的模板状态：模板 + 参数 + 元素树（02-技术设计 §4.1）。
// 每张照片独立持有（对齐原版"当前图可单独改"），批量应用由 controller 层拷贝状态实现。
// 注：设计 sketch 中的 ElementPtr 在值类型约束（v1.1）下改为值列表——
// 拷贝即深拷贝、JSON 序列化直接、无共享可变状态。
struct TemplateState
{
    QString templateId;           // bottom / around / blur / 用户模板 ID
    QVariantMap templateParams;   // 参数化模板的参数值
    QList<Element> elements;

    bool operator==(const TemplateState &) const = default;
};

} // namespace fxt
