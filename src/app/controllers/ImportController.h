#pragma once

#include <QElapsedTimer>
#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QStringList>

#include "model/PhotoDocument.h"

namespace fxt {

// 导入流水线（开发计划 3.2 / 04-UI §5.1）：
// QtConcurrent 并行解析（解码+缩略图+Exif），失败收集汇总（E10，不逐个弹窗）。
struct ImportFailure
{
    QString path;
    QString reason;
};

// 单张导入工作项（线程池内执行，经 QFuture 传递）
struct ImportItem
{
    QString path;
    PhotoDocument doc;
    QImage thumbnail;
    QString error;
};

class ImportController : public QObject
{
    Q_OBJECT

public:
    explicit ImportController(QObject *parent = nullptr);

    bool busy() const { return m_busy; }

    // 支持的格式过滤（E8 格式校验：JPEG/PNG/WebP/TIFF）
    static QStringList supportedSuffixes();

public slots:
    void importFiles(const QStringList &paths);

signals:
    void documentReady(const fxt::PhotoDocument &doc, const QImage &thumbnail);
    void importFinished(int okCount,
                        const QList<fxt::ImportFailure> &failures,
                        qint64 elapsedMs);

private:
    QFutureWatcher<ImportItem> *m_watcher = nullptr;   // mapped 结果序列（保序）
    QList<ImportFailure> m_failures;
    QElapsedTimer m_timer;
    bool m_busy = false;
};

} // namespace fxt

// Q_DECLARE_METATYPE 需求：信号跨线程仅经 watcher result() 传递（同值拷贝），
// 不经 queued signal 载荷，无需注册 metatype。
