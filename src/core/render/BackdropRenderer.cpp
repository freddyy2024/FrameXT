#include "render/BackdropRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <QtGlobal>

namespace fxt::BackdropRenderer {

namespace {

using namespace fxt::BuiltinTemplate;

// —— box blur（ARGB32_Premultiplied，滑动窗口 O(1)/像素）——
// 两轮 box blur 近似高斯（02-技术设计 §4.4：boxBlur×2，σ=W/20 换算）
void boxBlurPassHorizontal(QImage &img, const int radius)
{
    const int w = img.width();
    const int h = img.height();
    if (w < 2 || radius <= 0)
        return;
    const int window = radius * 2 + 1;
    QVector<quint32> out(w * h);

    for (int y = 0; y < h; ++y) {
        const quint32 *src = reinterpret_cast<const quint32 *>(img.constScanLine(y));
        quint32 *dst = out.data() + quint32(y) * w;
        int aSum = 0, rSum = 0, gSum = 0, bSum = 0;
        for (int i = 0; i < qMin(window, w); ++i) {
            const quint32 p = src[i];
            aSum += qAlpha(p);
            rSum += qRed(p);
            gSum += qGreen(p);
            bSum += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            dst[x] = qRgba(rSum / window, gSum / window, bSum / window, aSum / window);
            const int addIdx = qMin(x + radius + 1, w - 1);
            const int subIdx = qMax(x - radius, 0);
            const quint32 add = src[addIdx];
            const quint32 sub = src[subIdx];
            aSum += qAlpha(add) - qAlpha(sub);
            rSum += qRed(add) - qRed(sub);
            gSum += qGreen(add) - qGreen(sub);
            bSum += qBlue(add) - qBlue(sub);
        }
    }
    for (int y = 0; y < h; ++y)
        memcpy(img.scanLine(y), out.data() + quint32(y) * w, quint32(w) * 4);
}

void boxBlurPassVertical(QImage &img, const int radius)
{
    const int w = img.width();
    const int h = img.height();
    if (h < 2 || radius <= 0)
        return;
    const int window = radius * 2 + 1;
    QVector<quint32> out(w * h);

    for (int x = 0; x < w; ++x) {
        int aSum = 0, rSum = 0, gSum = 0, bSum = 0;
        for (int i = 0; i < qMin(window, h); ++i) {
            const quint32 p = reinterpret_cast<const quint32 *>(img.constScanLine(i))[x];
            aSum += qAlpha(p);
            rSum += qRed(p);
            gSum += qGreen(p);
            bSum += qBlue(p);
        }
        for (int y = 0; y < h; ++y) {
            out[quint32(y) * w + x] =
                qRgba(rSum / window, gSum / window, bSum / window, aSum / window);
            const int addY = qMin(y + radius + 1, h - 1);
            const int subY = qMax(y - radius, 0);
            const quint32 add = reinterpret_cast<const quint32 *>(img.constScanLine(addY))[x];
            const quint32 sub = reinterpret_cast<const quint32 *>(img.constScanLine(subY))[x];
            aSum += qAlpha(add) - qAlpha(sub);
            rSum += qRed(add) - qRed(sub);
            gSum += qGreen(add) - qGreen(sub);
            bSum += qBlue(add) - qBlue(sub);
        }
    }
    for (int y = 0; y < h; ++y)
        memcpy(img.scanLine(y), out.data() + quint32(y) * w, quint32(w) * 4);
}

void boxBlur(QImage &img, const int radius, const int passes = 2)
{
    if (img.isNull() || radius <= 0)
        return;
    for (int i = 0; i < passes; ++i) {
        boxBlurPassHorizontal(img, radius);
        boxBlurPassVertical(img, radius);
    }
}

// blur 模板：原图 downscale(≤512) → boxBlur×2 → 放大铺满画布（§4.4）
QImage blurBackdrop(const CanvasPlan &plan, const QImage &photo)
{
    const qreal shrink =
        qMin(1.0, 512.0 / qreal(qMax(plan.canvasSize.width(), plan.canvasSize.height())));
    const QSize smallSize(qMax(2, qRound(plan.canvasSize.width() * shrink)),
                          qMax(2, qRound(plan.canvasSize.height() * shrink)));

    // 原图拉伸到小画布尺寸（比例差异经模糊后不可见）
    QImage small = photo.scaled(smallSize, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // σ=W/20 换算到小图：半径 = (W/20) × shrink，两轮 box ≈ 高斯
    const qreal sigmaSmall = (plan.canvasSize.width() / 20.0) * shrink;
    boxBlur(small, qBound(1, qRound(sigmaSmall), 32));

    QImage canvas(plan.canvasSize, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(0, 0, plan.canvasSize.width(), plan.canvasSize.height()),
                      small);
    painter.end();
    return canvas;
}

} // namespace

QImage backdrop(const CanvasPlan &plan, const QString &templateId, const QImage &photo)
{
    if (templateId == QLatin1String(kBlur) && !photo.isNull())
        return blurBackdrop(plan, photo);

    QImage canvas(plan.canvasSize, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);   // bottom / around：白底
    return canvas;
}

void composite(QImage &canvas, const CanvasPlan &plan, const QImage &photo)
{
    if (photo.isNull())
        return;
    QPainter painter(&canvas);
    if (plan.roundedPhoto && plan.cornerRadius > 0) {
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clip;
        clip.addRoundedRect(QRectF(plan.photoRect), plan.cornerRadius,
                            plan.cornerRadius);
        painter.setClipPath(clip);
    }
    painter.drawImage(plan.photoRect, photo);
    painter.end();
}

} // namespace fxt::BackdropRenderer
