#include "panels/ExportPanel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace fxt {

namespace {
// 下拉 data() 存枚举 int，currentOptions/setOptions 双向映射的唯一事实源
int comboData(const QComboBox *combo) { return combo->currentData().toInt(); }

void selectByData(QComboBox *combo, int data)
{
    const int i = combo->findData(data);
    if (i >= 0)
        combo->setCurrentIndex(i);
}
} // namespace

ExportPanel::ExportPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("exportPanel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 8, 12, 10);
    layout->setSpacing(6);

    // 窄侧栏排版（用户指令：标签与控件同行过挤）——每个字段「标签在上、
    // 控件在下」，标签用淡色小字（#sectionLabel）
    auto *form = new QVBoxLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);
    auto addSection = [this, form](const QString &text) {
        auto *label = new QLabel(text, this);
        label->setObjectName(QStringLiteral("sectionLabel"));
        form->addSpacing(2);
        form->addWidget(label);
    };

    // —— 保存位置 ——
    m_dirEdit = new QLineEdit(this);
    m_dirEdit->setPlaceholderText(tr("选择导出目录…"));
    m_dirEdit->setReadOnly(true);
    auto *browse = new QPushButton(tr("浏览…"), this);
    browse->setObjectName("secondaryButton");
    connect(browse, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("选择导出目录"),
            m_dirEdit->text().isEmpty() ? QDir::homePath() : m_dirEdit->text());
        if (dir.isEmpty())
            return;
        m_dirEdit->setText(QDir::toNativeSeparators(dir));
    });
    auto *dirRow = new QHBoxLayout;
    dirRow->setContentsMargins(0, 0, 0, 0);
    dirRow->setSpacing(6);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(browse);
    addSection(tr("保存位置"));
    form->addLayout(dirRow);

    // —— 文件命名 + 同名冲突（同行两组）——
    m_namingCombo = new QComboBox(this);
    m_namingCombo->addItem(tr("保留原名"), int(NamingMode::KeepOriginal));
    m_namingCombo->addItem(tr("原名+序号"), int(NamingMode::OriginalPlusIndex));
    m_namingCombo->addItem(tr("日期+原名"), int(NamingMode::DatePlusOriginal));
    m_namingCombo->addItem(tr("自定义…"), int(NamingMode::CustomPattern));
    m_namingCombo->setToolTip(tr("原名+序号：DSC_2261-0001.jpg（同名照片各得连续序号）\n"
                                 "冲突重命名：DSC_2261(1).jpg（撞到已有文件时）"));
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItem(tr("自动重命名"), int(ConflictPolicy::AutoRename));
    m_conflictCombo->addItem(tr("覆盖"), int(ConflictPolicy::Overwrite));
    m_conflictCombo->addItem(tr("跳过"), int(ConflictPolicy::Skip));
    m_conflictCombo->addItem(tr("每个文件询问"), int(ConflictPolicy::AskEach));
    addSection(tr("文件命名"));
    form->addWidget(m_namingCombo);
    addSection(tr("同名冲突"));
    form->addWidget(m_conflictCombo);
    // 自定义命名：占位符输入 + 序号位数/起始（§3.3；activated 触发——
    // 仅用户点选，程序性 setCurrentIndex 不弹窗）
    connect(m_namingCombo, &QComboBox::activated, this,
            [this](int index) {
        if (index < 0)
            return;
        if (m_namingCombo->itemData(index).toInt()
            == int(NamingMode::CustomPattern))
            showCustomNamingDialog();
    });

    // —— 质量与格式 + 尺寸（同行两组）——
    m_formatCombo = new QComboBox(this);
    m_formatCombo->addItem(tr("JPEG"), int(OutputFormat::Jpeg));
    m_formatCombo->addItem(tr("PNG"), int(OutputFormat::Png));
    m_formatCombo->setToolTip(tr("PNG 为无损格式，画质项不适用（§3.3）"));
    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->addItem(tr("视觉无损"), int(QualityPreset::VisualLossless));
    m_qualityCombo->addItem(tr("平衡"), int(QualityPreset::Balanced));
    m_qualityCombo->addItem(tr("自定义…"), int(QualityPreset::Custom));
    // PNG 选中时禁用画质（§3.3：注「无损」）
    auto syncQualityEnabled = [this] {
        const bool png =
            OutputFormat(m_formatCombo->currentData().toInt())
                == OutputFormat::Png;
        m_qualityCombo->setEnabled(!png);
        m_qualityCombo->setToolTip(png ? tr("PNG 为无损格式")
                                       : QString());
    };
    connect(m_formatCombo, &QComboBox::currentIndexChanged, this,
            syncQualityEnabled);
    m_sizeCombo = new QComboBox(this);
    m_sizeCombo->addItem(tr("原始尺寸"), int(SizeMode::Original));
    m_sizeCombo->addItem(tr("长边 2048"), int(SizeMode::LongEdge));
    auto *qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->setSpacing(6);
    qualityRow->addWidget(m_formatCombo);
    qualityRow->addWidget(m_qualityCombo, 1);
    addSection(tr("质量与格式"));
    form->addLayout(qualityRow);
    addSection(tr("尺寸"));
    form->addWidget(m_sizeCombo);
    connect(m_qualityCombo, &QComboBox::activated, this,
            [this](int index) {
        if (index >= 0
            && m_qualityCombo->itemData(index).toInt()
                   == int(QualityPreset::Custom))
            showCustomQualityDialog();
    });

    // —— 元数据行（E2 导出保真的交互暴露：去定位 + DPI）——
    m_stripGpsCheck = new QCheckBox(tr("去定位（移除 GPS）"), this);
    m_stripGpsCheck->setToolTip(
        tr("从导出文件中摘除 GPS 定位信息（照片发布前推荐）"));
    m_dpiCombo = new QComboBox(this);
    m_dpiCombo->addItem(tr("保留原图"), int(Platform::ExportPolicy::DpiMode::Source));
    m_dpiCombo->addItem(tr("300"), 300);
    m_dpiCombo->addItem(tr("350"), 350);
    auto *metaRow = new QHBoxLayout;
    metaRow->setContentsMargins(0, 0, 0, 0);
    metaRow->setSpacing(6);
    metaRow->addWidget(m_stripGpsCheck, 1);
    metaRow->addWidget(new QLabel(tr("DPI"), this));
    metaRow->addWidget(m_dpiCombo);
    addSection(tr("元数据"));
    form->addLayout(metaRow);

    form->addStretch();
    layout->addLayout(form);

    // —— 执行导出（主按钮，右对齐 §3.3）——
    auto *buttonRow = new QHBoxLayout;
    buttonRow->setContentsMargins(0, 2, 0, 0);
    buttonRow->addStretch();
    m_exportButton = new QPushButton(tr("执行导出"), this);
    m_exportButton->setObjectName("primaryButton");
    connect(m_exportButton, &QPushButton::clicked, this,
            [this] { emit exportRequested(currentOptions()); });
    buttonRow->addWidget(m_exportButton);
    layout->addLayout(buttonRow);

    // —— 进度区（§5.2：进行中替换按钮区）——
    m_progressRow = new QWidget(this);
    m_progressRow->setVisible(false);
    auto *progressLayout = new QVBoxLayout(m_progressRow);
    progressLayout->setContentsMargins(0, 2, 0, 0);
    progressLayout->setSpacing(4);
    m_progress = new QProgressBar(m_progressRow);
    m_progress->setTextVisible(false);
    m_progress->setFixedHeight(6);
    m_progressLabel = new QLabel(m_progressRow);
    m_cancelButton = new QPushButton(tr("取消"), m_progressRow);
    m_cancelButton->setObjectName("secondaryButton");
    connect(m_cancelButton, &QPushButton::clicked, this,
            &ExportPanel::cancelRequested);
    auto *progressRow2 = new QHBoxLayout;
    progressRow2->setContentsMargins(0, 0, 0, 0);
    progressRow2->setSpacing(6);
    progressRow2->addWidget(m_progressLabel, 1);
    progressRow2->addWidget(m_cancelButton);
    progressLayout->addWidget(m_progress);
    progressLayout->addLayout(progressRow2);
    layout->addWidget(m_progressRow);
}

