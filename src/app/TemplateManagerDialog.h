#pragma once

#include <QDialog>
#include <QString>
#include <QVariantMap>

class QLabel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

namespace fxt {

class ProjectController;

// 模板管理器（M5.2，04-UI §6 的参数级实现）：内置模板只读可另存为，
// 用户模板（%APPDATA% templates/*.json，kind + 参数集）支持保存/删除/
// 应用到当前照片。元素行组编辑与内嵌预览缩略图挂账（需行组一等存储）。
class TemplateManagerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit TemplateManagerDialog(ProjectController *project,
                                   QWidget *parent = nullptr);

signals:
    void templateListChanged();   // 另存/删除后主窗口模板下拉需重建

private:
    void reloadList(const QString &selectId);
    void rebuildParamPanel();
    void applyToCurrentPhoto();
    void saveAsNew();
    void saveEdits();
    void removeSelected();
    void updateButtonStates();

    ProjectController *m_project = nullptr;
    QListWidget *m_list = nullptr;
    QLabel *m_nameLabel = nullptr;
    QWidget *m_paramHost = nullptr;
    QVBoxLayout *m_paramLayout = nullptr;
    QPushButton *m_saveButton = nullptr;     // 保存修改到模板（用户模板）
    QPushButton *m_deleteButton = nullptr;   // 删除所选（用户模板）
    QVariantMap m_editValues;   // 当前选中模板的编辑值（滑杆写、应用/保存读）
};

} // namespace fxt
