#pragma once

#include <QFrame>
#include <QHash>
#include <QImage>
#include <QUuid>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QToolButton;

namespace fxt {

// 照片列表（开发计划 3.3 / 04-UI §3.7）：
// card 结构（标题栏在面板内部：标题 + 状态行 + 撤销/清空图标按钮）
// + 横向缩略图条（单选，缩略图等比居中）+ 右键菜单 + Del 移除。
// 用户指令：列表只允许单选；文件名不随缩略图显示（tooltip 提供）。
class PhotoListPanel : public QFrame
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

    QLabel *m_summary = nullptr;       // 标题右侧状态行（浅色非粗体）
    QToolButton *m_undoButton = nullptr;
    QToolButton *m_clearButton = nullptr;
    QListWidget *m_list = nullptr;
    QHash<QUuid, QListWidgetItem *> m_items;   // row 顺序即导入顺序
};

} // namespace fxt
