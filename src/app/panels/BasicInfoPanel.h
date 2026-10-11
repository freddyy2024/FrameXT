#pragma once

#include <QFrame>
#include <QHash>
#include <QString>

class QGridLayout;
class QLabel;
class QStackedWidget;

namespace fxt {

// 基础属性面板（04-UI §3.2）：card 结构与导入导出面板一致
// （标题在面板内部 header，右侧 [⧉] 图标复制整表为 "字段: 值" 文本）。
// 空态（无照片）显示居中提示而非空参数表。
class BasicInfoPanel : public QFrame
{
    Q_OBJECT

public:
    explicit BasicInfoPanel(QWidget *parent = nullptr);

    // fields = PhotoMeta.exif.fields（含覆盖层时调用方已合并）
    void setFields(const QHash<QString, QString> &fields, bool hasPhoto);
    void clearFields();

private:
    QLabel *makeValueLabel();
    void buildRows();
    void copyAll();

    QStackedWidget *m_body = nullptr;   // page 0 = 空态提示 / page 1 = 参数表
    QGridLayout *m_grid = nullptr;
    struct Row
    {
        QString fieldKey;   // 空 = 无字段（静态行）
        QString label;      // 中文标签（复制用）
        QLabel *value = nullptr;
    };
    QList<Row> m_rows;
};

} // namespace fxt
