#include "AppPaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace fxt::AppPaths {

// 不做进程级缓存：调用点（品牌覆盖层、用户模板目录）各自以 static 缓存
// 派生路径，此处保持无状态便于单测覆盖两个分支；探测成本可忽略。
QString appDataRoot()
{
    const QString exeDir = QCoreApplication::applicationDirPath();
    QString root;
    if (QFileInfo::exists(exeDir + QStringLiteral("/FrameXT.portable")))
        root = exeDir + QStringLiteral("/Data");
    else {
        root = QStandardPaths::writableLocation(
            QStandardPaths::AppLocalDataLocation);
    }
    // 目录幂等创建：QSaveFile 等写入方不建父目录，这里统一兜底
    QDir().mkpath(root);
    return root;
}

} // namespace fxt::AppPaths
