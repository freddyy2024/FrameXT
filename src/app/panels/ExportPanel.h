#pragma once

#include <QWidget>

#include "export/ExportOptions.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QProgressBar;
class QPushButton;

namespace fxt {

// 导入导出面板的导出表单（04-UI设计.md §3.3，高 230 卡片区）：
//   保存位置 / 文件命名 / 同名冲突 / 质量与格式 / 尺寸 + 执行导出
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
    QLineEdit *m_dirEdit = nullptr;
    QComboBox *m_namingCombo = nullptr;
    QComboBox *m_conflictCombo = nullptr;
    QComboBox *m_qualityCombo = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QPushButton *m_exportButton = nullptr;
    // 「自定义…」画质高级项的暂存值（currentOptions 落盘用）
    int m_customQuality = 90;
    JpegSubsampling m_customSubsampling = JpegSubsampling::s420;

    // 进度区（§5.2：进行中替换按钮区）
    QWidget *m_progressRow = nullptr;
    QProgressBar *m_progress = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_cancelButton = nullptr;
};

} // namespace fxt
