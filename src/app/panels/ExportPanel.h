#pragma once

#include <QWidget>

#include "export/ExportOptions.h"

class QComboBox;
class QCheckBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace fxt {

// 导入导出面板的导出表单（04-UI设计.md §3.3，高 230 卡片区）：
//   保存位置 / 文件命名（含自定义占位符）/ 同名冲突 / 质量与格式
//   （JPEG 画质预设 | PNG）/ 尺寸 / 元数据（去定位 · DPI）+ 执行导出
//   导出中：内联进度条 + n/N + 当前文件名 + 取消（§5.2）
// 仅做「控件 ⇄ ExportOptions 值」的双向映射与进度展示；
// 导出调度与冲突弹窗由 MainWindow 接线（本类不依赖 controller）。
class ExportPanel : public QWidget
{
    Q_OBJECT

public:
    explicit ExportPanel(QWidget *parent = nullptr);

    ExportOptions currentOptions() const;
    void setOptions(const ExportOptions &options);

    // 无照片空态禁用（§3.4：45% 透明不可交互）
    void setHasPhotos(bool has);

    void setExporting(bool running);
    void updateProgress(int done, int total, const QString &currentFile);

signals:
    void exportRequested(const fxt::ExportOptions &options);
    void cancelRequested();

private:
    // 「自定义…」画质 / 「自定义…」命名的高级项对话框。
    // 挂在 activated（仅用户点选）而非 currentIndexChanged——程序性
    // setCurrentIndex（setOptions 恢复预设）不得触发弹窗（评审 P1-3：
    // 启动恢复 Custom 预设在主窗构造期弹模态框）
    void showCustomQualityDialog();
    void showCustomNamingDialog();

    QLineEdit *m_dirEdit = nullptr;
    QComboBox *m_formatCombo = nullptr;
    QComboBox *m_namingCombo = nullptr;
    QComboBox *m_conflictCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QCheckBox *m_stripGpsCheck = nullptr;   // E2「去定位」隐私开关
    QComboBox *m_dpiCombo = nullptr;        // DPI（原图/300/350）
    QPushButton *m_exportButton = nullptr;
    // 「自定义…」画质高级项的暂存值（currentOptions 落盘用）
    int m_customQuality = 90;
    JpegSubsampling m_customSubsampling = JpegSubsampling::s420;
    // 「自定义…」命名占位符与序号配置（§3.3：位数/起始可配）
    QString m_customPattern = QStringLiteral("{name}");
    int m_indexStart = 1;
    int m_indexDigits = 4;

    // 进度区（§5.2：进行中替换按钮区）
    QWidget *m_progressRow = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace fxt
