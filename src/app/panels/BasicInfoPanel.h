#pragma once

#include <QHash>
#include <QString>
#include <QWidget>

class QGridLayout;
class QLabel;

namespace fxt {

// 基础属性面板（04-UI §3.2）：label:value 两列只读展示，空字段 "—"，
// 标题栏 [⧉复制] 整表复制为 "字段: 值" 文本。
class BasicInfoPanel : public QWidget
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
    void setRow(const char *fieldKey, const QString &label,
                QLabel *valueLabel, const QHash<QString, QString> &fields,
                bool hasPhoto);
    void copyAll();

    QLabel *m_title = nullptr;
    QWidget *m_copyButton = nullptr;
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