void ExportPanel::showCustomQualityDialog()
{
    // 记住进入前预设：取消时回设原值（评审 P1-5——静默把视觉无损
    // 降级为平衡属体验破坏），并补「取消」按钮（§4 对话框统一规格）
    const int previousIndex = m_qualityCombo->currentIndex();
    QDialog dialog(this);
    dialog.setWindowTitle(tr("自定义画质"));
    auto *v = new QVBoxLayout(&dialog);
    auto *qualitySpin = new QSpinBox(&dialog);
    qualitySpin->setRange(1, 100);
    qualitySpin->setValue(m_customQuality);
    auto *subCombo = new QComboBox(&dialog);
    subCombo->addItem(tr("4:4:4（最佳色彩）"), int(JpegSubsampling::s444));
    subCombo->addItem(tr("4:2:2"), int(JpegSubsampling::s422));
    subCombo->addItem(tr("4:2:0（最小体积）"), int(JpegSubsampling::s420));
    selectByData(subCombo, int(m_customSubsampling));
    v->addWidget(new QLabel(tr("JPEG 质量"), &dialog));
    v->addWidget(qualitySpin);
    v->addWidget(new QLabel(tr("色度子采样"), &dialog));
    v->addWidget(subCombo);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    v->addWidget(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        m_customQuality = qualitySpin->value();
        m_customSubsampling = JpegSubsampling(subCombo->currentData().toInt());
    } else {
        // 取消 → 回设进入前预设（无副作用语义）
        m_qualityCombo->blockSignals(true);
        m_qualityCombo->setCurrentIndex(previousIndex);
        m_qualityCombo->blockSignals(false);
    }
}

