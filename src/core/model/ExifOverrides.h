#pragma once

#include <QHash>
#include <QString>

namespace fxt {

// 字段覆盖层：UI 手改值 fieldKey→value；「还原」= 清空
// （对齐原版 setAndGetOriginShowInfo，02-技术设计 §4.5）
struct ExifOverrides
{
    QHash<QString, QString> values;

    bool isEmpty() const { return values.isEmpty(); }
    void clear() { values.clear(); }
    bool operator==(const ExifOverrides &) const = default;
};

} // namespace fxt
