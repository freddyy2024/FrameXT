#include "TemplateManagerDialog.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QSignalBlocker>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include "controllers/ProjectController.h"
#include "model/TemplateSpec.h"

namespace fxt {

namespace {
// 滑杆通用档位：0–1000 线性映射声明范围 [min, max]（参数范围差异大，
// 统一千分位；step 仅作 UI 提示，滑杆连续取值）
constexpr int kSliderMax = 1000;

double sliderToValue(const TemplateParamDecl &p, int slider)
{
    return p.minimum + (p.maximum - p.minimum) * slider / double(kSliderMax);
}

int valueToSlider(const TemplateParamDecl &p, double value)
{
    if (p.maximum <= p.minimum)
        return 0;
    const double t = (value - p.minimum) / (p.maximum - p.minimum);
    return qBound(0, qRound(t * kSliderMax), kSliderMax);
}

QString valueText(const TemplateParamDecl &p, double value)
{
    if (p.key == QLatin1String("blurStrength"))
        return QStringLiteral("×%1").arg(value, 0, 'f', 2);
    return QStringLiteral("%1%").arg(value * 100.0, 0, 'f', 1);
}
} // namespace

TemplateManagerDialog::TemplateManagerDialog(ProjectController *project,
                                             QWidget *parent)
    : QDialog(parent),
      m_project(project)
{
    setWindowTitle(tr("模板管理"));
    setModal(true);
    resize(680, 460);

    auto *root = new QHBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 12);
    root->setSpacing(12);

    // —— 左列：模板列表 + 操作 ——
    auto *left = new QVBoxLayout;
    left->setSpacing(8);
    m_list = new QListWidget(this);
    m_list->setObjectName("templateList");
    left->addWidget(m_list, 1);

    auto *saveAs = new QPushButton(tr("另存为用户模板…"), this);
    saveAs->setObjectName("secondaryButton");
    connect(saveAs, &QPushButton::clicked, this,
            &TemplateManagerDialog::saveAsNew);
    left->addWidget(saveAs);

    // M5.3 .ftpl 分享包：导入安装为用户模板；导出仅用户模板（04-UI §6）
    auto *import = new QPushButton(tr("导入 .ftpl…"), this);
    import->setObjectName("secondaryButton");
    connect(import, &QPushButton::clicked, this,
            &TemplateManagerDialog::importFromFile);
    left->addWidget(import);

    auto *exportBtn = new QPushButton(tr("导出 .ftpl…"), this);
    exportBtn->setObjectName("secondaryButton");
    m_exportButton = exportBtn;
    connect(exportBtn, &QPushButton::clicked, this,
            &TemplateManagerDialog::exportToFile);
    left->addWidget(exportBtn);

    auto *remove = new QPushButton(tr("删除所选…"), this);
    remove->setObjectName("secondaryButton");
    m_deleteButton = remove;
    connect(remove, &QPushButton::clicked, this,
            &TemplateManagerDialog::removeSelected);
    left->addWidget(remove);
    root->addLayout(left, 1);

    // —— 右列：名称 + 参数编辑 + 动作 ——
    auto *right = new QVBoxLayout;
    right->setSpacing(8);
    m_nameLabel = new QLabel(this);
    m_nameLabel->setObjectName("panelTitle");
    right->addWidget(m_nameLabel);

    auto *paramCaption = new QLabel(tr("参数"), this);
    paramCaption->setObjectName("panelCount");
    right->addWidget(paramCaption);

    m_paramHost = new QWidget(this);
    m_paramLayout = new QVBoxLayout(m_paramHost);
    m_paramLayout->setContentsMargins(0, 0, 0, 0);
    m_paramLayout->setSpacing(6);
    right->addWidget(m_paramHost);
    right->addStretch();

    auto *apply = new QPushButton(tr("应用到当前照片"), this);
    apply->setObjectName("primaryButton");
    connect(apply, &QPushButton::clicked, this,
            &TemplateManagerDialog::applyToCurrentPhoto);
    right->addWidget(apply);

    auto *save = new QPushButton(tr("保存修改到模板"), this);
    save->setObjectName("secondaryButton");
    m_saveButton = save;
    connect(save, &QPushButton::clicked, this,
            &TemplateManagerDialog::saveEdits);
    right->addWidget(save);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    right->addWidget(buttons);
    root->addLayout(right, 2);

