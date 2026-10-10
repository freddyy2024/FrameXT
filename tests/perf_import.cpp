// M4 性能基准（§6.1 指标 1：导入 100 张 45MP JPEG ≤15s，含缩略图+Exif）。
// 样本复用 FXT_PERF_DIR（与 perf_export 共享缓存，不足 100 张时补齐）。
#include <QtTest>

#include <QEventLoop>
#include <QTimer>

#include "controllers/ImportController.h"

using namespace fxt;

namespace {

QString generateSample(const QString &path, const QSize &size)
{
    QImage img(size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const int g = y * 255 / size.height();
        for (int x = 0; x < size.width(); ++x)
            line[x] = qRgb(x * 255 / size.width(), g, 128);
    }
    img.save(path, "jpeg", 95);
    return path;
}

} // namespace

class PerfImport : public QObject
{
    Q_OBJECT

private slots:
    void import100MegapixelsWithin15s();
};

void PerfImport::import100MegapixelsWithin15s()
{
    const QSize sampleSize(8192, 5472);   // 44.8MP
    const int kCount = 100;
    QTemporaryDir tempDir;
    QString sampleDir = qEnvironmentVariable("FXT_PERF_DIR");
    if (sampleDir.isEmpty() || !QDir(sampleDir).exists())
        sampleDir = tempDir.path();
    QDir().mkpath(sampleDir);

    QStringList paths;
    for (int i = 0; i < kCount; ++i) {
        const QString path = QDir(sampleDir)
                                 .filePath(QStringLiteral("p%1.jpg").arg(i));
        if (!QFileInfo::exists(path))
            generateSample(path, sampleSize);
        paths.append(path);
    }

    ImportController ctl;
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    bool ok = false;
    QObject::connect(&ctl, &ImportController::importFinished, &loop,
                     [&](int, const QList<ImportFailure> &, qint64) {
                         ok = true;
                         loop.quit();
                     });
    QTimer::singleShot(120000, &loop, &QEventLoop::quit);
    ctl.importFiles(paths);
    loop.exec();

    QVERIFY(ok);
    const qint64 elapsed = timer.elapsed();
    qDebug() << "=== 导入" << kCount << "张 45MP：" << elapsed
             << "ms（准入 ≤15000ms）";
    QVERIFY2(elapsed <= 15000,
             qPrintable(QStringLiteral("100 张 45MP 导入耗时 %1ms，超 15s 准入线")
                            .arg(elapsed)));
}

QTEST_MAIN(PerfImport)
#include "perf_import.moc"
