#pragma once

// 自绘标题栏/工具条矢量图标（2.0.2）。
// 背景：窗口按钮与工具条图标原用 Segoe 图标字体（Win11 Fluent Icons /
// Win10 MDL2 Assets 私有区字形），精简版 Windows（Ghost 镜像常删该字体）
// 上整体空白；且微软版权字体不可随包分发。改为 QPainter 线稿：
// 任意系统一致、任意 DPI 清晰（1x+2x 双档，悬停态经 QIcon::Active 呈现）。

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>
#include <QtGlobal>

namespace fxt {

enum class Glyph {
    Minimize,   // ─          标题栏最小化
    Maximize,   // □          标题栏最大化
    Restore,    // □ 双框      标题栏向下还原
    Close,      // ✕          标题栏关闭
    Eye,        // 👁          纯看图开关
    Info,       // ⓘ          关于
    Undo,       // ↶ 回转箭头   画布撤销
    FitPage,    // 四角托架     适配窗口
    Delete,     // 🗑 垃圾桶    清空列表
    Copy,       // ⧉ 双页       复制属性
};

// 12x12 逻辑网格线稿（内容主体约 2..10）；dpr 放大保证高 DPI 清晰
inline QPixmap glyphPixmap(Glyph g, const QColor &color, qreal dpr)
{
    QPixmap pm(qRound(12 * dpr), qRound(12 * dpr));
    pm.setDevicePixelRatio(dpr);   // 画布坐标即 12x12 逻辑格，勿再 scale（双重缩放会把内容挤进左上角）
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    QPen pen(color, 1.1);
    pen.setCapStyle(Qt::FlatCap);
    pen.setJoinStyle(Qt::MiterJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    auto line = [&](qreal x1, qreal y1, qreal x2, qreal y2) {
        p.drawLine(QPointF(x1, y1), QPointF(x2, y2));
    };
    switch (g) {
    case Glyph::Minimize:
        line(2.5, 8.5, 9.5, 8.5);
        break;
    case Glyph::Maximize:
        p.drawRect(QRectF(2.5, 2.5, 7, 7));
        break;
    case Glyph::Restore:
        // 后框（右上）+ 前框（左下），Segoe 同款错位双框
        p.drawPolyline(QVector<QPointF> { { 5.5, 2 }, { 10, 2 },
                                          { 10, 6.5 }, { 8, 6.5 } });
        p.drawPolyline(QVector<QPointF> { { 7, 4 }, { 2, 4 }, { 2, 10 },
                                          { 7, 10 }, { 7, 4 } });
        break;
    case Glyph::Close:
        line(3, 3, 9, 9);
        line(9, 3, 3, 9);
        break;
    case Glyph::Eye: {
        QPainterPath lid;
        lid.moveTo(1.2, 6);
        lid.quadTo(6, 1.4, 10.8, 6);
        lid.quadTo(6, 10.6, 1.2, 6);
        p.drawPath(lid);
        p.drawEllipse(QPointF(6, 6), 2.1, 2.1);
        break;
    }
    case Glyph::Info: {
        p.drawEllipse(QRectF(1.8, 1.8, 8.4, 8.4));
        p.setBrush(color);
        p.drawEllipse(QPointF(6, 4.2), 0.8, 0.8);
        pen.setWidthF(1.4);
        p.setPen(pen);
        line(6, 5.9, 6, 8.7);
        break;
    }
    case Glyph::Undo: {
        // 3/4 圆弧（开口右下）+ 左上箭头（视觉近似 Segoe E7A7 回转箭头）
        QPainterPath arc;
        arc.arcTo(QRectF(2.4, 3.4, 7.2, 7.2), -45, 270);
        p.drawPath(arc);
        p.setBrush(color);
        p.setPen(Qt::NoPen);
        p.drawPolygon(QPolygonF { { 0.6, 3.0 }, { 4.2, 2.2 }, { 2.8, 5.6 } });
        break;
    }
    case Glyph::FitPage:
        // 四角托架 + 中央小矩形（适配页面）
        line(1.5, 3.8, 1.5, 1.5);
        line(1.5, 1.5, 3.8, 1.5);
        line(8.2, 1.5, 10.5, 1.5);
        line(10.5, 1.5, 10.5, 3.8);
        line(10.5, 8.2, 10.5, 10.5);
        line(10.5, 10.5, 8.2, 10.5);
        line(3.8, 10.5, 1.5, 10.5);
        line(1.5, 10.5, 1.5, 8.2);
        p.drawRect(QRectF(4.4, 4.4, 3.2, 3.2));
        break;
    case Glyph::Delete:
        // 垃圾桶：盖 + 提钮 + 收窄桶身 + 两道竖纹
        line(2.2, 3.4, 9.8, 3.4);
        line(4.6, 1.8, 7.4, 1.8);
        line(4.6, 1.8, 4.6, 3.4);
        line(7.4, 1.8, 7.4, 3.4);
        p.drawPolyline(QVector<QPointF> { { 3.4, 4.9 }, { 4.1, 10.4 },
                                          { 7.9, 10.4 }, { 8.6, 4.9 } });
        line(5.1, 6.4, 5.2, 9);
        line(6.9, 6.4, 6.8, 9);
        break;
    case Glyph::Copy:
        // 前页实框 + 后页露右上角（Segoe E8C8 双页语义）
        p.drawPolyline(QVector<QPointF> { { 4.8, 2 }, { 9.8, 2 },
                                          { 9.8, 7 }, { 8.3, 7 } });
        p.drawRect(QRectF(2.2, 4.2, 5.6, 5.6));
        break;
    }
    return pm;
}

// normal / Active（悬停）双态图标：QCommonStyle 在鼠标悬停时取 Active 档
inline QIcon glyphIcon(Glyph g, const QColor &normal, const QColor &active)
{
    QIcon icon;
    for (qreal dpr : { 1.0, 2.0 }) {
        icon.addPixmap(glyphPixmap(g, normal, dpr), QIcon::Normal);
        icon.addPixmap(glyphPixmap(g, active, dpr), QIcon::Active);
    }
    return icon;
}

} // namespace fxt
