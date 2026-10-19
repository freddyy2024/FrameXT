#pragma once

#include "CaptionIcons.h"

#include <QColor>
#include <QWidget>

class QLabel;
class QToolButton;

namespace fxt {

// 自绘标题栏（04-UI设计.md §2.1）：品牌 Logo + 产品名 + 语言切换 + 窗口三按钮。
// 拖拽移动 = press 记录起点、move 超拖动阈值后 startSystemMove（延迟启动，
// 保住双击事件派发）；双击最大化在本类处理；HTCAPTION 原生路径 Qt 6.11 不可用。
class TitleBar : public QWidget
{
    Q_OBJECT

public:
    explicit TitleBar(QWidget *parent = nullptr);

    // 原生命中测试辅助：pos 为本控件坐标系内的点。
    // 返回 true 表示落在可交互控件上（应返回 HTCLIENT，保留 Qt 点击），
    // 返回 false 表示空白区域（应返回 HTCAPTION，交由系统拖拽）。
    bool isInteractiveAt(const QPoint &pos) const;

    // 窗口最大化状态变化时刷新「最大化 / 向下还原」图标
    void updateMaximizeButton(bool maximized);
    // 启动回填：按持久化语言 key（"zh-CN"/"zh-TW"/"en"）刷新按钮文本
    void updateLanguageButton(const QString &code);
    // 纯看图态同步（👁 按钮选中态刷新）
    void updateWatermarkEyeButton(bool hidden);
    // 空态防呆：无照片时 👁 不可交互（水印不存在，切换无意义）
    void updateEyeEnabled(bool enabled);

signals:
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();
    // 纯看图开关（👁，§3.4 第三态入口）
    void watermarkEyeToggled(bool hidden);
    // i18n（3.9）：语言菜单选择（"zh-CN" / "zh-TW" / "en"）
    void languageSelected(const QString &code);
    // 关于对话框入口（用户指令：从导入导出面板移至标题栏）
    void aboutRequested();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QPixmap logoPixmap() const;
    // 窗口标题按钮：图标为自绘矢量（CaptionIcons），hoverColor 对应各按钮
    // QSS 悬停态下的图标色（关闭钮悬停红底，传白色）
    QToolButton *createCaptionButton(const QString &objectName,
                                     Glyph glyph,
                                     const QString &tooltip,
                                     const QColor &hoverColor);

    QLabel *m_logoLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_languageButton = nullptr;
    QToolButton *m_eyeButton = nullptr;
    QToolButton *m_minButton = nullptr;
    QToolButton *m_maxButton = nullptr;
    QToolButton *m_closeButton = nullptr;
    QPoint m_pressPos;   // 拖动阈值检测起点（null = 未在拖动待命态）
};

} // namespace fxt
