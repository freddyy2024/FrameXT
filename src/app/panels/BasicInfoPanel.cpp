#include "panels/BasicInfoPanel.h"

#include <QApplication>
#include <QClipboard>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace fxt {

namespace {
constexpr int kLabelColumnWidth = 88;
} // namespace

BasicInfoPanel::BasicInfoPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("basicInfoPanel"));

    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 4, 12, 8);
    outer->setSpacing(4);

    // —— 标题行 ——
    auto *header = new QHBoxLayout;
    m_title = new QLabel(tr("基础属性"));
    m_title->setObjectName(QStringLiteral("panelTitle"));
    header->addWidget(m_title);
    header->addStretch();
    auto *copyButton = new QPushButton(tr("复制"));
    copyButton->setObjectName(QStringLiteral("secondaryButton"));
    copyButton->setToolTip(tr("复制全部属性文本"));
    connect(copyButton, &QPushButton::clicked, this,
            &BasicInfoPanel::copyAll);
    header->addWidget(copyButton);
    outer->addLayout(header);

    // —— 内容卡片（可滚动）——
    auto *content = new QWidget;
    content->setObjectName(QStringLiteral("card"));
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
    outer->addWidget(scroll, 1);
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
    // —— 位置 ——
    addRow(tr("纬度"))->fieldKey = QStringLiteral("gpsLatitude");
    addRow(tr("经度"))->fieldKey = QStringLiteral("gpsLongitude");
    addRow(tr("海拔"))->fieldKey = QStringLiteral("gpsAltitude");
    // —— Maker 特色 ——
    addRow(tr("胶片模拟"))->fieldKey = QStringLiteral("fujiFilmSimulation");
    addRow(tr("创意风格"))->fieldKey = QStringLiteral("sonyCreativeStyle");
    addRow(tr("优化校准"))->fieldKey = QStringLiteral("nikonPictureControl");
    // —— 文件 ——
    addRow(tr("软件"))->fieldKey = QStringLiteral("software");
}

void BasicInfoPanel::setRow(const char *fieldKey, const QString &,
                            QLabel *valueLabel,
                            const QHash<QString, QString> &fields,
                            bool hasPhoto)
{
    const QString value =
        hasPhoto ? fields.value(QLatin1String(fieldKey)) : QString();
    valueLabel->setText(value.isEmpty() ? QStringLiteral("—") : value);
}

void BasicInfoPanel::setFields(const QHash<QString, QString> &fields,
                               bool hasPhoto)
{
    // fieldKey 与 buildRows 的注册顺序一一对应（同源生成，防漂移）
    const char *keys[] = {
        "make", "model", "dateTime", "iso", "shutter", "fNumber",
        "exposureComp", "meteringMode", "flash", "whiteBalance",
        "lens", "focalLength", "focalLengthEq",
        "gpsLatitude", "gpsLongitude", "gpsAltitude",
        "fujiFilmSimulation", "sonyCreativeStyle", "nikonPictureControl",
        "software",
    };
    const char *labels[] = {
        "make", "model", "dateTime", "iso", "shutter", "fNumber",
        "exposureComp", "meteringMode", "flash", "whiteBalance",
        "lens", "focalLength", "focalLengthEq",
        "gpsLatitude", "gpsLongitude", "gpsAltitude",
        "fujiFilmSimulation", "sonyCreativeStyle", "nikonPictureControl",
        "software",
    };
    Q_UNUSED(labels);
    const int expected = int(sizeof(keys) / sizeof(keys[0]));
    if (m_rows.size() != expected)
        return;   // 结构漂移防御（buildRows 与 keys 必须同步）
    for (int i = 0; i < expected; ++i)
        setRow(keys[i], QString(), m_rows[i].value, fields, hasPhoto);
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
