// M4 性能基准（4.1 准入：60 张 45MP JPEG 全尺寸导出 ≤60s，方案 §6.1）。
// 独立 target（不进常规 ctest 轮——生成 45MP 样本的一次性成本约 1 分钟）。
// 样本缓存：FXT_PERF_DIR 指向持久目录时复用已生成的样本；缺省用
// QTemporaryDir 现场生成（测试自包含，CI 可跑）。
#include <QtTest>

#include <QEventLoop>
#include <QTimer>

#include "controllers/ExportController.h"
#include "export/ExportEngine.h"
#include "render/BuiltinTemplates.h"

using namespace fxt;

namespace {

// 程序化生成 45MP 级 JPEG（8192×5472 ≈ 44.8MP，横版全画幅比例）；
// 内容 = 平滑渐变（有损编码的现实上界；不做随机噪点——那会放大体积）
QString generateSample(const QString &path, const QSize &size)
{
    QImage img(size, QImage::Format_ARGB32);
    for (int y = 0; y < size.height(); y += 1) {
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        const int g = y * 255 / size.height();
        for (int x = 0; x < size.width(); ++x) {
            const int r = x * 255 / size.width();
            line[x] = qRgb(r, g, 128);
        }
    }
    img.save(path, "jpeg", 95);
    return path;
}

PhotoDocument makeDoc(const QString &sourcePath, const QSize &px)
{
    PhotoDocument doc;
    doc.id = QUuid::createUuid();
    doc.sourcePath = sourcePath;
    doc.meta.pixelSize = px;
    doc.meta.format = QStringLiteral("jpeg");
    const auto spec = BuiltinTemplates::defaultSpec(QStringLiteral("bottom"));
    doc.templateState.templateId = QStringLiteral("bottom");
    doc.templateState.elements = spec.elements;
    return doc;
}

} // namespace

class PerfExport : public QObject
{
    Q_OBJECT

private slots:
    void export60MegapixelsWithin60s();
};

void PerfExport::export60MegapixelsWithin60s()
{
    // —— 样本准备（缓存复用）——
    const QSize sampleSize(8192, 5472);   // 44.8MP
    const int kCount = 60;
    QTemporaryDir tempDir;
    const QString sampleDir = qEnvironmentVariable("FXT_PERF_DIR");
    const bool cached = !sampleDir.isEmpty() && QDir(sampleDir).exists();

    QList<PhotoDocument> docs;
    QList<FieldValueResolver> resolvers;
    QList<QImage> logos;
    for (int i = 0; i < kCount; ++i) {
        const QString path = cached
            ? QDir(sampleDir).filePath(QStringLiteral("p%1.jpg").arg(i))
            : tempDir.filePath(QStringLiteral("p%1.jpg").arg(i));
        if (!QFileInfo::exists(path))
            generateSample(path, sampleSize);
        docs.append(makeDoc(path, sampleSize));
        resolvers.append([](const QString &) { return QString(); });
        logos.append(QImage());
    }
    qint64 genMs = 0;   // 样本生成不计入导出指标（缓存命中时为 0）

    // —— 全尺寸视觉无损导出（视觉无损 = 最重的预设路径）——
    QTemporaryDir outDir;
    ExportOptions opt;
    opt.outputDir = outDir.path();
    opt.preset = QualityPreset::VisualLossless;   // q100 + 4:4:4

    ExportController ctl;
    QElapsedTimer timer;
    timer.start();
    QEventLoop loop;
    QList<ExportEngine::ItemResult> results;
    bool ok = false;
    QObject::connect(
        &ctl, &ExportController::finished, &loop,
        [&](const QList<ExportEngine::ItemResult> &r, bool, qint64) {
            results = r;
            ok = true;
            loop.quit();
        });
    QTimer::singleShot(300000, &loop, &QEventLoop::quit);   // 5 分钟硬超时
    ctl.start(docs, resolvers, logos, opt);
    loop.exec();
    genMs = 0;
    Q_UNUSED(genMs);

    QVERIFY(ok);
    int exported = 0;
    for (const auto &r : results)
        exported += r.ok ? 1 : 0;
    QCOMPARE(exported, kCount);

    const qint64 elapsed = timer.elapsed();
    qDebug() << "=== 导出" << kCount << "张 45MP：" << elapsed << "ms（准入 ≤60000ms）";
    QVERIFY2(elapsed <= 60000,
             qPrintable(QStringLiteral("60 张 45MP 导出耗时 %1ms，超 60s 准入线")
                            .arg(elapsed)));
}

QTEST_MAIN(PerfExport)
#include "perf_export.moc"
