#pragma once

#include <QJsonObject>

#include "Element.h"
#include "PhotoDocument.h"
#include "TemplateState.h"

namespace fxt::ModelIo {

// 领域模型 ↔ JSON（模板文件 .ftpl / 项目持久化共用）。
// 容错约定：缺字段回落默认值（schema 演进向后兼容），未知字段忽略，非法 id 自动补 UUID。
QJsonObject toJson(const Element &element);
Element elementFromJson(const QJsonObject &object);

QJsonObject toJson(const TemplateState &state);
TemplateState templateStateFromJson(const QJsonObject &object);

QJsonObject toJson(const PhotoDocument &document);
PhotoDocument photoDocumentFromJson(const QJsonObject &object);

} // namespace fxt::ModelIo
