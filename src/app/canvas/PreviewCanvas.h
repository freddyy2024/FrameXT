#pragma once

#include <QImage>
#include <QPoint>
#include <QWidget>

namespace fxt {

// 预览画布（开发计划 3.4 第一批 / 04-UI §3.4）：
// 棋盘格底 + 圆角卡片内渲染图显示；滚轮缩放（光标锚 0.1–8×）、
// 空格/中键平移、适配窗口 / 100%、对比原图（按住临时隐藏水印——
// 由外部切 ProjectController::setWatermarkHidden，此处只发信号）。
// 元素选中/拖动/吸附/就地编辑为 3.4 第二批。
class PreviewCanvas : public QWidget
{
    Q_OBJECT

public:
    explicit PreviewCanvas(QWidget *parent = nullptr);

    void setPreview(const QImage &image);
    void setEmpty(bool empty);
    void fitToWindow();
    void setZoom100();
    // 以视口中心为锚点的缩放（工具条滑杆联动用）
    void setZoom(qreal zoom);
    qreal zoom() const { return m_zoom; }

signals:
    void zoomChanged(qreal zoom);
    void compareOriginalPressed(bool pressed);   // 对比原图按住/松开

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRectF imageRect() const;   // 当前缩放/平移下图像在视口中的矩形
    void setZoomAt(qreal zoom, const QPointF &anchorPoint);

    QImage m_image;
    qreal m_zoom = 1.0;
    QPointF m_panOffset;         // 视口平移（图像坐标原点的视口偏移）
    bool m_panning = false;
    QPoint m_panStart;
    bool m_spacePan = false;
    bool m_empty = true;
};

} // namespace fxt
