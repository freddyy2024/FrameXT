#include "canvas/PreviewCanvas.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "../Theme.h"

namespace fxt {

namespace {
constexpr qreal kMinZoom = 0.1;
constexpr qreal kMaxZoom = 8.0;
constexpr int kCheckerSize = 16;
} // namespace

PreviewCanvas::PreviewCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("previewCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
}

void PreviewCanvas::setPreview(const QImage &image)
{
    m_image = image;
    m_empty = image.isNull();
    if (m_empty)
        return;
    fitToWindow();
    update();
}

void PreviewCanvas::setEmpty(bool empty)
{
    m_empty = empty;
    m_image = QImage();
    update();
}

QRectF PreviewCanvas::imageRect() const
{
    if (m_image.isNull())
        return {};
    const QSizeF sz(m_image.size());
    return QRectF(m_panOffset, sz * m_zoom);
}

void PreviewCanvas::fitToWindow()
{
    if (m_image.isNull())
        return;
    // 内衬 8px（§3.4 卡片内衬）
    const qreal avail = qMin(qreal(width()) - 16, qreal(height()) - 16);
    const qreal maxEdge = qMax(m_image.width(), m_image.height());
    if (maxEdge <= 0 || avail <= 0)
        return;
    m_zoom = qBound(kMinZoom, avail / maxEdge, kMaxZoom);
    m_panOffset = QPointF((width() - m_image.width() * m_zoom) / 2.0,
                          (height() - m_image.height() * m_zoom) / 2.0);
    emit zoomChanged(m_zoom);
    update();
}

void PreviewCanvas::setZoom100()
{
    setZoomAt(1.0, rect().center());
    update();
}

void PreviewCanvas::setZoom(qreal zoom)
{
    setZoomAt(zoom, rect().center());
}

void PreviewCanvas::setZoomAt(qreal zoom, const QPointF &anchorPoint)
{
    const qreal newZoom = qBound(kMinZoom, zoom, kMaxZoom);
    if (qFuzzyCompare(newZoom, m_zoom))
        return;
    // 锚点缩放：锚点下的图像坐标在缩放前后保持重合
    const QPointF imagePos = (anchorPoint - m_panOffset) / m_zoom;
    m_zoom = newZoom;
    m_panOffset = anchorPoint - imagePos * m_zoom;
    emit zoomChanged(m_zoom);
    update();
}

void PreviewCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 棋盘格底（§1.1：16px #FFFFFF/#EDEFF2）
    p.fillRect(rect(), QColor(0xFF, 0xFF, 0xFF));
    if (!m_image.isNull()) {
        QColor checker(0xED, 0xEF, 0xF2);
        for (int y = 0; y < height(); y += kCheckerSize) {
            const bool oddRow = (y / kCheckerSize) % 2 != 0;
            for (int x = oddRow ? 0 : kCheckerSize; x < width();
                 x += kCheckerSize * 2) {
                p.fillRect(x, y, kCheckerSize, kCheckerSize, checker);
            }
        }
    }

    if (m_empty || m_image.isNull()) {
        // 无图空态（§3.4：就地空态，不跳转独立页）
        p.setPen(QColor(Theme::kTextMuted));
        QFont f = p.font();
        f.setPixelSize(14);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("拖入照片开始使用\n或点击选择 — 照片不会离开这台电脑"));
        return;
    }

    p.drawImage(imageRect(), m_image);
}

void PreviewCanvas::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull())
        return;
    const qreal step = event->angleDelta().y() > 0 ? 1.2 : 1.0 / 1.2;
    setZoomAt(m_zoom * step, event->position());
}

void PreviewCanvas::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePan)) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    if (event->button() == Qt::LeftButton) {
        emit compareOriginalPressed(true);   // 对比原图（按住查看，§9-D）
    }
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (!m_panning)
        return;
    m_panOffset += QPointF(event->pos() - m_panStart);
    m_panStart = event->pos();
    update();
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_panning)
        emit compareOriginalPressed(false);
    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePan)) {
        m_panning = false;
        setCursor(m_spacePan ? Qt::OpenHandCursor : Qt::ArrowCursor);
    }
}

void PreviewCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan = true;
        setCursor(Qt::OpenHandCursor);
    }
    QWidget::keyPressEvent(event);
}

void PreviewCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan = false;
        if (!m_panning)
            setCursor(Qt::ArrowCursor);
    }
    QWidget::keyReleaseEvent(event);
}

void PreviewCanvas::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_image.isNull())
        fitToWindow();   // 窗口变化保持适配（简单可靠；用户手动缩放后
                         // 保持策略由 3.4 第二批细化）
}

} // namespace fxt
