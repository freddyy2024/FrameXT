#include "panels/WatermarkPanel.h"

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFontComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSlider>
#include <QSpacerItem>
#include <QToolButton>
#include <QVBoxLayout>

#include "../Theme.h"

namespace fxt {

namespace {
// 行内元素名称（fieldKey → 中文标签；与 BasicInfoPanel 的字段标签同源语义）
QString elementLabel(const Element &el)
{
    if (el.kind == ElementKind::Logo)
        return WatermarkPanel::tr("Logo");
    if (el.fieldKey.isEmpty())
        return WatermarkPanel::tr("自定义文本");
    static const QHash<QString, QString> kLabels = {
        { QStringLiteral("make"), WatermarkPanel::tr("相机品牌") },
        { QStringLiteral("model"), WatermarkPanel::tr("相机型号") },
        { QStringLiteral("lens"), WatermarkPanel::tr("镜头型号") },
        { QStringLiteral("paramString"), WatermarkPanel::tr("参数串") },
        { QStringLiteral("dateTime"), WatermarkPanel::tr("拍摄日期") },
        { QStringLiteral("focalLengthEq"), WatermarkPanel::tr("等效焦距") },
        { QStringLiteral("gpsLatitude"), WatermarkPanel::tr("GPS") },
    };
    return kLabels.value(el.fieldKey, el.fieldKey);
}
} // namespace

WatermarkPanel::WatermarkPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("card"));
    // 普通 QWidget 的 QSS background 不生效，必须开 StyledBackground
    // （用户指令：右侧面板背景与导入导出卡一致的白底）
    setAttribute(Qt::WA_StyledBackground, true);
    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(8);

    // —— 标题行 + 添加自定义文本 ——
    auto *header = new QWidget(this);
    header->setObjectName("panelHeader");
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 12, 0);
    auto *title = new QLabel(tr("水印内容"), header);
    title->setObjectName("panelTitle");
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    auto *addButton = new QPushButton(tr("＋ 自定义文本"), header);
    addButton->setObjectName("secondaryButton");
    addButton->setToolTip(tr("添加自定义文本（最多 5 条）"));
    headerLayout->addWidget(addButton);
    rootLayout->addWidget(header);

    // —— 行列表（可滚动）——
    auto *scroll = new QScrollArea(this);
    scroll->setObjectName("watermarkScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_rowsHost = new QWidget(scroll);
    m_rowsHost->setObjectName("watermarkRows");
    auto *rowsLayout = new QVBoxLayout(m_rowsHost);
    rowsLayout->setContentsMargins(8, 8, 8, 8);
    rowsLayout->setSpacing(2);
    scroll->setWidget(m_rowsHost);
    rootLayout->addWidget(scroll, 1);

    // —— 选中元素样式区 ——
    buildStyleArea();
    rootLayout->addWidget(m_styleGroup);
    m_styleGroup->hide();

    // —— 应用范围（§5.5：模板/水印/样式统一的批量入口）——
    auto *applyRow = new QWidget(this);
    auto *applyLayout = new QHBoxLayout(applyRow);
    applyLayout->setContentsMargins(12, 4, 12, 8);
    applyLayout->setSpacing(6);
    applyLayout->addWidget(new QLabel(tr("应用到"), applyRow));
    auto *scopeCombo = new QComboBox(applyRow);
    // 照片列表已单选（用户指令），「选中的照片」与「当前照片」重合，不提供
    scopeCombo->addItem(tr("当前照片"), int(ApplyScope::Current));
    scopeCombo->addItem(tr("全部照片"), int(ApplyScope::All));
    applyLayout->addWidget(scopeCombo, 1);
    auto *applyButton = new QPushButton(tr("应用…"), applyRow);
    applyButton->setObjectName("primaryButton");
    applyLayout->addWidget(applyButton);
    rootLayout->addWidget(applyRow);

    connect(addButton, &QPushButton::clicked, this,
            &WatermarkPanel::addCustomTextRequested);
    connect(applyButton, &QPushButton::clicked, this, [this, scopeCombo] {
        emit applyRequested(ApplyScope(scopeCombo->currentData().toInt()));
    });
}

