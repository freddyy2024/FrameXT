#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include "model/Element.h"

namespace fxt::CustomLogo {

// 元素级自定义 Logo 解析（方案 A：用户为单张照片更换品牌标）。
// customLogoPath 非空时优先加载本地图片文件，空/加载失败回落品牌
// 自动匹配（resolver 结果）——坏图不产生空占位，行为与缺 Logo 一致。
// 渲染管线无状态、在 worker 线程并发执行，会话级缓存需互斥保护；
// QImage 隐式共享，跨线程传递安全
inline QImage resolve(const Element &element, const QImage &brandFallback)
{
    if (element.kind != ElementKind::Logo || element.customLogoPath.isEmpty())
        return brandFallback;

    static QMutex mutex;
    static QHash<QString, QImage> cache;
    const QString path = element.customLogoPath;

    QMutexLocker locker(&mutex);
    const auto it = cache.constFind(path);
    if (it != cache.constEnd())
        return *it;
    locker.unlock();

    QImage img(path);   // 锁外解码（文件 IO 不持锁）
    if (img.isNull())
        return brandFallback;   // 缺失/坏图：回落且不缓存，允许用户修正后重选

    locker.relock();
    cache.insert(path, img);
    return img;
}

} // namespace fxt::CustomLogo
