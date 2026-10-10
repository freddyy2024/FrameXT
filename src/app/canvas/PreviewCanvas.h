#pragma once

#include <QHash>
#include <QImage>
#include <QLineF>
#include <QPoint>
#include <QRectF>
#include <QUuid>
#include <QWidget>

#include "render/LayoutEngine.h"

class QLineEdit;

namespace fxt {

// 预览画布（开发计划 3.4 / 04-UI §3.4）：
// 棋盘格底 + 圆角卡片内渲染图显示；滚轮缩放（光标锚 0.1–8×）、
// 空格/中键平移、适配窗口 / 100%、对比原图（按住临时隐藏水印——
// 由外部切 ProjectController::setWatermarkHidden，此处只发信号）。
// 编辑态：元素单击选中（#6C5CE7 手柄框）→ 拖动移动 + 参考线吸附
// （中心/三分线/画布边），松手一次提交（一次撤销快照）；
// 双击文本元素就地编辑；Del/右键菜单走信号由外部处置。
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

    // 元素布局（图像像素坐标；列表序 = z 序，前 = 上层）。每次渲染后同步。
    void setElementLayouts(const QList<QPair<QUuid, ElementLayout>> &layouts);
    // 外部选中联动（右栏行点击）；空 id = 取消选中。不发 elementSelected。
    void setSelectedElement(const QUuid &id);
    // 纯看图态提示（画布顶部浮"已隐藏水印 · 显示"胶囊）
    void setWatermarkHiddenHint(bool hidden);

signals:
    void zoomChanged(qreal zoom);
    void compareOriginalPressed(bool pressed);   // 对比原图按住/松开
    void elementSelected(const QUuid &id);       // 空 = 取消选中
    // 拖动提交：offset 增量（相对单位，画布尺寸归一）
    void elementMoved(const QUuid &id, const QPointF &offsetDelta);
    void elementTextEdited(const QUuid &id, const QString &text);
    void elementDeleteRequested(const QUuid &id);
    void elementContextMenuRequested(const QUuid &id, const QPoint &globalPos);
    void showWatermarkRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QRectF imageRect() const;   // 当前缩放/平移下图像在视口中的矩形
    void setZoomAt(qreal zoom, const QPointF &anchorPoint);
    // 视口坐标 → 图像像素坐标
    QPointF toImagePos(const QPointF &viewPos) const;
    QUuid hitTest(const QPointF &imagePos) const;
    QRectF handleRect(const QRectF &viewRect) const;   // 选中手柄框（视口坐标）
    void beginTextEdit(const QUuid &id);
    void commitTextEdit(bool submit);
    QRectF capsuleRect() const;   // "已隐藏水印"胶囊（视口坐标）

    QImage m_image;
    qreal m_zoom = 1.0;
    QPointF m_panOffset;         // 视口平移（图像坐标原点的视口偏移）
    bool m_panning = false;
    QPoint m_panStart;
    bool m_spacePan = false;
    bool m_empty = true;

    // —— 元素交互 ——
    QList<QPair<QUuid, ElementLayout>> m_layouts;   // 图像像素坐标，前 = 上层
    QUuid m_selectedId;
    QUuid m_hoverId;
    bool m_dragging = false;
    QPointF m_dragStartImg;        // 拖动起点（图像坐标）
    QPointF m_dragOriginTopLeft;   // 被拖元素初始矩形（图像坐标）
    QRectF m_dragRect;             // 拖动中的元素矩形（吸附修正后）
    QLineF m_guideV;               // 吸附参考线（图像坐标；null = 不显示）
    QLineF m_guideH;
    bool m_comparePressed = false; // 空白区按住 = 对比原图

    // —— 就地编辑 ——
    QUuid m_editingId;
    QLineEdit *m_editor = nullptr;

    // —— 纯看图胶囊 ——
    bool m_watermarkHidden = false;
};

} // namespace fxt
