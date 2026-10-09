#include "AppInfo.h"

namespace fxt {

QString AppInfo::versionText()
{
    return QString::fromLatin1(kVersion);
}

} // namespace fxt