    connect(m_list, &QListWidget::currentRowChanged, this,
            [this](int) { rebuildParamPanel(); });

    reloadList({});
}

void TemplateManagerDialog::reloadList(const QString &selectId)
{
    QSignalBlocker blocker(m_list);
    m_list->clear();
    int selectRow = 0;
    const QList<TemplateSpec> specs = TemplateCatalog::allSpecs();
    for (int i = 0; i < specs.size(); ++i) {
        const TemplateSpec &s = specs.at(i);
        auto *item = new QListWidgetItem(
            s.builtin ? tr("%1　· 内置").arg(s.name) : s.name, m_list);
        item->setData(Qt::UserRole, s.id);
        if (s.id == selectId)
            selectRow = i;
    }
    m_list->setCurrentRow(selectRow);
    rebuildParamPanel();
}

void TemplateManagerDialog::rebuildParamPanel()
{
    const QString id = m_list->currentItem()
                           ? m_list->currentItem()->data(Qt::UserRole)
                                 .toString()
                           : QString();
    const TemplateSpec spec = TemplateCatalog::specFor(id);
    m_nameLabel->setText(spec.builtin
                             ? tr("%1（内置，只读）").arg(spec.name)
                             : spec.name);

    // 编辑初值：选中模板 = 当前照片模板时携带其已调参数，否则声明默认
    m_editValues = spec.defaultParams();
    const PhotoDocument *doc = m_project ? m_project->current() : nullptr;
    if (doc && doc->templateState.templateId == spec.id) {
        for (auto it = doc->templateState.templateParams.constBegin();
             it != doc->templateState.templateParams.constEnd(); ++it)
            m_editValues.insert(it.key(), it.value());
    }

    // 清空旧参数行（先取走 item 删 widget，再删布局本体）
    if (auto *old = m_paramHost->layout()) {
        while (old->count() > 0) {
            QLayoutItem *item = old->takeAt(0);
            delete item->widget();
            delete item;
        }
        delete old;
    }
    m_paramLayout = new QVBoxLayout(m_paramHost);
    m_paramLayout->setContentsMargins(0, 0, 0, 0);
    m_paramLayout->setSpacing(6);

    for (const TemplateParamDecl &p : spec.params) {
        auto *row = new QWidget(m_paramHost);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        lay->setSpacing(6);
        auto *caption = new QLabel(p.label, row);
        lay->addWidget(caption);
        auto *slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, kSliderMax);
        lay->addWidget(slider, 1);
        auto *value = new QLabel(row);
        value->setObjectName("panelCount");
        value->setMinimumWidth(48);
        value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        lay->addWidget(value);
        m_paramLayout->addWidget(row);

        const double v =
            m_editValues.value(p.key, p.defaultValue).toDouble();
        QSignalBlocker blocker(slider);
        slider->setValue(valueToSlider(p, v));
        value->setText(valueText(p, v));
        connect(slider, &QSlider::valueChanged, this,
                [this, p, value](int s) {
                    const double nv = sliderToValue(p, s);
                    m_editValues.insert(p.key, nv);
                    value->setText(valueText(p, nv));
                });
    }
    updateButtonStates();
}

void TemplateManagerDialog::applyToCurrentPhoto()
{
    if (!m_project || !m_list->currentItem())
        return;
    const QString id =
        m_list->currentItem()->data(Qt::UserRole).toString();
    // setTemplateId 会把参数重置为模板声明默认，随后覆写编辑值
    // （多次 setTemplateParam 由 250ms 防抖合并为一次渲染）
    m_project->setTemplateId(id);
    const TemplateSpec spec = TemplateCatalog::specFor(id);
    for (auto it = m_editValues.constBegin(); it != m_editValues.constEnd();
         ++it) {
        const TemplateParamDecl *p = spec.param(it.key());
        if (p && it.value().toDouble() != p->defaultValue)
            m_project->setTemplateParam(it.key(), it.value().toDouble());
    }
    accept();   // 应用后关闭，主画布即所见即所得
}

