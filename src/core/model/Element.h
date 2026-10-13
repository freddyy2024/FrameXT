#pragma once

#include <QColor>
#include <QList>
#include <QPointF>
#include <QString>
#include <QUuid>

namespace fxt {

// 水印元素类别（02-技术设计 §4.1，统一四类）
enum class ElementKind {
    Text,     // 文本（绑定字段或自由内容）
    Logo,     // 相机品牌 Logo 图片
    Divider,  // 分隔竖线（bottom 模板 Logo 后）
    Free,     // 自由元素（用户自定义内容）
};

// 锚点：9 宫格 + 模板专用锚线（LayoutEngine，02-技术设计 §4.3）
enum class Anchor {
    TopLeft, TopCenter, TopRight,
    MiddleLeft, Center, MiddleRight,
    BottomLeft, BottomCenter, BottomRight,
    LogoLineStart,  // bottom 模板专用：Logo 后分隔线 / 文字区起点
};

// 文本样式（值类型约束 v1.1：只存可序列化量；QFont 在 ElementPainter 内即时构造）
struct TextStyle
{
    // 边框文字默认字体 Aileron（原版同款，CC0 随包嵌入，main.cpp 注册）；
    // 仅含拉丁字形，中文由 Qt 缺字回落系统字体
    QString fontFamily = QStringLiteral("Aileron");
    qreal fontScale = 0.028;  // 字号 = fontScale × 画布宽（相对单位制）
    bool bold = false;
    bool italic = false;
    qreal letterSpacing = 0.0;  // 字间距 = letterSpacing × 画布宽（相对单位制）
    QColor color = QColor(Qt::black);
    qreal opacity = 1.0;

    bool operator==(const TextStyle &) const = default;
};

// 水印元素（纯值类型：拷贝即快照，支撑快照式撤销与 JSON 序列化）
struct Element
{
    QUuid id;
    ElementKind kind = ElementKind::Text;
    QString fieldKey;    // 绑定的 Exif 字段 key（空 = 自由内容）
    QString literal;     // 自由文本内容 / 字段覆盖值
    Anchor anchor = Anchor::BottomLeft;
    QPointF offset;      // 相对锚点的偏移（相对单位 0~1）
    TextStyle style;
    // Logo 专用：用户自定义图片路径（方案 A 元素级换标）。非空时渲染
    // 优先加载该文件，空 = 品牌 EXIF 自动匹配（BrandRegistry）
    QString customLogoPath;
    qreal logoScale = 0.6;  // Logo 专用：显示高 = logoScale × 所在行行高
    bool visible = true;
    bool locked = false;
    bool hasCustomPosition = false;  // 用户手动定位后脱离行组自动排布（v1.1）

    bool operator==(const Element &) const = default;
};

} // namespace fxt