void WatermarkPanel::buildStyleArea()
{
    m_styleGroup = new QGroupBox(tr("选中元素样式"), this);
    m_styleGroup->setObjectName("styleGroup");
    auto *groupLayout = new QVBoxLayout(m_styleGroup);
    // 内边距（用户指令）：两侧 5px、顶部 10px（5+5 二次加高）、底部 10px
    groupLayout->setContentsMargins(5, 10, 5, 10);
    groupLayout->setSpacing(6);

    // —— 文字属性容器（选中 Logo 时整组隐藏，切显 Logo 换标行）——
    m_textStyleRows = new QWidget(m_styleGroup);
    auto *layout = new QGridLayout(m_textStyleRows);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setHorizontalSpacing(6);
    layout->setVerticalSpacing(6);

    m_fontCombo = new QFontComboBox(m_textStyleRows);
    // 默认按最长字体名自适应宽度，会把窄侧栏的最小宽度撑大（用户指令：
    // 侧栏调窄）；改按固定内容长度自适应，超宽字体名靠下拉列表展示
    m_fontCombo->setMinimumContentsLength(8);
    m_fontCombo->setSizeAdjustPolicy(
        QComboBox::AdjustToMinimumContentsLengthWithIcon);
    // 重排（用户指令：原 4 列混排凌乱）——字体下拉独占整行，
    // 其余字段两列对齐：标签 | 控件 | 标签 | 控件
    layout->addWidget(m_fontCombo, 0, 0, 1, 4);

    m_fontScaleSpin = new QDoubleSpinBox(m_textStyleRows);
    m_fontScaleSpin->setRange(0.005, 0.10);
    m_fontScaleSpin->setSingleStep(0.002);
    m_fontScaleSpin->setDecimals(3);
    m_fontScaleSpin->setFixedWidth(96);   // 用户指令：原 72 加宽 1/3
    m_fontScaleSpin->setToolTip(tr("字号（相对画布宽）"));
    layout->addWidget(new QLabel(tr("字号"), m_textStyleRows), 1, 0);
    layout->addWidget(m_fontScaleSpin, 1, 1);

    m_boldButton = new QToolButton(m_textStyleRows);
    m_boldButton->setObjectName(QStringLiteral("styleMiniButton"));
    m_boldButton->setText(tr("B"));
    m_boldButton->setCheckable(true);
    m_boldButton->setStyleSheet(QStringLiteral("font-weight: bold"));
    m_italicButton = new QToolButton(m_textStyleRows);
    m_italicButton->setObjectName(QStringLiteral("styleMiniButton"));
    m_italicButton->setText(tr("I"));
    m_italicButton->setCheckable(true);
    m_italicButton->setStyleSheet(QStringLiteral("font-style: italic"));
    // B/I 按钮容器（用户指令：与字号框等距左对齐排列，间距 6px）——
    // 直接入 grid 列会被字间距输入框（96px 列宽）撑开；左侧 margin 4px
    // 补足与字号框的视觉间距（用户指令：粗体按钮左侧留 margin）
    auto *styleBtns = new QWidget(m_textStyleRows);
    auto *btnLay = new QHBoxLayout(styleBtns);
    btnLay->setContentsMargins(4, 0, 0, 0);
    btnLay->setSpacing(6);
    btnLay->addWidget(m_boldButton);
    btnLay->addWidget(m_italicButton);
    btnLay->addStretch();
    layout->addWidget(styleBtns, 1, 2, 1, 3);

    m_colorButton = new QPushButton(m_textStyleRows);
    m_colorButton->setObjectName(QStringLiteral("colorButton"));
    m_colorButton->setFixedSize(40, 24);
    m_colorButton->setToolTip(tr("文字颜色"));
    layout->addWidget(new QLabel(tr("颜色"), m_textStyleRows), 2, 0);
    layout->addWidget(m_colorButton, 2, 1);

    m_spacingSpin = new QDoubleSpinBox(m_textStyleRows);
    m_spacingSpin->setRange(-0.02, 0.20);
    m_spacingSpin->setSingleStep(0.002);
    m_spacingSpin->setDecimals(3);
    m_spacingSpin->setFixedWidth(96);   // 用户指令：原 72 加宽 1/3，小数显全
    m_spacingSpin->setToolTip(tr("字间距（相对画布宽）"));
    layout->addWidget(new QLabel(tr("字间距"), m_textStyleRows), 2, 2);
    layout->addWidget(m_spacingSpin, 2, 3);

    m_opacitySlider = new QSlider(Qt::Horizontal, m_textStyleRows);
    m_opacitySlider->setRange(0, 100);
    layout->addWidget(new QLabel(tr("透明度"), m_textStyleRows), 3, 0);
    layout->addWidget(m_opacitySlider, 3, 1, 1, 4);

    m_resetPosButton = new QPushButton(tr("还原位置"), m_textStyleRows);
    m_resetPosButton->setObjectName("secondaryButton");
    layout->addWidget(m_resetPosButton, 4, 0, 1, 5);
    groupLayout->addWidget(m_textStyleRows);

    // —— Logo 换标行（方案 A：元素级自定义 Logo，仅 Logo 选中时显示）——
    m_logoStyleRow = new QWidget(m_styleGroup);
    auto *logoLayout = new QHBoxLayout(m_logoStyleRow);
    logoLayout->setContentsMargins(0, 0, 0, 0);
    logoLayout->setSpacing(6);
    auto *replaceLogoButton =
        new QPushButton(tr("更换 Logo…"), m_logoStyleRow);
    replaceLogoButton->setObjectName("secondaryButton");
    replaceLogoButton->setToolTip(tr("选择本地图片替换品牌 Logo（建议透明底 PNG）"));
    auto *resetLogoButton = new QPushButton(tr("还原默认"), m_logoStyleRow);
    resetLogoButton->setObjectName("secondaryButton");
    resetLogoButton->setToolTip(tr("恢复按照片品牌自动匹配"));
    logoLayout->addWidget(replaceLogoButton, 1);
    logoLayout->addWidget(resetLogoButton, 1);
    groupLayout->addWidget(m_logoStyleRow);
    m_logoStyleRow->hide();
    connect(replaceLogoButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedId.isNull())
            emit logoReplaceRequested(m_selectedId);
    });
    connect(resetLogoButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedId.isNull())
            emit logoResetRequested(m_selectedId);
    });

    // 样式区任一控件变化 → 汇总当前 TextStyle 发出
    auto emitStyle = [this] {
        if (m_updatingStyle || m_selectedId.isNull())
            return;
        TextStyle style;
        style.fontFamily = m_fontCombo->currentFont().family();
        style.fontScale = m_fontScaleSpin->value();
        style.bold = m_boldButton->isChecked();
        style.italic = m_italicButton->isChecked();
        style.letterSpacing = m_spacingSpin->value();
        style.color = m_colorButton->property("color").value<QColor>();
        style.opacity = m_opacitySlider->value() / 100.0;
        emit elementStyleChanged(m_selectedId, style);
    };
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, emitStyle);
    connect(m_fontScaleSpin, &QDoubleSpinBox::valueChanged, this, emitStyle);
    connect(m_boldButton, &QToolButton::toggled, this, emitStyle);
    connect(m_italicButton, &QToolButton::toggled, this, emitStyle);
    connect(m_spacingSpin, &QDoubleSpinBox::valueChanged, this, emitStyle);
    connect(m_opacitySlider, &QSlider::valueChanged, this, emitStyle);
    connect(m_colorButton, &QPushButton::clicked, this,
            [this, emitStyle] {
        const QColor current =
            m_colorButton->property("color").value<QColor>();
        const QColor picked =
            QColorDialog::getColor(current, this, tr("文字颜色"));
        if (!picked.isValid())
            return;
        m_colorButton->setProperty("color", picked);
        m_colorButton->setStyleSheet(
            QStringLiteral("background: %1").arg(picked.name()));
        emitStyle();
    });
    // 还原位置：独立信号（MainWindow 接 resetElementPosition）
    connect(m_resetPosButton, &QPushButton::clicked, this, [this] {
        if (!m_selectedId.isNull())
            emit elementResetPositionRequested(m_selectedId);
    });
}

