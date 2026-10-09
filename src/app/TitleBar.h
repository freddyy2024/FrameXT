#pragma once

#include <QWidget>

class QLabel;
class QToolButton;

// 自绘标题栏（04-UI设计.md §2.1）：品牌 Logo + 产品名 + 语言切换 + 窗口三按钮。
// 拖拽移动 / 双击最大化 / Win Snap 贴靠由 MainWindow 的原生命中测试（HTCAPTION）实现，
// 本类只负责视觉呈现与按钮、语言菜单。
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

signals:
    void minimizeClicked();
    void maximizeClicked();
    void closeClicked();

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    QPixmap logoPixmap() const;
    QToolButton *createCaptionButton(const QString &objectName,
                                     const QString &glyph,
                                     const QString &tooltip);

    QLabel *m_logoLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QToolButton *m_languageButton = nullptr;
    QToolButton *m_minButton = nullptr;
    QToolButton *m_maxButton = nullptr;
    QToolButton *m_closeButton = nullptr;
};
