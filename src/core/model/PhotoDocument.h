#pragma once

#include <QString>
#include <QUuid>

#include "ExifOverrides.h"
#include "PhotoMeta.h"
#include "TemplateState.h"

namespace fxt {

// 一张照片 = 一个文档（02-技术设计 §4.1：纯数据，无缓存、无行为）。
// v1.1：RenderCache 移出文档（属渲染关注点，见 render/RenderCacheManager）；
//       分组概念不做（YAGNI）。
struct PhotoDocument
{
    QUuid id;                     // 缓存 key 组件（photoId）
    QString sourcePath;
    PhotoMeta meta;               // 导入时解析一次
    TemplateState templateState;  // 每张独立
    ExifOverrides overrides;      // 用户改写层

    bool operator==(const PhotoDocument &) const = default;
};

} // namespace fxt