void ExportPanel::showCustomNamingDialog()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("自定义命名"));
    auto *form = new QFormLayout(&dialog);
    auto *patternEdit = new QLineEdit(m_customPattern, &dialog);
    form->addRow(tr("命名模板"), patternEdit);
    auto *hint = new QLabel(
        tr("占位符：{name} 原文件名 · {index} 序号\n"
           "{date} 拍摄日期(yyyyMMdd) · {model} 相机型号"), &dialog);
    hint->setStyleSheet(QStringLiteral("color: gray"));
    form->addRow(hint);
    auto *startSpin = new QSpinBox(&dialog);
    startSpin->setRange(0, 999999);
    startSpin->setValue(m_indexStart);
    form->addRow(tr("序号起始"), startSpin);
    auto *digitsSpin = new QSpinBox(&dialog);
    digitsSpin->setRange(1, 6);
    digitsSpin->setValue(m_indexDigits);
    form->addRow(tr("序号位数"), digitsSpin);
    auto *preview = new QLabel(&dialog);
    auto updatePreview = [=, &dialog] {
        // 实时示例（帮助理解占位符；无 controller 依赖）
        QString name = patternEdit->text();
        if (name.isEmpty())
            name = QStringLiteral("{name}");
        name.replace(QStringLiteral("{name}"), QStringLiteral("DSC_2261"));
        name.replace(QStringLiteral("{index}"),
                     QString::number(startSpin->value())
                         .rightJustified(digitsSpin->value(), QLatin1Char('0')));
        name.replace(QStringLiteral("{date}"), QStringLiteral("20261010"));
        name.replace(QStringLiteral("{model}"), QStringLiteral("OM-1"));
        preview->setText(tr("示例：%1.jpg").arg(name));
    };
    connect(patternEdit, &QLineEdit::textChanged, &dialog, updatePreview);
    connect(startSpin, &QSpinBox::valueChanged, &dialog, updatePreview);
    connect(digitsSpin, &QSpinBox::valueChanged, &dialog, updatePreview);
    updatePreview();
    form->addRow(preview);
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    form->addRow(buttons);
    if (dialog.exec() == QDialog::Accepted) {
        // 空 pattern 兜底（评审 A7：空模板会产出 ".jpg" 怪名）
        QString pattern = patternEdit->text().trimmed();
        if (pattern.isEmpty())
            pattern = QStringLiteral("{name}");
        m_customPattern = pattern;
        m_indexStart = startSpin->value();
        m_indexDigits = digitsSpin->value();
    } else {
        // 取消 → 回落保留原名（无副作用语义）
        m_namingCombo->blockSignals(true);
        selectByData(m_namingCombo, int(NamingMode::KeepOriginal));
        m_namingCombo->blockSignals(false);
    }
}