void WatermarkPanel::setElements(const QList<Element> &elements,
                                 const QHash<QUuid, QString> &displayValues,
                                 int photoCount)
{
    m_elements = elements;
    m_displayValues = displayValues;
    m_photoCount = photoCount;
    rebuildRows();
}

QWidget *WatermarkPanel::makeRow(const Element &element,
                                 const QString &displayValue)
{
    auto *row = new QWidget(m_rowsHost);
    row->setObjectName(QStringLiteral("watermarkRow"));
    auto *layout = new QHBoxLayout(row);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(6);

    auto *check = new QCheckBox(row);
    check->setChecked(element.visible);
    layout->addWidget(check);

    const bool isCustom = element.kind == ElementKind::Text
                          && element.fieldKey.isEmpty();
    auto *label = new QLabel(elementLabel(element), row);
    label->setFixedWidth(64);
    layout->addWidget(label);

    // 内容编辑：文本类元素行内改值（绑定字段=覆盖层，自定义=literal）；
    // Logo/Divider 无内容编辑
    if (element.kind == ElementKind::Text) {
        auto *edit = new QLineEdit(row);
        edit->setPlaceholderText(displayValue);
        edit->setText(displayValue);
        edit->setToolTip(isCustom ? tr("自定义文本内容")
                                  : tr("留空恢复原字段值"));
        layout->addWidget(edit, 1);
        // 失焦也会发 editingFinished——只有用户真实改动过才提交，
        // 防止「点进去不改值点别处」把合成显示值固化成覆盖层
        connect(edit, &QLineEdit::textEdited, row,
                [edit] { edit->setProperty("dirty", true); });
        connect(edit, &QLineEdit::editingFinished, row, [this, element, edit] {
            if (!edit->property("dirty").toBool())
                return;
            edit->setProperty("dirty", false);
            emit elementContentEdited(element.id, edit->text());
        });
    } else {
        layout->addWidget(new QLabel(displayValue.isEmpty()
                                         ? QStringLiteral("—")
                                         : displayValue,
                                     row),
                          1);
    }

    if (isCustom) {
        auto *remove = new QToolButton(row);
        remove->setText(tr("✕"));
        remove->setToolTip(tr("删除此自定义文本"));
        layout->addWidget(remove);
        connect(remove, &QToolButton::clicked, this, [this, element] {
            emit removeCustomTextRequested(element.id);
        });
    }

    // 勾选 = 元素总开关（E3：直接控制 visible，无同步死角）
    connect(check, &QCheckBox::toggled, row, [this, element](bool visible) {
        emit elementVisibleToggled(element.id, visible);
    });
    // 点击行（编辑框/勾选框以外区域）= 选中联动（§3.5 行高亮）
    row->setProperty("elementId", element.id);
    row->installEventFilter(this);
    label->installEventFilter(this);
    m_rowByElement.insert(element.id, row);
    return row;
}

