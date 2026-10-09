#include "render/RenderCacheManager.h"

namespace fxt {

RenderCacheManager::RenderCacheManager(qint64 maxBytes)
    : m_maxBytes(maxBytes)
{
}

RenderCacheManager::~RenderCacheManager() = default;

QImage RenderCacheManager::backdrop(const QString &key) const
{
    const QMutexLocker lock(&m_mutex);
    const auto it = m_entries.constFind(key);
    if (it == m_entries.constEnd()) {
        ++m_misses;
        return QImage();
    }
    ++m_hits;
    // LRU 触达：移到最新
    m_order.removeOne(key);
    m_order.append(key);
    return it.value().image;
}

void RenderCacheManager::insertBackdrop(const QString &key, const QImage &image)
{
    if (image.isNull())
        return;
    const QMutexLocker lock(&m_mutex);
    const qint64 bytes = qint64(image.sizeInBytes());

    if (bytes > m_maxBytes)   // 单条超上限：不缓存（超大图直接渲染）
        return;

    // 覆盖已有条目：先扣减旧体积
    if (const auto it = m_entries.constFind(key); it != m_entries.constEnd()) {
        m_totalBytes -= it.value().bytes;
        m_order.removeOne(key);
    }

    m_entries.insert(key, Entry { image, bytes });
    m_order.append(key);
    m_totalBytes += bytes;
    evictLocked();
}

void RenderCacheManager::clear()
{
    const QMutexLocker lock(&m_mutex);
    m_entries.clear();
    m_order.clear();
    m_totalBytes = 0;
}

void RenderCacheManager::evictLocked()
{
    while (m_totalBytes > m_maxBytes && !m_order.isEmpty()) {
        const QString oldest = m_order.takeFirst();
        const auto it = m_entries.constFind(oldest);
        if (it != m_entries.constEnd()) {
            m_totalBytes -= it.value().bytes;
            m_entries.erase(it);
        }
    }
}

} // namespace fxt
