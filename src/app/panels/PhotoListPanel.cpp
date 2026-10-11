#include "panels/PhotoListPanel.h"

#include "../GlyphButton.h"

#include <QApplication>
#include <QEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QListWidget>
#include <QMenu>
#include <QPainter>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>

#include "../Theme.h"

namespace fxt {

namespace {
// 列表项缩略图规格（用户指令：项宽 170，高度随列表区域自适应）
constexpr QSize kThumbSize(112, 84);
constexpr int kItemWidth = 170;
constexpr int kThumbPadding = 5;   // 照片与 item 边缘的内衬间距

// 缩略图绘制（用户指令：水平+垂直居中，长边等比缩放，四周 5px 间距）：
// QIcon::paint 自带 KeepAspectRatio + AlignCenter；文字清空（文件名走
// tooltip），背景/选中/hover 态仍由基类按 QSS 绘制。
class ThumbDelegate : public QStyledItemDelegate
{
public:
    explicit ThumbDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QIcon icon = opt.icon;
        opt.icon = QIcon();   // 关键：基类也会画 icon（旧尺寸、顶部对齐），
                              // 不清空会叠出「重影」——清空后由下方自绘接管
        opt.text.clear();
        QStyledItemDelegate::paint(painter, opt, index);   // 背景/选中/hover
        if (!icon.isNull())
            icon.paint(painter,
                       opt.rect.adjusted(kThumbPadding, kThumbPadding,
                                         -kThumbPadding, -kThumbPadding),
                       Qt::AlignCenter);
    }
};
} // namespace

PhotoListPanel::PhotoListPanel(QWidget *parent)
    : QFrame(parent)
{
    // card 结构与基础属性/导入导出面板一致（标题栏在面板内部）
    setObjectName(QStringLiteral("card"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // —— 标题栏：标题 + 状态行（浅色）+ 撤销/清空图标按钮 ——
    auto *header = new QWidget(this);
    header->setObjectName(QStringLiteral("panelHeader"));
    header->setFixedHeight(32);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(12, 0, 8, 0);
    auto *title = new QLabel(tr("照片列表"), header);
    title->setObjectName(QStringLiteral("panelTitle"));
    headerLayout->addWidget(title);
    m_summary = new QLabel(header);
    m_summary->setObjectName(QStringLiteral("panelCount"));
    headerLayout->addWidget(m_summary);
    headerLayout->addStretch();
    m_undoButton = createGlyphButton(header, QStringLiteral("toolGlyph"),
                                     0xE7A7,   // Undo
                                     tr("撤销上一步（Ctrl+Z）"));
    m_undoButton->setEnabled(false);
    headerLayout->addWidget(m_undoButton);
    m_clearButton = createGlyphButton(header, QStringLiteral("toolGlyph"),
                                      0xE74D,   // Delete
                                      tr("清空列表"));
    headerLayout->addWidget(m_clearButton);
    layout->addWidget(header);

    // —— 横向缩略图条 ——
    m_list = new QListWidget;
    m_list->setObjectName(QStringLiteral("photoThumbList"));
    m_list->setFlow(QListView::LeftToRight);
    m_list->setViewMode(QListView::IconMode);
    m_list->setResizeMode(QListView::Adjust);
    m_list->setWrapping(false);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    // 缩略图显示区：自绘 delegate 等比居中（QIcon::paint KeepAspectRatio+AlignCenter）
    m_list->setIconSize(kThumbSize);
    m_list->setItemDelegate(new ThumbDelegate(m_list));
    // 项间距（用户反馈：无间距时相邻缩略图的图像区连成一片，看似一张项里两张图）
    m_list->setSpacing(6);
    m_list->setDragEnabled(false);
    m_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_list->setContextMenuPolicy(Qt::CustomContextMenu);
    m_list->installEventFilter(this);   // Del 键拦截（eventFilter）
    // 空态提示
    m_list->setStyleSheet(QString());
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

    connect(m_undoButton, &QToolButton::clicked, this,
            &PhotoListPanel::undoRequested);
    connect(m_clearButton, &QToolButton::clicked, this,
            &PhotoListPanel::clearRequested);
}

void PhotoListPanel::addPhoto(const QUuid &id, const QString &fileName,
                              const QImage &thumbnail)
{
    // 缩略图不显示文件名（用户指令）：名字进 tooltip 与状态行数据
    auto *item = new QListWidgetItem(QIcon(QPixmap::fromImage(thumbnail)),
                                     QString());
    item->setData(Qt::UserRole, id);
    item->setData(Qt::UserRole + 1, fileName);
    item->setSizeHint(QSize(kItemWidth, currentItemHeight()));
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

void PhotoListPanel::setUndoEnabled(bool enabled)
{
    m_undoButton->setEnabled(enabled);
}

// item 高度 100% 自适应列表区域（用户指令），宽度固定 kItemWidth
int PhotoListPanel::currentItemHeight() const
{
    const int h = m_list->viewport()->height();
    return h > kThumbSize.height() ? h : kThumbSize.height();
}

void PhotoListPanel::updateItemHeights()
{
    const QSize size(kItemWidth, currentItemHeight());
    for (int i = 0; i < m_list->count(); ++i)
        m_list->item(i)->setSizeHint(size);
}

void PhotoListPanel::resizeEvent(QResizeEvent *event)
{
    QFrame::resizeEvent(event);
    // 排到下一拍：布局完成后再读列表视口高度，item 全量刷新
    QMetaObject::invokeMethod(this, &PhotoListPanel::updateItemHeights,
                              Qt::QueuedConnection);
}

bool PhotoListPanel::eventFilter(QObject *watched, QEvent *event)
{
    // Del = 从列表移除当前照片（§3.7：不删文件）
    if (watched == m_list && event->type() == QEvent::KeyPress) {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Delete) {
            if (m_list->currentItem()) {
                const QUuid id =
                    m_list->currentItem()->data(Qt::UserRole).toUuid();
                if (!id.isNull())
                    emit removeRequested({ id });
            }
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

void PhotoListPanel::updateSummary()
{
    const int total = m_items.size();
    if (total == 0) {
        m_summary->setText(tr("拖入照片或点击选择"));
        return;
    }
    const int currentRow = m_list->currentItem()
                               ? m_list->currentItem()->listWidget()->row(
                                     m_list->currentItem()) + 1
                               : 0;
    QString currentName;
    if (m_list->currentItem())
        currentName = m_list->currentItem()->data(Qt::UserRole + 1).toString();
    m_summary->setText(tr("共 %1 张 · 当前第 %2 张（%3）")
                           .arg(total)
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
        menu.addSeparator();
        menu.addAction(tr("从列表移除"), this, [this, id] {
            emit removeRequested({ id });
        });
    }
    if (!m_items.isEmpty()) {
        menu.addSeparator();
        menu.addAction(tr("清空列表"), this, &PhotoListPanel::clearRequested);
    }
    if (!menu.isEmpty())
        menu.exec(m_list->mapToGlobal(pos));
}

} // namespace fxt