void TemplateManagerDialog::saveAsNew()
{
    if (!m_list->currentItem())
        return;
    const TemplateSpec current = TemplateCatalog::specFor(
        m_list->currentItem()->data(Qt::UserRole).toString());
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, tr("另存为用户模板"), tr("模板名称"), QLineEdit::Normal,
        tr("我的%1").arg(current.name), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    TemplateSpec spec = current;
    spec.id.clear();          // 另存必须生成新 id（内置 id 不可覆盖）
    spec.name = name.trimmed();
    spec.builtin = false;
    const QString newId = TemplateCatalog::saveUserTemplate(spec);
    if (newId.isEmpty()) {
        QMessageBox::warning(this, tr("模板管理"), tr("模板保存失败"));
        return;
    }
    emit templateListChanged();
    reloadList(newId);
}

void TemplateManagerDialog::saveEdits()
{
    if (!m_list->currentItem())
        return;
    const QString id =
        m_list->currentItem()->data(Qt::UserRole).toString();
    TemplateSpec spec = TemplateCatalog::specFor(id);
    if (spec.builtin)
        return;
    for (TemplateParamDecl &p : spec.params)
        p.defaultValue = m_editValues.value(p.key, p.defaultValue)
                             .toDouble();
    // saveUserTemplate 以 spec.id 覆盖同名文件（非空 id 不再生成）
    const QString saved = TemplateCatalog::saveUserTemplate(spec);
    if (saved.isEmpty()) {
        QMessageBox::warning(this, tr("模板管理"), tr("模板保存失败"));
        return;
    }
    emit templateListChanged();
    QMessageBox::information(this, tr("模板管理"), tr("模板已保存"));
}

void TemplateManagerDialog::importFromFile()
{
    // 宽容接受 *.json：用户可能直接分享 %APPDATA% 里的原文件
    const QString path = QFileDialog::getOpenFileName(
        this, tr("导入模板包"), QString(),
        tr("模板包 (*.ftpl *.json)"));
    if (path.isEmpty())
        return;
    QString error;
    const QString id = TemplateCatalog::importTemplate(path, &error);
    if (id.isEmpty()) {
        QMessageBox::warning(this, tr("导入模板包"),
                             error.isEmpty() ? tr("模板导入失败") : error);
        return;
    }
    emit templateListChanged();
    reloadList(id);   // 列表选中新导入项即视觉反馈，不额外弹窗
}

void TemplateManagerDialog::exportToFile()
{
    if (!m_list->currentItem())
        return;
    const TemplateSpec spec = TemplateCatalog::specFor(
        m_list->currentItem()->data(Qt::UserRole).toString());
    // 模板名直接做默认文件名：先清洗 Windows 非法字符
    QString base = spec.name;
    base.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|])")),
                 QStringLiteral("_"));
    if (base.trimmed().isEmpty())
        base = QStringLiteral("template");
    const QString path = QFileDialog::getSaveFileName(
        this, tr("导出模板包"), base + QLatin1String(".ftpl"),
        tr("模板包 (*.ftpl)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!TemplateCatalog::exportTemplate(spec, path, &error)) {
        QMessageBox::warning(this, tr("导出模板包"),
                             error.isEmpty() ? tr("模板导出失败") : error);
        return;
    }
    QMessageBox::information(this, tr("导出模板包"),
                             tr("模板已导出到：%1").arg(path));
}

void TemplateManagerDialog::removeSelected()
{
    if (!m_list->currentItem())
        return;
    const QString id =
        m_list->currentItem()->data(Qt::UserRole).toString();
    if (TemplateCatalog::specFor(id).builtin)
        return;
    const TemplateSpec spec = TemplateCatalog::specFor(id);
    const auto answer = QMessageBox::question(
        this, tr("删除模板"),
        tr("确定删除模板「%1」吗？已应用该模板的照片会回落到默认样式。")
            .arg(spec.name));
    if (answer != QMessageBox::Yes)
        return;
    TemplateCatalog::deleteUserTemplate(id);
    emit templateListChanged();
    reloadList({});
}

void TemplateManagerDialog::updateButtonStates()
{
    const QString id = m_list->currentItem()
                           ? m_list->currentItem()->data(Qt::UserRole)
                                 .toString()
                           : QString();
    // 内置只读：保存/删除/导出不可用（另存为/导入始终可用）
    const bool editable = !TemplateCatalog::specFor(id).builtin
                          && !id.isEmpty();
    m_saveButton->setEnabled(editable);
    m_deleteButton->setEnabled(editable);
    m_exportButton->setEnabled(editable);
}

} // namespace fxt
