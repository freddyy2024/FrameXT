#include "export/JpegEncoder.h"

#include <turbojpeg.h>

namespace fxt {

namespace {

// TurboJPEG 资源 RAII（handle / 输出缓冲，异常与早退路径都不泄漏）
struct Compressor
{
    tjhandle handle = nullptr;

    Compressor()
    {
        if (void *h = tj3Init(TJINIT_COMPRESS))
            handle = static_cast<tjhandle>(h);
    }
    ~Compressor()
    {
        if (handle)
            tj3Destroy(handle);
    }
};

// QImage 内存布局 → TurboJPEG 像素格式。
// QImage 内部永远 4 字节对齐（bytesPerLine）且 ARGB32/RGB32 在小端机上是
// B,G,R,A 字节序 → 对应 TJPF_BGRA；RGB888 是 R,G,B → TJPF_RGB。
// 其余格式统一转 ARGB32（convertToFormat 对已是目标格式者是零拷贝）。
bool prepareSource(const QImage &image, QImage &buffer, int &pixelFormat)
{
    switch (image.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_RGB32:
        buffer = image;
        pixelFormat = TJPF_BGRA;
        return true;
    case QImage::Format_RGB888:
        buffer = image;
        pixelFormat = TJPF_RGB;
        return true;
    default:
        buffer = image.convertToFormat(QImage::Format_ARGB32);
        pixelFormat = TJPF_BGRA;
        return buffer.format() == QImage::Format_ARGB32;
    }
}

} // namespace

QByteArray JpegEncoder::encode(const QImage &image, int quality,
                               JpegSubsampling subsampling, QString *error)
{
    if (error)
        error->clear();
    if (image.isNull()) {
        if (error)
            *error = QStringLiteral("空图像，无法编码 JPEG");
        return {};
    }
    if (image.width() > 32767 || image.height() > 32767) {
        // 与渲染层 100MP/单边上限一致：超限显式报错，不静默废图
        if (error)
            *error = QStringLiteral("图像尺寸超出 JPEG 编码上限（单边 32767）");
        return {};
    }

    QImage source;
    int pixelFormat = 0;
    if (!prepareSource(image, source, pixelFormat)) {
        if (error)
            *error = QStringLiteral("图像格式转换失败");
        return {};
    }

    Compressor comp;
    if (!comp.handle) {
        if (error)
            *error = QStringLiteral("TurboJPEG 初始化失败");
        return {};
    }

    tj3Set(comp.handle, TJPARAM_QUALITY, qBound(1, quality, 100));
    tj3Set(comp.handle, TJPARAM_SUBSAMP,
           subsampling == JpegSubsampling::s444 ? TJSAMP_444
           : subsampling == JpegSubsampling::s422 ? TJSAMP_422
                                                  : TJSAMP_420);

    unsigned char *jpegBuf = nullptr;
    std::size_t jpegSize = 0;   // tj3 API 用 size_t（tjCompress2 老 API 才是 unsigned long）
    const int pitch = source.bytesPerLine();
    if (tj3Compress8(comp.handle, source.constBits(), source.width(), pitch,
                     source.height(), pixelFormat, &jpegBuf, &jpegSize) < 0) {
        if (error)
            *error = QString::fromLatin1(tj3GetErrorStr(comp.handle));
        return {};
    }

    QByteArray out(reinterpret_cast<const char *>(jpegBuf), int(jpegSize));
    tj3Free(jpegBuf);
    return out;
}

} // namespace fxt
