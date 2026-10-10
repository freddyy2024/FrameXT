#include "export/ExportEngine.h"

#include <QBuffer>
#include <QDate>
#include <QImageReader>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include "AppInfo.h"
#include "export/JpegEncoder.h"
#include "platform/PngChunkIO.h"

namespace fxt {

using namespace Platform;


namespace {

// 渲染请求：导出 = 全尺寸 scale=1（§4.2：导出不依赖预览历史，
// 元素树由模板 + meta 即时解算）
RenderRequest makeRenderRequest(const PhotoDocument &doc)
{
    RenderRequest req;
    req.photoId = doc.id;
    req.filePath = doc.sourcePath;
    req.templateState = doc.templateState;
    req.overrides = doc.overrides;
    req.scale = 1.0;
    req.purpose = RenderPurpose::Export;
    return req;
}

// 源文件的 raw Exif TIFF 块（跨格式导出的重建材料，§4.8）：
//   JPEG → APP1 提取；PNG → eXIf chunk；其余（WebP/TIFF）v1 不提取，
//   导出按「无 Exif 源」处理（ICC 仍经 PhotoMeta.iccProfile 保真），
//   挂账给评审定夺是否补 WebP/TIFF 提取器。
QByteArray rawExifTiff(const QByteArray &sourceBytes, const QString &format)
{
    if (format == QLatin1String("jpeg"))
        return MetadataMuxer::exifTiffFromJpeg(sourceBytes);
    if (format == QLatin1String("png"))
        return PngChunkIO::chunkData(sourceBytes, "eXIf");
    return {};
}

// 文件名里的非法字符（Windows 保留集）替换为下划线
QString sanitizeFileName(QString name)
{
    const QString illegal = QStringLiteral("\\/:*?\"<>|");
    for (const QChar c : illegal) {
        if (name.contains(c))
            name.replace(c, QLatin1Char('_'));
    }
    while (name.endsWith(QLatin1Char('.')))
        name.chop(1);
    return name;
}

// 拍摄日期 yyyyMMdd：展示值格式不一（yyyy-MM-dd / yyyy:MM:dd 等），
// 逐候选格式解析；无值/解析失败回落今天（与原版语义一致）
QString captureDate(const PhotoDocument &doc)
{
    const QString shown = doc.meta.exif.fields.value(QStringLiteral("dateTime"));
    const QDate today = QDate::currentDate();
    for (const QString &fmt :
         { QStringLiteral("yyyy-MM-dd"), QStringLiteral("yyyy:MM:dd"),
           QStringLiteral("yyyy/MM/dd"), QStringLiteral("yyyyMMdd") }) {
        const QDate d = QDate::fromString(shown.left(fmt.size()), fmt);
        if (d.isValid())
            return d.toString(QStringLiteral("yyyyMMdd"));
    }
    return today.toString(QStringLiteral("yyyyMMdd"));
}

} // namespace

QString ExportEngine::buildFileName(const PhotoDocument &doc,
                                    const ExportOptions &options, int index)
{
    const QString base = QFileInfo(doc.sourcePath).completeBaseName();
    QString name;
    switch (options.naming) {
    case NamingMode::KeepOriginal:
        name = base;
        break;
    case NamingMode::OriginalPlusIndex: {
        const int n = options.indexStart + index;
        name = base + QLatin1Char('-')
               + QString::number(n).rightJustified(options.indexDigits,
                                                   QLatin1Char('0'));
        break;
    }
    case NamingMode::DatePlusOriginal:
        name = captureDate(doc) + QLatin1Char('_') + base;
        break;
    case NamingMode::CustomPattern: {
        name = options.customPattern;
        if (name.trimmed().isEmpty())
            name = QStringLiteral("{name}");   // 空 pattern → ".jpg" 怪名（A7）
        const int n = options.indexStart + index;
        name.replace(QStringLiteral("{name}"), base);
        name.replace(QStringLiteral("{index}"),
                     QString::number(n).rightJustified(options.indexDigits,
                                                       QLatin1Char('0')));
        name.replace(QStringLiteral("{date}"), captureDate(doc));
        name.replace(QStringLiteral("{model}"),
                     doc.meta.exif.model.isEmpty()
                         ? QStringLiteral("Camera")
                         : doc.meta.exif.model);
        break;
    }
    }
    return sanitizeFileName(name) + options.outputSuffix();
}

ExportEngine::ItemResult ExportEngine::exportSingle(const ItemRequest &req)
{
    ItemResult result;
    result.sourcePath = req.doc.sourcePath;
    result.outputPath = req.outputPath;

    // —— 读源文件字节（S8 元数据搬运 + 体积统计都需要）——
    QFile source(req.doc.sourcePath);
    QByteArray sourceBytes;
    if (source.open(QIODevice::ReadOnly)) {
        sourceBytes = source.readAll();
        source.close();
    }
    if (sourceBytes.isEmpty()) {
        // 渲染管线能容错部分可解码文件；字节都读不出（零字节/权限）直接判失败
        result.stage = QStringLiteral("read");
        result.error = QStringLiteral("无法读取源文件（不存在/零字节/无权限）");
        return result;
    }
    result.sourceBytes = sourceBytes.size();

    // —— S1–S6 全尺寸渲染 ——
    const RenderResult rendered =
        RenderPipeline::run(makeRenderRequest(req.doc), req.resolver,
                            [&req](const QString &) { return req.logo; });
    if (!rendered.ok || rendered.image.isNull()) {
        result.stage = QStringLiteral("render");
        result.error = rendered.error.isEmpty()
                           ? QStringLiteral("渲染失败（图片解码或布局错误）")
                           : rendered.error;
        return result;
    }

    // —— 尺寸缩放（§3.3：长边限制，平滑插值）——
    QImage canvas = rendered.image;
    if (req.options.sizeMode == SizeMode::LongEdge
        && qMax(canvas.width(), canvas.height()) > req.options.longEdge) {
        canvas = canvas.scaled(req.options.longEdge, req.options.longEdge,
                               Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    // 导出覆写：Orientation=1 / PixelDimension / DPI / Software。
    // outputSize = 实际输出尺寸（缩放后画布）
    Platform::ExportPolicy policy = req.options.meta;
    policy.outputSize = canvas.size();
    if (policy.softwareTag) {
        // Software 带版本号（core 层可引 AppInfo；platform 默认值仅裸名）
        policy.softwareValue =
            QByteArrayLiteral("FrameXT ") + AppInfo::versionText().toLatin1();
    }

    // —— S7 编码 ——
    QByteArray encoded;
    if (req.options.format == OutputFormat::Jpeg) {
        QString encodeError;
        encoded = JpegEncoder::encode(canvas, req.options.quality(),
                                      req.options.subsampling(), &encodeError);
        if (encoded.isEmpty()) {
            result.stage = QStringLiteral("encode");
            result.error = encodeError.isEmpty()
                               ? QStringLiteral("JPEG 编码失败")
                               : encodeError;
            return result;
        }
    } else {
        QBuffer buffer(&encoded);
        buffer.open(QIODevice::WriteOnly);
        if (!canvas.save(&buffer, "png")) {
            result.stage = QStringLiteral("encode");
            result.error = QStringLiteral("PNG 编码失败");
            return result;
        }
        buffer.close();
    }

    // —— S8 元数据复用 ——
    const QByteArray tiff = rawExifTiff(sourceBytes, req.doc.meta.format);
    if (req.options.format == OutputFormat::Jpeg) {
        if (req.doc.meta.format == QLatin1String("jpeg"))
            encoded = MetadataMuxer::composeJpeg(encoded, sourceBytes, policy);
        else
            encoded = MetadataMuxer::composeJpegFromExif(encoded, tiff,
                                                         req.doc.meta.iccProfile,
                                                         policy);
    } else {
        if (req.doc.meta.format == QLatin1String("png"))
            encoded = MetadataMuxer::composePng(encoded, sourceBytes, policy);
        else
            encoded = MetadataMuxer::composePngFromRaw(encoded, tiff,
                                                       req.doc.meta.iccProfile,
                                                       policy);
    }

    // —— 完整性校验（E10：可解码回读；写成功 ≠ 编码产物有效）。
    // canRead 仅验头部签名（毫秒级）——足以拦截空产物/坏魔数；
    // 全图解码 45MP ~300ms/张，60 张批次代价过高，不在此做
    {
        QBuffer verify(&encoded);
        verify.open(QIODevice::ReadOnly);
        QImageReader reader(&verify);
        if (!reader.canRead()) {
            result.stage = QStringLiteral("encode");
            result.error = QStringLiteral("导出文件完整性校验未通过");
            return result;
        }
    }

    // —— 落盘（QSaveFile 原子写；失败重试 1 次，§4.7）——
    for (int attempt = 0; attempt < 2; ++attempt) {
        QSaveFile out(req.outputPath);
        if (out.open(QIODevice::WriteOnly) && out.write(encoded) == encoded.size()
            && out.commit()) {
            result.ok = true;
            result.outputBytes = encoded.size();
            return result;
        }
    }
    result.stage = QStringLiteral("write");
    result.error = QStringLiteral("写入目标文件失败（磁盘满/权限/路径无效）");
    return result;
}

} // namespace fxt
