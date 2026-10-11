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
#include <QScrollBar>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>

#include "../Theme.h"

namespace fxt {

namespace {
// 列表项缩略图规格（用户指令：项宽 170，高度随列表区域自适应）
constexpr QSize kThumbSize(112, 84);
constexpr int kItemWidth = 170;
constexpr int kThumbPadding = 5;   // 照片与 item 边缘的内衬间距

// 缩略图绘制（用户指令：水平+垂直居中，长边等比缩放，四周 5px 间距）：
// QIcon::paint 自带 KeepAspectRatio + AlignCenter；文字清空（文件名走
// tooltip），背景/选中/hover 态按 QSS 绘制。
class ThumbDelegate : public QStyledItemDelegate
{
public:
    explicit ThumbDelegate(QObject *parent = nullptr)
        : QStyledItemDelegate(parent) {}

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override
    {
        // 不能走 QStyledItemDelegate::paint 再补画图像：它内部会再次
        // initStyleOption 从模型重建 icon/text/features——外层清空的
        // opt 会被覆盖，基类照画一遍 icon，自绘再画一遍 = 重影。
        // 正确做法：自行构造 option（清除 icon/text 与特征位）后直接
        // style->drawControl 只画背景/选中/hover，图像由本委托统一绘制。
        QStyleOptionViewItem opt = option;
        initStyleOption(&opt, index);
        const QIcon icon = opt.icon;
        opt.text.clear();
        opt.icon = QIcon();
        opt.features &= ~(QStyleOptionViewItem::HasDecoration
                          | QStyleOptionViewItem::HasDisplay);
        const QWidget *widget = opt.widget;
        QStyle *style = widget ? widget->style() : QApplication::style();
        style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, widget);
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

    // —— 标题栏：标题 + 状态行（浅色）+ 清空图标按钮 ——
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
    // 视口尺寸变化（含横向滚动条出现/消失挤压高度）→ 重标定 item 高度；
    // 面板 resizeEvent 捕获不到这一类变化，必须挂在 viewport 上
    m_list->viewport()->installEventFilter(this);
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
    // 默认选中第一张（用户指令：导入后即展示该照片的预览与属性）；
    // setCurrentItem 触发 currentItemChanged → currentRequested → 主窗口切换
    if (!m_list->currentItem())
        m_list->setCurrentItem(item);
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

// item 高度 100% 自适应列表区域（用户指令），宽度固定 kItemWidth。
// 注意：IconMode 下 QListView 的内容上下各留一份 spacing（实测 item rect
// 从 y=spacing 起、内容底部再加一份），必须减两份，否则内容恒比视口高
// 一个 spacing 而产生可滚动范围（用户反馈「列表还能垂直滚动」）；
// 额外 2px 为亚像素舍入余量。
int PhotoListPanel::currentItemHeight() const
{
    const int h = m_list->viewport()->height() - 2 * m_list->spacing() - 2;
    return h > kThumbSize.height() ? h : kThumbSize.height();
}

void PhotoListPanel::updateItemHeights()
{
    const QSize size(kItemWidth, currentItemHeight());
    // 幂等：高度变化会触发视口 resize，无变化时不再写回，避免来回震荡
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem *item = m_list->item(i);
        if (item->sizeHint() != size)
            item->setSizeHint(size);
    }
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
    // 视口尺寸变化 → 重标定 item 高度（排到下一拍等布局稳定）
    if (watched == m_list->viewport() && event->type() == QEvent::Resize) {
        QMetaObject::invokeMethod(this, &PhotoListPanel::updateItemHeights,
                                  Qt::QueuedConnection);
    }
    // 滚轮 → 横向滚动（用户指令：列表是单行横向布局，垂直滚轮在此无意义；
    // 一次滚轮 120 单位折半为 60px 步长，触摸板水平分量同样可用）
    if (watched == m_list->viewport() && event->type() == QEvent::Wheel) {
        auto *wheelEvent = static_cast<QWheelEvent *>(event);
        QScrollBar *hbar = m_list->horizontalScrollBar();
        if (hbar->maximum() > 0) {
            const QPoint delta = wheelEvent->angleDelta();
            const int step = delta.y() != 0 ? delta.y() : delta.x();
            hbar->setValue(hbar->value() - step / 2);
        }
        return true;
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
    // 未选中照片时不显示状态信息（用户指令）
    QListWidgetItem *current = m_list->currentItem();
    if (!current) {
        m_summary->clear();
        return;
    }
    m_summary->setText(tr("共 %1 张 · 当前第 %2 张（%3）")
                           .arg(total)
                           .arg(m_list->row(current) + 1)
                           .arg(current->data(Qt::UserRole + 1).toString()));
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
