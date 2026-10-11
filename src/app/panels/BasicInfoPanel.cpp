#include "panels/BasicInfoPanel.h"

#include "../GlyphButton.h"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QScrollArea>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace fxt {

namespace {
constexpr int kLabelColumnWidth = 88;
} // namespace

BasicInfoPanel::BasicInfoPanel(QWidget *parent)
    : QFrame(parent)
{
    // card 结构与导入导出面板（MainWindow::createCard）一致：
    // 白底圆角卡 + 32 高内部标题栏（标题 + 复制图标按钮）
    setObjectName(QStringLiteral("card"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    // —— 标题栏（面板内部）——
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("panelHeader"));
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 8, 0);
    auto *title = new QLabel(tr("基础属性"), header);
    title->setObjectName(QStringLiteral("panelTitle"));
    headerLayout->addWidget(title);
    headerLayout->addStretch();
    auto *copyButton = createGlyphButton(header, QStringLiteral("toolGlyph"),
                                         0xE8C8,   // Copy
                                         tr("复制全部属性文本"));
    connect(copyButton, &QToolButton::clicked, this,
            &BasicInfoPanel::copyAll);
    headerLayout->addWidget(copyButton);
    outer->addWidget(header);

    // —— 主体：空态提示 / 参数表 双页切换 ——
    m_body = new QStackedWidget(this);
    m_body->setContentsMargins(0, 0, 0, 0);

    auto *emptyHint = new QLabel(tr("导入照片后，此处显示拍摄参数"), m_body);
    emptyHint->setObjectName(QStringLiteral("panelCount"));
    emptyHint->setAlignment(Qt::AlignCenter);
    emptyHint->setWordWrap(true);
    m_body->addWidget(emptyHint);

    // 参数表（可滚动；无边框融入卡片）
    auto *content = new QWidget;
    m_grid = new QGridLayout(content);
    m_grid->setContentsMargins(12, 10, 12, 10);
    m_grid->setHorizontalSpacing(8);
    m_grid->setVerticalSpacing(6);
    m_grid->setColumnMinimumWidth(0, kLabelColumnWidth);

    buildRows();
    m_grid->setColumnStretch(1, 1);
    m_grid->setRowStretch(m_rows.size(), 1);

    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(content);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_body->addWidget(scroll);

    m_body->setCurrentIndex(0);   // 默认空态
    outer->addWidget(m_body, 1);
}

QLabel *BasicInfoPanel::makeValueLabel()
{
    auto *label = new QLabel(QStringLiteral("—"));
    label->setTextInteractionFlags(Qt::TextSelectableByMouse);
    label->setWordWrap(true);
    label->setObjectName(QStringLiteral("infoValue"));
    return label;
}

void BasicInfoPanel::buildRows()
{
    // 分组顺序对齐 §7.2：设备 → 拍摄参数 → 镜头 → 位置 → 文件
    auto addRow = [this](const QString &labelText) {
        const int row = m_rows.size();
        auto *label = new QLabel(labelText);
        label->setObjectName(QStringLiteral("infoLabel"));
        label->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        QLabel *value = makeValueLabel();
        m_grid->addWidget(label, row, 0);
        m_grid->addWidget(value, row, 1);
        Row r;
        r.value = value;
        r.label = labelText;
        m_rows.append(r);
        return &m_rows.last();
    };

    // —— 设备 ——
    addRow(tr("相机器材"))->fieldKey = QStringLiteral("make");
    addRow(tr("相机型号"))->fieldKey = QStringLiteral("model");
    // —— 拍摄参数 ——
    addRow(tr("拍摄时间"))->fieldKey = QStringLiteral("dateTime");
    addRow(tr("ISO"))->fieldKey = QStringLiteral("iso");
    addRow(tr("快门"))->fieldKey = QStringLiteral("shutter");
    addRow(tr("光圈"))->fieldKey = QStringLiteral("fNumber");
    addRow(tr("曝光补偿"))->fieldKey = QStringLiteral("exposureComp");
    addRow(tr("测光模式"))->fieldKey = QStringLiteral("meteringMode");
    addRow(tr("闪光灯"))->fieldKey = QStringLiteral("flash");
    addRow(tr("白平衡"))->fieldKey = QStringLiteral("whiteBalance");
    // —— 镜头 ——
    addRow(tr("镜头"))->fieldKey = QStringLiteral("lens");
    addRow(tr("焦距"))->fieldKey = QStringLiteral("focalLength");
    addRow(tr("等效焦距"))->fieldKey = QStringLiteral("focalLengthEq");
    // 位置（GPS）与 Maker 特色（胶片模拟/创意风格/优化校准）行不展示
    // （用户指令：低频信息不占面板空间；字段解析仍在 core 层保留）
    // —— 文件 ——
    addRow(tr("软件"))->fieldKey = QStringLiteral("software");
}

void BasicInfoPanel::setFields(const QHash<QString, QString> &fields,
                               bool hasPhoto)
{
    // 直接按行注册的 fieldKey 取值（单一事实源）。此前另有平行 keys[]
    // 数组 + 行数校验的「防漂移」防御——增删行时两处必须同步，漏改
    // 即静默不刷新（本条注释即该回归的教训：删 6 行后防御 return，
    // 面板永远停在空态提示）
    for (const Row &row : qAsConst(m_rows)) {
        const QString value =
            hasPhoto ? fields.value(row.fieldKey) : QString();
        row.value->setText(value.isEmpty() ? QStringLiteral("—") : value);
    }
    m_body->setCurrentIndex(hasPhoto ? 1 : 0);   // 有照片显示参数表，否则空态提示
}

void BasicInfoPanel::clearFields()
{
    setFields({}, false);
}

void BasicInfoPanel::copyAll()
{
    QString text;
    for (const Row &row : m_rows) {
        text += QStringLiteral("%1: %2\n")
                    .arg(row.label, row.value->text());
    }
    if (!text.isEmpty())
        QApplication::clipboard()->setText(text);
}

} // namespace fxt
