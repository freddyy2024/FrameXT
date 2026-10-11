#include "render/BackdropRenderer.h"

#include <QPainter>
#include <QPainterPath>
#include <QVector>
#include <QtGlobal>

namespace fxt::BackdropRenderer {

namespace {

using namespace fxt::BuiltinTemplate;

// —— box blur（ARGB32_Premultiplied，滑动窗口 O(1)/像素）——
// 两轮 box blur 近似高斯（02-技术设计 §4.4：boxBlur×2，σ=W/20 换算）。
// 滑窗为精确 clamp 居中窗口（除数=实际窗口项数）：早期实现用固定除数
// window 且初始累加右偏，对渐变图系统性偏亮（极端宽高比下短边几乎全处
// 于窗口 clamp 区，实测 G 通道均值 49→103），此处必须保持窗口对称。
void boxBlurPassHorizontal(QImage &img, const int radius)
{
    const int w = img.width();
    const int h = img.height();
    if (w < 2 || radius <= 0)
        return;
    QVector<quint32> out(w * h);

    for (int y = 0; y < h; ++y) {
        const quint32 *src = reinterpret_cast<const quint32 *>(img.constScanLine(y));
        quint32 *dst = out.data() + quint32(y) * w;
        int lo = 0;
        int hi = qMin(radius, w - 1);
        int count = hi - lo + 1;
        int aSum = 0, rSum = 0, gSum = 0, bSum = 0;
        for (int i = lo; i <= hi; ++i) {
            const quint32 p = src[i];
            aSum += qAlpha(p);
            rSum += qRed(p);
            gSum += qGreen(p);
            bSum += qBlue(p);
        }
        for (int x = 0; x < w; ++x) {
            dst[x] = qRgba(rSum / count, gSum / count, bSum / count, aSum / count);
            // 窗口推进到 [x+1-r, x+1+r]（clamp）：左右缘各至多移动一步
            const int nHi = qMin(x + 1 + radius, w - 1);
            if (nHi != hi) {
                const quint32 add = src[nHi];
                aSum += qAlpha(add);
                rSum += qRed(add);
                gSum += qGreen(add);
                bSum += qBlue(add);
                ++count;
                hi = nHi;
            }
            const int nLo = qMax(x + 1 - radius, 0);
            if (nLo != lo) {
                const quint32 sub = src[lo];
                aSum -= qAlpha(sub);
                rSum -= qRed(sub);
                gSum -= qGreen(sub);
                bSum -= qBlue(sub);
                --count;
                lo = nLo;
            }
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
    QVector<quint32> out(w * h);

    for (int x = 0; x < w; ++x) {
        int lo = 0;
        int hi = qMin(radius, h - 1);
        int count = hi - lo + 1;
        int aSum = 0, rSum = 0, gSum = 0, bSum = 0;
        for (int i = lo; i <= hi; ++i) {
            const quint32 p = reinterpret_cast<const quint32 *>(img.constScanLine(i))[x];
            aSum += qAlpha(p);
            rSum += qRed(p);
            gSum += qGreen(p);
            bSum += qBlue(p);
        }
        for (int y = 0; y < h; ++y) {
            out[quint32(y) * w + x] =
                qRgba(rSum / count, gSum / count, bSum / count, aSum / count);
            const int nHi = qMin(y + 1 + radius, h - 1);
            if (nHi != hi) {
                const quint32 add = reinterpret_cast<const quint32 *>(img.constScanLine(nHi))[x];
                aSum += qAlpha(add);
                rSum += qRed(add);
                gSum += qGreen(add);
                bSum += qBlue(add);
                ++count;
                hi = nHi;
            }
            const int nLo = qMax(y + 1 - radius, 0);
            if (nLo != lo) {
                const quint32 sub = reinterpret_cast<const quint32 *>(img.constScanLine(lo))[x];
                aSum -= qAlpha(sub);
                rSum -= qRed(sub);
                gSum -= qGreen(sub);
                bSum -= qBlue(sub);
                --count;
                lo = nLo;
            }
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
QImage blurBackdrop(const CanvasPlan &plan, const QImage &photo,
                    qreal blurStrength)
{
    const qreal shrink =
        qMin(1.0, 512.0 / qreal(qMax(plan.canvasSize.width(), plan.canvasSize.height())));
    const QSize smallSize(qMax(2, qRound(plan.canvasSize.width() * shrink)),
                          qMax(2, qRound(plan.canvasSize.height() * shrink)));

    // 原图拉伸到小画布尺寸（比例差异经模糊后不可见）
    QImage small = photo.scaled(smallSize, Qt::IgnoreAspectRatio,
                                Qt::SmoothTransformation)
                       .convertToFormat(QImage::Format_ARGB32_Premultiplied);

    // σ=W/20 × blurStrength（M5.1 参数化，默认 1.0 = 原视觉常量）换算到
    // 小图：半径 = σ × shrink，两轮 box ≈ 高斯。
    // 半径上限按小图短边钳制（window=2r+1 不得超过短边-1）：极端宽高比
    // （如 10000×100 长截图）下小图宽可缩到 2px，超限窗口会使滑窗累加
    // 除数失配、背景整体变暗约 1/9
    const qreal sigmaSmall =
        (plan.canvasSize.width() / 20.0) * blurStrength * shrink;
    const int radius = qBound(1, qRound(sigmaSmall), 32);
    const int shortEdge = qMin(smallSize.width(), smallSize.height());
    boxBlur(small, qMin(radius, qMax(1, (shortEdge - 1) / 2)));

    QImage canvas(plan.canvasSize, QImage::Format_ARGB32_Premultiplied);
    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    painter.drawImage(QRect(0, 0, plan.canvasSize.width(), plan.canvasSize.height()),
                      small);
    painter.end();
    return canvas;
}

} // namespace

QImage backdrop(const CanvasPlan &plan, const QString &templateId,
                const QImage &photo, qreal blurStrength)
{
    if (templateId == QLatin1String(kBlur) && !photo.isNull())
        return blurBackdrop(plan, photo, blurStrength);

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
