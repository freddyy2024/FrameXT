#include "canvas/PreviewCanvas.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QWheelEvent>

#include "../Theme.h"

namespace fxt {

namespace {
constexpr qreal kMinZoom = 0.1;
constexpr qreal kMaxZoom = 8.0;
constexpr qreal kSnapThreshold = 8.0;    // 吸附命中距离（视口 px）
constexpr qreal kDragDeadZone = 3.0;     // 位移小于此值视为点击（不提交拖动）
constexpr int kCapsuleWidth = 168;       // "已隐藏水印"胶囊
constexpr int kCapsuleHeight = 28;
constexpr int kPanEdge = 24;             // 平移边界钳制：任一边至少留 24px 在视口内

// Qt 6 无 QColor(const QColor&, alpha) 构造，自绘交互层的半透明强调色在此生成
QColor accentWithAlpha(int alpha)
{
    QColor c = Theme::kAccent;
    c.setAlpha(alpha);
    return c;
}
} // namespace

PreviewCanvas::PreviewCanvas(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("previewCanvas"));
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setCursor(Qt::OpenHandCursor);
    setContextMenuPolicy(Qt::CustomContextMenu);   // 右键元素菜单（无则菜单不可达）
    // 右键：命中元素则选中并发菜单请求（菜单本体在 MainWindow 装配）
    connect(this, &QWidget::customContextMenuRequested, this,
            [this](const QPoint &pos) {
                const QUuid hit = hitTest(toImagePos(pos));
                if (!hit.isNull()) {
                    m_selectedId = hit;
                    emit elementSelected(hit);
                    update();
                }
                emit elementContextMenuRequested(hit,
                                                 mapToGlobal(pos));
            });
}

void PreviewCanvas::setPreview(const QImage &image)
{
    const bool firstImage = m_image.isNull() && !image.isNull();
    m_image = image;
    m_empty = image.isNull();
    if (m_empty)
        return;
    // 首图适配；后续渲染（防抖/编辑）保持用户当前视图不跳变
    if (firstImage)
        fitToWindow();
    update();
}

void PreviewCanvas::setEmpty(bool empty)
{
    m_empty = empty;
    m_image = QImage();
    m_selectedId = QUuid();
    m_layouts.clear();
    update();
}

QRectF PreviewCanvas::imageRect() const
{
    if (m_image.isNull())
        return {};
    const QSizeF sz(m_image.size());
    return QRectF(m_panOffset, sz * m_zoom);
}

void PreviewCanvas::centerImage()
{
    if (m_image.isNull())
        return;
    m_panOffset = QPointF((width() - m_image.width() * m_zoom) / 2.0,
                          (height() - m_image.height() * m_zoom) / 2.0);
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
    centerImage();
    emit zoomChanged(m_zoom);
    update();
}

void PreviewCanvas::setZoom100()
{
    setZoomAt(1.0);
}

void PreviewCanvas::setZoom(qreal zoom)
{
    setZoomAt(zoom);
}

// 中心锚缩放 + 缩放即回中：图像中心恒对齐视口中心（用户指令）——
// 拖动偏移不跨缩放保留，滚轮一动就回居中，图片不会跑出视口
void PreviewCanvas::setZoomAt(qreal zoom)
{
    const qreal newZoom = qBound(kMinZoom, zoom, kMaxZoom);
    if (qFuzzyCompare(newZoom, m_zoom))
        return;
    m_zoom = newZoom;
    centerImage();
    emit zoomChanged(m_zoom);
    update();
}

QPointF PreviewCanvas::toImagePos(const QPointF &viewPos) const
{
    return (viewPos - imageRect().topLeft()) / m_zoom;
}

QUuid PreviewCanvas::hitTest(const QPointF &imagePos) const
{
    // 前 = 上层，从上往下命中
    for (const auto &entry : m_layouts) {
        if (entry.second.pxRect.contains(imagePos))
            return entry.first;
    }
    return {};
}

void PreviewCanvas::setElementLayouts(
    const QList<QPair<QUuid, ElementLayout>> &layouts)
{
    m_layouts = layouts;
    // 选中元素不可见（隐藏/删除）后取消选中
    bool found = false;
    for (const auto &entry : m_layouts) {
        if (entry.first == m_selectedId) {
            found = true;
            break;
        }
    }
    if (!found)
        m_selectedId = QUuid();
    update();
}

void PreviewCanvas::setSelectedElement(const QUuid &id)
{
    m_selectedId = id;
    update();
}

void PreviewCanvas::setWatermarkHiddenHint(bool hidden)
{
    m_watermarkHidden = hidden;
    update();
}

QRectF PreviewCanvas::capsuleRect() const
{
    return QRectF((width() - kCapsuleWidth) / 2.0, 12, kCapsuleWidth,
                  kCapsuleHeight);
}

