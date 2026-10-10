#pragma once

#include <QHash>
#include <QImage>
#include <QUuid>
#include <QWidget>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace fxt {

// 照片列表（开发计划 3.3 / 04-UI §3.7）：
// 横向缩略图条（单选）+ 右键菜单 + Del 移除 + [↶撤销][清空]。
// 用户指令：列表只允许单选，不支持多选——选中态即当前态。
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
    void setUndoEnabled(bool enabled);
    QUuid idAt(int row) const;

signals:
    void currentRequested(const QUuid &id);
    void removeRequested(const QList<QUuid> &ids);
    void clearRequested();
    void undoRequested();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void updateSummary();
    void showContextMenu(const QPoint &pos);

    QLabel *m_summary = nullptr;
    QPushButton *m_undoButton = nullptr;
    QPushButton *m_clearButton = nullptr;
    QListWidget *m_list = nullptr;
    QHash<QUuid, QListWidgetItem *> m_items;   // row 顺序即导入顺序
};

} // namespace fxt