bool WatermarkPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        const QVariant id = watched->property("elementId");
        if (!id.isNull()) {
            setSelected(id.toUuid());
            emit elementSelected(id.toUuid());
        }
    }
    return QWidget::eventFilter(watched, event);
}

void WatermarkPanel::rebuildRows()
{
    // 清空旧行（含编辑态），按当前元素树重建
    m_rowByElement.clear();
    QList<QWidget*> stale;
    auto *rowsLayout = qobject_cast<QVBoxLayout *>(m_rowsHost->layout());
    while (rowsLayout->count() > 0) {
        QLayoutItem *item = rowsLayout->takeAt(0);
        if (item->widget())
            stale.append(item->widget());
        delete item;
    }
    for (QWidget *w : stale)
        w->deleteLater();

    // 空态禁用（45% 透明不可交互，§3.4）
    setEnabled(m_photoCount > 0);

    auto *rowsLayout2 = qobject_cast<QVBoxLayout *>(m_rowsHost->layout());
    for (const Element &el : m_elements) {
        // 模板派生行按 fieldKey 分组展示，自定义文本垫底（§3.5 顺序）
        rowsLayout2->addWidget(makeRow(el, m_displayValues.value(el.id)));
    }
    rowsLayout2->addStretch();
}

void WatermarkPanel::setSelected(const QUuid &id)
{
    // 行选中高亮（§3.5：画布选中 ⇄ 对应行联动）
    if (QWidget *prev = m_rowByElement.value(m_selectedId)) {
        prev->setProperty("rowSelected", false);
        prev->style()->unpolish(prev);
        prev->style()->polish(prev);
    }
    m_selectedId = id;
    if (QWidget *row = m_rowByElement.value(id)) {
        row->setProperty("rowSelected", true);
        row->style()->unpolish(row);
        row->style()->polish(row);
        row->setVisible(true);
    }
    // 样式区：Text 显示文字属性，Logo 显示换标行，其余（Divider）收起
    const Element *selected = nullptr;
    for (const Element &el : m_elements) {
        if (el.id == id && (el.kind == ElementKind::Text
                            || el.kind == ElementKind::Logo)) {
            selected = &el;
            break;
        }
    }
    if (!selected) {
        m_styleGroup->hide();
        return;
    }
    m_styleGroup->show();
    const bool isLogo = selected->kind == ElementKind::Logo;
    m_textStyleRows->setVisible(!isLogo);
    m_logoStyleRow->setVisible(isLogo);
    if (isLogo)
        return;   // Logo 无文字属性可填
    m_updatingStyle = true;
    const TextStyle &ts = selected->style;
    m_fontCombo->setCurrentFont(QFont(ts.fontFamily));
    m_fontScaleSpin->setValue(ts.fontScale);
    m_boldButton->setChecked(ts.bold);
    m_italicButton->setChecked(ts.italic);
    m_spacingSpin->setValue(ts.letterSpacing);
    m_opacitySlider->setValue(qRound(ts.opacity * 100));
    m_colorButton->setProperty("color", ts.color);
    m_colorButton->setStyleSheet(
        QStringLiteral("background: %1").arg(ts.color.name()));
    m_updatingStyle = false;
}

} // namespace fxt