QRectF PreviewCanvas::handleRect(const QRectF &viewRect) const
{
    return viewRect.adjusted(-4, -4, 4, 4);
}

// —— 绘制 ——

void PreviewCanvas::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 卡片边框（用户指令：与基础属性卡同款边框）。QSS #previewCanvas 的
    // border 对自绘 paintEvent 不生效，按同规格（kCardBorder/kRadius）自画；
    // 空态/有图两个出口都要画，lambda 复用
    auto drawCardFrame = [this, &p] {
        QPen pen(Theme::kCardBorder);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(QRectF(0.5, 0.5, qreal(width() - 1),
                                 qreal(height() - 1)),
                          Theme::kRadius, Theme::kRadius);
    };

    // 画布底：浅灰实底（用户指令：原棋盘格透明指示底改浅灰，色取原棋盘格灰）
    p.fillRect(rect(), QColor(0xED, 0xEF, 0xF2));

    if (m_empty || m_image.isNull()) {
        // 无图空态（§3.4：就地空态，不跳转独立页）
        p.setPen(QColor(Theme::kTextMuted));
        QFont f = p.font();
        f.setPixelSize(14);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter,
                   tr("拖入照片开始使用\n或点击选择 — 照片不会离开这台电脑\n支持 JPEG·PNG·WebP·TIFF"));
        drawCardFrame();
        return;
    }

    p.drawImage(imageRect(), m_image);

    // —— 元素交互层（视口坐标绘制）——
    const QRectF imgRect = imageRect();
    QPen hoverPen(accentWithAlpha(90));
    hoverPen.setWidthF(1.0);
    for (const auto &entry : m_layouts) {
        const QRectF viewRect =
            QRectF(imgRect.topLeft() + entry.second.pxRect.topLeft() * m_zoom,
                   entry.second.pxRect.size() * m_zoom);
        if (entry.first == m_selectedId)
            continue;
        if (entry.first == m_hoverId && !m_dragging) {
            p.setPen(hoverPen);
            p.setBrush(Qt::NoBrush);
            p.drawRect(viewRect);
        }
    }

    if (!m_selectedId.isNull()) {
        for (const auto &entry : m_layouts) {
            if (entry.first != m_selectedId)
                continue;
            const QRectF viewRect =
                QRectF(imgRect.topLeft() + entry.second.pxRect.topLeft() * m_zoom,
                       entry.second.pxRect.size() * m_zoom);
            const QRectF handle = handleRect(viewRect);
            QPen pen(Theme::kAccent);
            pen.setWidthF(1.5);
            p.setPen(pen);
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(handle, 4, 4);
            // 四角手柄（6px 实心圆点）
            p.setBrush(Theme::kAccent);
            p.setPen(Qt::NoPen);
            for (const QPointF &corner :
                 {handle.topLeft(), handle.topRight(), handle.bottomLeft(),
                  handle.bottomRight()}) {
                p.drawEllipse(corner, 3.0, 3.0);
            }
            break;
        }
    }

    // 拖动中：吸附修正后的目标矩形轮廓
    if (m_dragging && !m_dragRect.isNull()) {
        QPen pen(accentWithAlpha(160));
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        p.drawRect(QRectF(imgRect.topLeft() + m_dragRect.topLeft() * m_zoom,
                          m_dragRect.size() * m_zoom));
    }

    // 吸附参考线（1px 虚线）
    if (!m_guideV.isNull() || !m_guideH.isNull()) {
        QPen pen(Theme::kAccent);
        pen.setStyle(Qt::DashLine);
        pen.setWidthF(1.0);
        p.setPen(pen);
        p.setBrush(Qt::NoBrush);
        auto drawGuide = [&p, &imgRect, this](const QLineF &guide) {
            if (guide.isNull())
                return;
            p.drawLine(QLineF(imgRect.topLeft() + guide.p1() * m_zoom,
                              imgRect.topLeft() + guide.p2() * m_zoom));
        };
        drawGuide(m_guideV);
        drawGuide(m_guideH);
    }

    // 纯看图胶囊（§3.4：已隐藏水印 · 显示）
    if (m_watermarkHidden && !m_dragging) {
        const QRectF cap = capsuleRect();
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(31, 35, 41, 190));
        p.drawRoundedRect(cap, cap.height() / 2.0, cap.height() / 2.0);
        p.setPen(Qt::white);
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        p.drawText(cap, Qt::AlignCenter, tr("已隐藏水印 · 显示"));
    }

    drawCardFrame();
}

// —— 交互 ——

