#include "panels/ExportPanel.h"

#include <QComboBox>
#include <QDialog>
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

    auto *form = new QFormLayout;
    form->setHorizontalSpacing(8);
    form->setVerticalSpacing(6);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);

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
    form->addRow(tr("保存位置"), dirRow);

    // —— 文件命名 + 同名冲突（同行两组）——
    m_namingCombo = new QComboBox(this);
    m_namingCombo->addItem(tr("保留原名"), int(NamingMode::KeepOriginal));
    m_namingCombo->addItem(tr("原名+序号"), int(NamingMode::OriginalPlusIndex));
    m_namingCombo->addItem(tr("日期+原名"), int(NamingMode::DatePlusOriginal));
    m_namingCombo->addItem(tr("自定义…"), int(NamingMode::CustomPattern));
    m_conflictCombo = new QComboBox(this);
    m_conflictCombo->addItem(tr("自动重命名"), int(ConflictPolicy::AutoRename));
    m_conflictCombo->addItem(tr("覆盖"), int(ConflictPolicy::Overwrite));
    m_conflictCombo->addItem(tr("跳过"), int(ConflictPolicy::Skip));
    m_conflictCombo->addItem(tr("每个文件询问"), int(ConflictPolicy::AskEach));
    auto *namingRow = new QHBoxLayout;
    namingRow->setContentsMargins(0, 0, 0, 0);
    namingRow->setSpacing(6);
    namingRow->addWidget(m_namingCombo, 1);
    namingRow->addWidget(new QLabel(tr("同名冲突"), this));
    namingRow->addWidget(m_conflictCombo, 1);
    form->addRow(tr("文件命名"), namingRow);
    // 自定义命名：占位符说明（{name} {index} {date} {model}，§3.3）
    connect(m_namingCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
        if (index < 0
            || m_namingCombo->itemData(index).toInt()
                   != int(NamingMode::CustomPattern))
            return;
        QMessageBox::information(
            this, tr("自定义命名"),
            tr("可用占位符：\n{name} 原文件名  {index} 序号\n"
               "{date} 拍摄日期(yyyyMMdd)  {model} 相机型号"));
    });

    // —— 质量与格式 + 尺寸（同行两组）——
    m_qualityCombo = new QComboBox(this);
    m_qualityCombo->addItem(tr("视觉无损"), int(QualityPreset::VisualLossless));
    m_qualityCombo->addItem(tr("平衡"), int(QualityPreset::Balanced));
    m_qualityCombo->addItem(tr("自定义…"), int(QualityPreset::Custom));
    m_sizeCombo = new QComboBox(this);
    m_sizeCombo->addItem(tr("原始尺寸"), int(SizeMode::Original));
    m_sizeCombo->addItem(tr("长边 2048"), int(SizeMode::LongEdge));
    auto *qualityRow = new QHBoxLayout;
    qualityRow->setContentsMargins(0, 0, 0, 0);
    qualityRow->setSpacing(6);
    qualityRow->addWidget(m_qualityCombo, 1);
    qualityRow->addWidget(new QLabel(tr("尺寸"), this));
    qualityRow->addWidget(m_sizeCombo, 1);
    form->addRow(tr("质量与格式"), qualityRow);
    // 自定义画质：质量/子采样高级项（§3.3「自定义…」弹高级项；v1 简化
    // 为质量滑杆 + 子采样下拉，格式固定 JPEG——PNG 走视觉无损行外入口 M5）
    connect(m_qualityCombo, &QComboBox::currentIndexChanged, this,
            [this](int index) {
        if (index < 0
            || m_qualityCombo->itemData(index).toInt()
                   != int(QualityPreset::Custom))
            return;
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
        auto *ok = new QPushButton(tr("确定"), &dialog);
        ok->setDefault(true);
        connect(ok, &QPushButton::clicked, &dialog, &QDialog::accept);
        v->addWidget(ok);
        if (dialog.exec() == QDialog::Accepted) {
            m_customQuality = qualitySpin->value();
            m_customSubsampling =
                JpegSubsampling(subCombo->currentData().toInt());
        } else {
            // 取消自定义 → 回落平衡（不留悬空选择）
            selectByData(m_qualityCombo, int(QualityPreset::Balanced));
        }
    });

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

ExportOptions ExportPanel::currentOptions() const
{
    ExportOptions opt;
    opt.outputDir = QDir::fromNativeSeparators(m_dirEdit->text());
    opt.naming = NamingMode(m_namingCombo->currentData().toInt());
    opt.conflict = ConflictPolicy(m_conflictCombo->currentData().toInt());
    opt.preset = QualityPreset(m_qualityCombo->currentData().toInt());
    opt.sizeMode = SizeMode(m_sizeCombo->currentData().toInt());
    opt.customQuality = m_customQuality;
    opt.customSubsampling = m_customSubsampling;
    return opt;
}

void ExportPanel::setOptions(const ExportOptions &options)
{
    m_dirEdit->setText(QDir::toNativeSeparators(options.outputDir));
    selectByData(m_namingCombo, int(options.naming));
    selectByData(m_conflictCombo, int(options.conflict));
    selectByData(m_qualityCombo, int(options.preset));
    selectByData(m_sizeCombo, int(options.sizeMode));
    m_customQuality = options.customQuality;
    m_customSubsampling = options.customSubsampling;
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
