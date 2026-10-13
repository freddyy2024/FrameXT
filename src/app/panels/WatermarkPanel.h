#pragma once

#include <QHash>
#include <QList>
#include <QUuid>
#include <QWidget>

#include "controllers/ProjectController.h"
#include "model/Element.h"

class QCheckBox;
class QDoubleSpinBox;
class QFontComboBox;
class QGroupBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QToolButton;

namespace fxt {

// 水印内容面板（开发计划 3.5 / 04-UI §3.5 面板二）：
// 开关行 = 元素总开关 + 内容编辑合一（E3：勾选即显隐、行内改值无死角）；
// 自定义文本（≤5）添加/删除；选中元素样式区（点行/画布元素联动展开）；
// 应用范围（当前/选中/全部 → §5.5 确认流程）。
class WatermarkPanel : public QWidget
{
    Q_OBJECT

public:
    explicit WatermarkPanel(QWidget *parent = nullptr);

    // 全量重建行列表（元素树变化时；displayValues = 行内编辑框当前值）
    void setElements(const QList<Element> &elements,
                     const QHash<QUuid, QString> &displayValues,
                     int photoCount);
    // 画布选中联动（空 id = 取消，样式区收起）
    void setSelected(const QUuid &id);

signals:
    void elementVisibleToggled(const QUuid &id, bool visible);
    void elementContentEdited(const QUuid &id, const QString &text);
    void elementSelected(const QUuid &id);
    void elementStyleChanged(const QUuid &id, const TextStyle &style);
    void elementResetPositionRequested(const QUuid &id);
    // Logo 元素级换标（方案 A：customLogoPath）/ 还原品牌自动匹配
    void logoReplaceRequested(const QUuid &id);
    void logoResetRequested(const QUuid &id);
    void addCustomTextRequested();
    void removeCustomTextRequested(const QUuid &id);
    void applyRequested(ApplyScope scope);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void rebuildRows();
    void buildStyleArea();
    QWidget *makeRow(const Element &element, const QString &displayValue);

    QWidget *m_rowsHost = nullptr;    // 行容器（垂直流式）
    QList<Element> m_elements;
    QHash<QUuid, QString> m_displayValues;
    int m_photoCount = 0;
    QUuid m_selectedId;

    // 选中元素样式区
    QGroupBox *m_styleGroup = nullptr;
    QWidget *m_textStyleRows = nullptr;   // 文字属性容器（Logo 选中时整组隐藏）
    QWidget *m_logoStyleRow = nullptr;    // Logo 换标按钮行（仅 Logo 选中时显示）
    QFontComboBox *m_fontCombo = nullptr;
    QDoubleSpinBox *m_fontScaleSpin = nullptr;
    QPushButton *m_colorButton = nullptr;
    QToolButton *m_boldButton = nullptr;
    QToolButton *m_italicButton = nullptr;
    QDoubleSpinBox *m_spacingSpin = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QPushButton *m_resetPosButton = nullptr;
    bool m_updatingStyle = false;     // 程序性填充不回发 styleChanged

    QHash<QUuid, QWidget *> m_rowByElement;   // 行选中高亮（§3.5）
};

} // namespace fxt
