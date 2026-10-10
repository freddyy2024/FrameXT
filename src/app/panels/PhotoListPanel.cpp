#include "panels/PhotoListPanel.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QVBoxLayout>

#include "../Theme.h"

namespace fxt {

namespace {
// 列表项缩略图规格（§3.7：项宽 120）
constexpr QSize kThumbSize(112, 84);
constexpr int kItemWidth = 120;
} // namespace

PhotoListPanel::PhotoListPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("photoListPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 8);
    layout->setSpacing(4);

    // —— 标题行 ——
    auto *header = new QHBoxLayout;
    m_summary = new QLabel(tr("照片列表"));
    m_summary->setObjectName(QStringLiteral("panelTitle"));
    header->addWidget(m_summary);
    header->addStretch();
    m_undoButton = new QPushButton(tr("撤销"));
    m_undoButton->setObjectName(QStringLiteral("secondaryButton"));
    m_undoButton->setEnabled(false);
    m_undoButton->setToolTip(tr("Ctrl+Z 可撤销"));
    header->addWidget(m_undoButton);
    m_clearButton = new QPushButton(tr("清空"));
    m_clearButton->setObjectName(QStringLiteral("secondaryButton"));
    header->addWidget(m_clearButton);
    layout->addLayout(header);

    // —— 横向缩略图条 ——
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("photoThumbList"));
    m_list->setFlow(QListView::LeftToRight);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWrapping(false);
    m_list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_list->setDragEnabled(false);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);   // Del 键拦截（eventFilter）
    // 空态提示
    m_list->setStyleSheet(QString());
    connect(m_list, &QListWidget::itemSelectionChanged, this, [this] {
        emit selectionRequested(selectedIds());
    });
    connect(m_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem *cur, QListWidgetItem *) {
                if (!cur)
                    return;
                const QUuid id = cur->data(Qt::UserRole).toUuid();
                if (!id.isNull())
                    emit currentRequested(id);
            });
    connect(m_list, &QWidget::customContextMenuRequested, this,
            &PhotoListPanel::showContextMenu);
    layout->addWidget(m_list, 1);

    connect(m_undoButton, &QPushButton::clicked, this,
            &PhotoListPanel::undoRequested);
    connect(m_clearButton, &QPushButton::clicked, this,
            &PhotoListPanel::clearRequested);
}

void PhotoListPanel::addPhoto(const QUuid &id, const QString &fileName,
                              const QImage &thumbnail)
{
    auto *item = new QListWidgetItem(
        QIcon(QPixmap::fromImage(thumbnail)), fileName);
    item->setData(Qt::UserRole, id);
    item->setSizeHint(QSize(kItemWidth, kThumbSize.height() + 20));
    item->setToolTip(fileName);
    m_list->addItem(item);
    m_items.insert(id, item);
    updateSummary();
}

void PhotoListPanel::removePhoto(const QUuid &id)
{
    QListWidgetItem *item = m_items.take(id);
    delete item;
    updateSummary();
}

void PhotoListPanel::clearPhotos()
{
    m_list->clear();
    m_items.clear();
    updateSummary();
}

void PhotoListPanel::setCurrent(const QUuid &id)
{
    QListWidgetItem *item = m_items.value(id);
    if (item && m_list->currentItem() != item) {
        QSignalBlocker blocker(m_list);   // 编程性设置不回发信号
        m_list->setCurrentItem(item);
    }
    updateSummary();
}

void PhotoListPanel::setSelection(const QSet<QUuid> &ids)
{
    QSignalBlocker blocker(m_list);
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        item->setSelected(ids.contains(item->data(Qt::UserRole).toUuid()));
    }
    updateSummary();
}

void PhotoListPanel::setUndoEnabled(bool enabled)
{
    m_undoButton->setEnabled(enabled);
}

void PhotoListPanel::selectAll()
{
    if (m_items.isEmpty())
        return;
    m_list->selectAll();   // ExtendedSelection 下全选 → itemSelectionChanged
                           // → selectionRequested 照常回发
}

bool PhotoListPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Del = 从列表移除（§3.7：不删文件）
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete) {
            const QSet<QUuid> doomed = selectedIds();
            if (!doomed.isEmpty())
                emit removeRequested(doomed.values());
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

QUuid PhotoListPanel::idAt(int row) const
{
    if (row < 0 || row >= m_list->count())
        return QUuid();
    return m_list->item(row)->data(Qt::UserRole).toUuid();
}

QSet<QUuid> PhotoListPanel::selectedIds() const
{
    QSet<QUuid> ids;
    for (QListWidgetItem *item : m_list->selectedItems())
        ids.insert(item->data(Qt::UserRole).toUuid());
    return ids;
}

void PhotoListPanel::updateSummary()
{
    const int total = m_items.size();
    const int selected = m_list->selectedItems().size();
    QString currentName;
    if (m_list->currentItem())
        currentName = m_list->currentItem()->text();
    if (total == 0) {
        m_summary->setText(tr("拖入照片或点击选择"));
        return;
    }
    const int currentRow = m_list->currentItem()
                               ? m_list->currentItem()->listWidget()->row(
                                     m_list->currentItem()) + 1
                               : 0;
    m_summary->setText(tr("共 %1 张 · 已选 %2 张 · 当前第 %3 张 / %4")
                           .arg(total)
                           .arg(selected)
                           .arg(currentRow)
                           .arg(currentName));
}

void PhotoListPanel::showContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_list->itemAt(pos);
    QMenu menu(this);
    if (item) {
        const QUuid id = item->data(Qt::UserRole).toUuid();
        menu.addAction(tr("设为当前"), this, [this, id] {
            emit currentRequested(id);
        });
        if (m_list->selectedItems().size() > 1) {
            menu.addAction(tr("应用当前设置到选中…"), this,
                           &PhotoListPanel::applyToSelectedRequested);
        }
        menu.addSeparator();
        menu.addAction(tr("从列表移除"), this, [this, id] {
            emit removeRequested({ id });
        });
        if (m_list->selectedItems().size() > 1) {
            menu.addAction(tr("移除未选中"), this,
                           &PhotoListPanel::removeUnselectedRequested);
        }
    }
    if (!m_items.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("清空列表"), this, &PhotoListPanel::clearRequested);
    }
    if (!menu.isEmpty())
        menu.exec(m_list->mapToGlobal(pos));
}

} // namespace fxt