void PreviewCanvas::wheelEvent(QWheelEvent *event)
{
    if (m_image.isNull())
        return;
    const int dy = event->angleDelta().y();
    if (dy == 0)
        return;   // 横向滚动（触控板双指水平）不触发缩放
    const qreal step = dy > 0 ? 1.2 : 1.0 / 1.2;
    setZoomAt(m_zoom * step);   // 中心锚，缩放即回中
}

void PreviewCanvas::mousePressEvent(QMouseEvent *event)
{
    if (m_image.isNull()) {
        // 空态点击 = 打开文件对话框（文案承诺「点击选择」）
        if (event->button() == Qt::LeftButton)
            emit importRequested();
        return;
    }

    // 纯看图胶囊命中
    if (event->button() == Qt::LeftButton && m_watermarkHidden
        && capsuleRect().contains(event->pos())) {
        emit showWatermarkRequested();
        return;
    }

    if (event->button() == Qt::MiddleButton
        || (event->button() == Qt::LeftButton && m_spacePan)) {
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // 纯看图态（水印隐藏）：元素不可见则不可交互
        if (m_watermarkHidden)
            return;
        const QPointF imgPos = toImagePos(event->pos());
        const QUuid hit = hitTest(imgPos);
        if (!hit.isNull()) {
            m_selectedId = hit;
            emit elementSelected(hit);
            // 准备拖动（位移过死区才算真拖动）
            m_dragging = true;
            m_dragStartImg = imgPos;
            for (const auto &entry : m_layouts) {
                if (entry.first == hit) {
                    m_dragOriginTopLeft = entry.second.pxRect.topLeft();
                    break;
                }
            }
            m_dragRect = QRectF();
            setCursor(Qt::SizeAllCursor);
            update();
            return;
        }
        // 空白区：取消选中 + 开始平移（左键拖动移动视图，用户指令；
        // 交互替代原「按住对比原图」，对比原图功能已整体移除）
        if (!m_selectedId.isNull()) {
            m_selectedId = QUuid();
            emit elementSelected(QUuid());
            update();
        }
        m_panning = true;
        m_panStart = event->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void PreviewCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        QPointF offset = m_panOffset + QPointF(event->pos() - m_panStart);
        // 边界钳制：图片不允许完全拖出视口（任一边至少留 24px 可见）
        const qreal imgW = m_image.width() * m_zoom;
        const qreal imgH = m_image.height() * m_zoom;
        offset.setX(qBound(qreal(kPanEdge) - imgW, offset.x(),
                           qreal(width() - kPanEdge)));
        offset.setY(qBound(qreal(kPanEdge) - imgH, offset.y(),
                           qreal(height() - kPanEdge)));
        m_panOffset = offset;
        m_panStart = event->pos();
        update();
        return;
    }

    if (m_dragging) {
        const QPointF delta = toImagePos(event->pos()) - m_dragStartImg;
        if (!m_dragRect.isNull() || QPointF::dotProduct(delta, delta) >
                                        kDragDeadZone * kDragDeadZone) {
            if (m_dragRect.isNull()) {
                // 首次过死区：从布局取元素真实尺寸
                for (const auto &entry : m_layouts) {
                    if (entry.first == m_selectedId) {
                        m_dragRect = QRectF(m_dragOriginTopLeft + delta,
                                            entry.second.pxRect.size());
                        break;
                    }
                }
            } else {
                m_dragRect.moveTopLeft(m_dragOriginTopLeft + delta);
            }

            // —— 吸附（§3.4：中心/三分线/画布边，命中显虚线）——
            const qreal cw = m_image.width();
            const qreal ch = m_image.height();
            const QRectF r = m_dragRect;
            m_guideV = QLineF();
            m_guideH = QLineF();
            auto trySnap = [this, cw, ch](qreal value, qreal target,
                                          bool vertical) -> qreal {
                const qreal threshold = kSnapThreshold / m_zoom;
                if (qAbs(value - target) <= threshold) {
                    if (vertical)
                        m_guideV = QLineF(target, 0, target, ch);
                    else
                        m_guideH = QLineF(0, target, cw, target);
                    return target;
                }
                return value;
            };
            // 水平方向：矩形中心/左右边 vs 画布中心/三分线/边
            qreal x = r.center().x();
            x = trySnap(x, cw / 2.0, true);
            x = trySnap(x, cw / 3.0, true);
            x = trySnap(x, cw * 2.0 / 3.0, true);
            // 垂直方向：矩形中心/上下边 vs 画布中心/三分线/边
            qreal y = r.center().y();
            y = trySnap(y, ch / 2.0, false);
            y = trySnap(y, ch / 3.0, false);
            y = trySnap(y, ch * 2.0 / 3.0, false);
            m_dragRect.moveCenter(QPointF(x, y));
            update();
        }
        return;
    }

    // hover 光标：空白 = 抓手（提示可拖动平移），元素 = 移动
    const QUuid hover = hitTest(toImagePos(event->pos()));
    if (hover != m_hoverId) {
        m_hoverId = hover;
        setCursor(hover.isNull() ? Qt::OpenHandCursor : Qt::SizeAllCursor);
        update();
    }
}

void PreviewCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_dragging = false;
        m_guideV = QLineF();
        m_guideH = QLineF();
        setCursor(m_hoverId.isNull() ? Qt::ArrowCursor : Qt::SizeAllCursor);
        if (!m_dragRect.isNull()) {
            // 提交：offset 增量（相对单位，画布尺寸归一；anchor 不变）
            const QPointF delta =
                m_dragRect.topLeft() - m_dragOriginTopLeft;
            emit elementMoved(m_selectedId,
                              QPointF(delta.x() / m_image.width(),
                                      delta.y() / m_image.height()));
        }
        m_dragRect = QRectF();
        update();
    }
    // 平移结算无条件执行（先松空格再松左键的顺序也要退出拖拽态）
    if (m_panning
        && (event->button() == Qt::MiddleButton
            || event->button() == Qt::LeftButton)) {
        m_panning = false;
        setCursor(Qt::OpenHandCursor);
    }
}

void PreviewCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (m_image.isNull() || event->button() != Qt::LeftButton
        || m_watermarkHidden)   // 纯看图态不可编辑隐藏元素
        return;
    const QUuid hit = hitTest(toImagePos(event->pos()));
    if (hit.isNull())
        return;
    for (const auto &entry : m_layouts) {
        if (entry.first == hit) {
            // 只有文本类元素支持就地编辑（Logo/Divider 无文本）
            const bool textLike = !entry.second.resolvedText.isEmpty();
            if (textLike) {
                m_selectedId = hit;
                emit elementSelected(hit);
                beginTextEdit(hit);
            }
            return;
        }
    }
}

void PreviewCanvas::beginTextEdit(const QUuid &id)
{
    commitTextEdit(false);
    QString text;
    QRectF pxRect;
    bool found = false;
    for (const auto &entry : m_layouts) {
        if (entry.first == id) {
            text = entry.second.resolvedText;
            pxRect = entry.second.pxRect;
            found = true;
            break;
        }
    }
    if (!found || m_image.isNull())
        return;

    m_editingId = id;
    m_editor = new QLineEdit(this);
    m_editor->setText(text);
    m_editor->selectAll();
    // 覆盖元素视口矩形（最少可点中）
    const QRectF imgRect = imageRect();
    QRectF viewRect =
        QRectF(imgRect.topLeft() + pxRect.topLeft() * m_zoom,
               pxRect.size() * m_zoom).toAlignedRect();
    viewRect = viewRect.adjusted(-4, -4, 4, 4).intersected(rect());
    m_editor->setGeometry(viewRect.toRect());
    m_editor->show();
    m_editor->setFocus();
    connect(m_editor, &QLineEdit::editingFinished, this, [this] {
        commitTextEdit(true);
    });
}

void PreviewCanvas::commitTextEdit(bool submit)
{
    if (!m_editor)
        return;
    QLineEdit *editor = m_editor;
    m_editor = nullptr;   // 先断引用防 editingFinished 重入
    const QUuid id = m_editingId;
    m_editingId = QUuid();
    const QString text = editor->text();
    editor->deleteLater();
    if (submit && !id.isNull())
        emit elementTextEdited(id, text);
    setFocus();
    update();
}

void PreviewCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan = true;
        setCursor(Qt::OpenHandCursor);
    } else if (event->key() == Qt::Key_Delete && !m_selectedId.isNull()
               && !m_editor && !m_watermarkHidden) {
        emit elementDeleteRequested(m_selectedId);
    } else if (event->key() == Qt::Key_Escape && m_editor) {
        commitTextEdit(false);
    } else if (event->key() == Qt::Key_Escape && !m_selectedId.isNull()) {
        m_selectedId = QUuid();   // Esc 取消选中（§7.2）
        emit elementSelected(QUuid());
        update();
    }
    QWidget::keyPressEvent(event);
}

void PreviewCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePan = false;
        if (!m_panning)
            setCursor(Qt::OpenHandCursor);   // 空白常态即抓手（可平移）
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

// 右键菜单：CustomContextMenu 信号是 QWidget 级的（无元素信息），
// 这里用 contextMenuEvent 覆盖代替——头文件未声明 contextMenuEvent，
// 通过 CustomContextMenu 时序：press(RB) → hitTest 由 mousePress 完成。
// v1 简化：右键即选中命中元素并发菜单请求（菜单本体在 MainWindow）。

} // namespace fxt
