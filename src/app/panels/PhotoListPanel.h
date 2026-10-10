#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QUuid>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace fxt {

// 照片列表（开发计划 3.3 / 04-UI §3.7）：
// 横向缩略图条 + 多选（Ctrl/Shift/全选）+ 当前/选中双层视觉 + 右键菜单
// + Del 移除 + [↶撤销][清空]。选中集（虚线角标）与当前（实线强调框）分层。
class PhotoListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit PhotoListPanel(QWidget *parent = nullptr);

    void addPhoto(const QUuid &id, const QString &fileName,
                  const QImage &thumbnail);
    void removePhoto(const QUuid &id);
    void clearPhotos();
    void setCurrent(const QUuid &id);          // 不发 currentRequested
    void setSelection(const QSet<QUuid> &ids); // 不发 selectionRequested
    void setUndoEnabled(bool enabled);
    QUuid idAt(int row) const;

signals:
    void currentRequested(const QUuid &id);
    void selectionRequested(const QSet<QUuid> &ids);
    void removeRequested(const QList<QUuid> &ids);
    void removeUnselectedRequested();
    void clearRequested();
    void undoRequested();
    void applyToSelectedRequested();

private:
    void updateSummary();
    void showContextMenu(const QPoint &pos);
    QSet<QUuid> selectedIds() const;

    QLabel *m_summary = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QListWidget *m_list = nullptr;
    QHash<QUuid, QListWidgetItem *> m_items;   // row 顺序即导入顺序
};

} // namespace fxt
