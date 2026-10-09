#include "render/PhotoDecoder.h"

#include <QImageReader>
#include <QtGlobal>

namespace fxt::PhotoDecoder {

DecodeResult decode(const QString &filePath, qreal scale)
{
    DecodeResult result;

    QImageReader reader(filePath);
    reader.setAutoTransform(true);   // 按 Exif Orientation 自动转向
    result.format = reader.format().toLower();

    const QSize stored = reader.size();
    if (stored.isValid())
        result.originalSize = stored;

    // 预览下采样解码：方向修正后尺寸可能交换，直接用缩放比换算目标尺寸
    if (scale < 1.0 && stored.isValid()) {
        QSize scaled = stored;
        // Rotate90/Rotate270 族转换会交换宽高
        if (reader.transformation() & QImageIOHandler::TransformationRotate90)
            scaled.transpose();
        result.originalSize = scaled;
        reader.setScaledSize(QSize(qMax(1, qRound(scaled.width() * scale)),
                                   qMax(1, qRound(scaled.height() * scale))));
    }

    result.image = reader.read();
    result.ok = !result.image.isNull();
    if (!result.ok)
        result.error = reader.errorString();
    else if (result.originalSize.isEmpty())
        result.originalSize = result.image.size();  // 某些格式读前拿不到尺寸

    return result;
}

} // namespace fxt::PhotoDecoder
