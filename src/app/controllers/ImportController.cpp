#include "controllers/ImportController.h"

#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QtConcurrent/QtConcurrentMap>

#include "metadata/BrandRegistry.h"
#include "metadata/ExifReader.h"
#include "metadata/FieldRegistry.h"
#include "render/BuiltinTemplates.h"
#include "render/PhotoDecoder.h"

namespace fxt {

namespace {
constexpr int kThumbEdge = 240;   // 列表缩略图边长（§3.7 项宽 120@2x）

// 单张导入（线程池内执行；exiv2/解码器每线程独立实例，无共享态）
ImportItem importOne(const QString &path)
{
    ImportItem item;
    item.path = path;
    item.doc.id = QUuid::createUuid();
    item.doc.sourcePath = path;

    // 1) 解码 + 缩略图——缩放解码（低配机内存优化）：
    //    先只读头拿尺寸算缩放比，PhotoDecoder 走 QImageReader setScaledSize，
    //    JPEG 经 libjpeg DCT 档位（1/2、1/4、1/8）降采样解码，45MP 单张
    //    解码缓冲从 ~180MB 降到 ~11MB，多张并行导入的瞬时峰值同步下降。
    //    非 JPEG 格式插件不支持时回落全尺寸解码 + CPU 缩放（不劣于旧行为）。
    const QSize stored = QImageReader(path).size();
    qreal thumbScale = 1.0;
    if (stored.isValid()) {
        const int maxEdge = qMax(stored.width(), stored.height());
        if (maxEdge > kThumbEdge)
            thumbScale = qreal(kThumbEdge) / maxEdge;
    }
    const auto decoded = PhotoDecoder::decode(path, thumbScale);
    if (!decoded.ok) {
        item.error = decoded.error.isEmpty()
                         ? QStringLiteral("无法解码图像")
                         : decoded.error;
        return item;
    }
    item.doc.meta.pixelSize = decoded.originalSize;
    item.doc.meta.format = decoded.format;
    // 缩略图以长边等比缩放（KeepAspectRatio 不裁剪），列表项内居中完整显示；
    // scaled 对已达标尺寸走快速路径（零成本）
    item.thumbnail = decoded.image.scaled(kThumbEdge, kThumbEdge,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);

    // 2) Exif 解析（M2 引擎）→ 展示值集 + 品牌规范化
    ExifReader reader(path);
    if (reader.read()) {
        const RawExif raw = reader.raw();
        item.doc.meta.exif.fields = FieldRegistry::resolveDisplay(raw);
        item.doc.meta.exif.make = raw.value(QStringLiteral("Exif.Image.Make"));
        item.doc.meta.exif.model = raw.value(QStringLiteral("Exif.Image.Model"));
        // 品牌规范化（匹配值供 Logo 解析与展示；别名原文保留在 fields）
        const BrandDef brand = BrandRegistry::match(
            raw.value(QStringLiteral("Exif.Image.Make")),
            item.doc.meta.exif.model);
        if (brand.value != QLatin1String("Others"))
            item.doc.meta.exif.make = brand.value;
        item.doc.meta.hasGps =
            raw.contains(QStringLiteral("Exif.GPSInfo.GPSLatitude"));
        // ICC 全字节（跨格式导出重建路径的保真材料，评审 A3）
        item.doc.meta.iccProfile = reader.iccProfile();
    }
    // Exif 缺失不阻塞导入（无水印照片合法）；默认模板状态每张独立持有（§4.1）
    const auto spec = BuiltinTemplates::defaultSpec(QStringLiteral("bottom"));
    item.doc.templateState.templateId = QStringLiteral("bottom");
    item.doc.templateState.elements = spec.elements;

    return item;
}
} // namespace

ImportController::ImportController(QObject *parent)
    : QObject(parent)
{
    m_watcher = new QFutureWatcher<ImportItem>(this);
    connect(m_watcher, &QFutureWatcherBase::finished, this, [this] {
        // mapped 结果与输入同序，逐个回放
        int ok = 0;
        const int count = m_watcher->future().resultCount();
        for (int i = 0; i < count; ++i) {
            const ImportItem &item = m_watcher->resultAt(i);
            if (!item.error.isEmpty()) {
                m_failures.append({ item.path, item.error });
                continue;
            }
            ++ok;
            emit documentReady(item.doc, item.thumbnail);
        }
        m_busy = false;
        emit importFinished(ok, m_failures, m_timer.elapsed());
    });
}

QStringList ImportController::supportedSuffixes()
{
    return { QStringLiteral("jpg"), QStringLiteral("jpeg"),
             QStringLiteral("png"), QStringLiteral("webp"),
             QStringLiteral("tif"), QStringLiteral("tiff") };
}

void ImportController::importFiles(const QStringList &paths)
{
    if (m_busy || paths.isEmpty())
        return;

    // E8 格式校验：后缀不在支持列表的直接进失败清单（E10 汇总展示）
    QStringList accepted;
    m_failures.clear();
    const auto suffixes = supportedSuffixes();
    for (const QString &p : paths) {
        const QString suffix = QFileInfo(p).suffix().toLower();
        if (suffixes.contains(suffix))
            accepted.append(p);
        else
            m_failures.append({ p, QStringLiteral("不支持的格式") });
    }
    if (accepted.isEmpty()) {
        emit importFinished(0, m_failures, 0);
        return;
    }

    m_busy = true;
    m_timer.start();
    m_watcher->setFuture(QtConcurrent::mapped(accepted, importOne));
}

} // namespace fxt