ExportOptions ExportPanel::currentOptions() const
{
    ExportOptions opt;
    opt.outputDir = QDir::fromNativeSeparators(m_dirEdit->text());
    opt.format = OutputFormat(m_formatCombo->currentData().toInt());
    opt.naming = NamingMode(m_namingCombo->currentData().toInt());
    opt.conflict = ConflictPolicy(m_conflictCombo->currentData().toInt());
    opt.preset = QualityPreset(m_qualityCombo->currentData().toInt());
    opt.sizeMode = SizeMode(m_sizeCombo->currentData().toInt());
    opt.customQuality = m_customQuality;
    opt.customSubsampling = m_customSubsampling;
    opt.customPattern = m_customPattern;
    opt.indexStart = m_indexStart;
    opt.indexDigits = m_indexDigits;
    // 元数据策略（E2 交互暴露）
    opt.meta.stripGps = m_stripGpsCheck->isChecked();
    const int dpi = m_dpiCombo->currentData().toInt();
    if (dpi > 0) {
        opt.meta.dpiMode = Platform::ExportPolicy::DpiMode::Fixed;
        opt.meta.fixedDpi = dpi;
    } else {
        opt.meta.dpiMode = Platform::ExportPolicy::DpiMode::Source;
    }
    return opt;
}

void ExportPanel::setOptions(const ExportOptions &options)
{
    m_dirEdit->setText(QDir::toNativeSeparators(options.outputDir));
    selectByData(m_formatCombo, int(options.format));
    selectByData(m_namingCombo, int(options.naming));
    selectByData(m_conflictCombo, int(options.conflict));
    selectByData(m_qualityCombo, int(options.preset));
    selectByData(m_sizeCombo, int(options.sizeMode));
    m_customQuality = options.customQuality;
    m_customSubsampling = options.customSubsampling;
    m_customPattern = options.customPattern;
    m_indexStart = options.indexStart;
    m_indexDigits = options.indexDigits;
    m_stripGpsCheck->setChecked(options.meta.stripGps);
    selectByData(m_dpiCombo, options.meta.dpiMode
                                 == Platform::ExportPolicy::DpiMode::Fixed
                                 ? options.meta.fixedDpi
                                 : 0);
}

void ExportPanel::setHasPhotos(bool has)
{
    m_exportButton->setEnabled(has);
    setToolTip(has ? QString() : tr("导入照片后可导出"));
}

void ExportPanel::setExporting(bool running)
{
    m_exportButton->setVisible(!running);
    m_progressRow->setVisible(running);
    if (running)
        m_progress->setValue(0);
}

void ExportPanel::updateProgress(int done, int total, const QString &currentFile)
{
    m_progress->setRange(0, qMax(1, total));
    m_progress->setValue(done);
    m_progressLabel->setText(
        tr("导出中: %1%  (%2/%3)  %4")
            .arg(total > 0 ? done * 100 / total : 0)
            .arg(done)
            .arg(total)
            .arg(currentFile));
}

} // namespace fxt
