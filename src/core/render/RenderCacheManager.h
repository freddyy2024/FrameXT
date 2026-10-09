#pragma once

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QString>
#include <QStringList>

namespace fxt {

// 分层渲染缓存（02-技术设计 §4.2 v1.1）：
// S3 背景结果单独缓存 —— 模板底图/blur 参数/圆角不变即命中，
// 改文字/参数只重跑 S5-S6（预览 <80ms 指标的支撑）。
// 容量上限默认 512MB，LRU 淘汰；线程安全（预览与导出 worker 并发访问）。
class RenderCacheManager
{
public:
    explicit RenderCacheManager(qint64 maxBytes = 512LL * 1024 * 1024);
    ~RenderCacheManager();

    QImage backdrop(const QString &key) const;      // 未命中返回空图
    void insertBackdrop(const QString &key, const QImage &image);
    void clear();

    // 测试与诊断用
    qint64 totalBytes() const { return m_totalBytes; }
    int hits() const { return m_hits; }
    int misses() const { return m_misses; }

private:
    void evictLocked();

    struct Entry {
        QImage image;
        qint64 bytes;
    };
    mutable QMutex m_mutex;
    QHash<QString, Entry> m_entries;
    mutable QStringList m_order;   // LRU 顺序：最旧在前（查询触达也要更新，故 mutable）
    qint64 m_maxBytes;
    qint64 m_totalBytes = 0;
    mutable int m_hits = 0;
    mutable int m_misses = 0;
};

} // namespace fxt
