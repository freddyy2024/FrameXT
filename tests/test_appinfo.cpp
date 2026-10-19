// AppInfo 元信息冒烟测试：校验应用名 / 版本契约（版本号需与根 CMakeLists.txt 同步维护）
#include <QChar>
#include <QStringList>
#include <QtTest>

#include "AppInfo.h"

using namespace fxt;

class TestAppInfo : public QObject
{
    Q_OBJECT

private slots:
    void appNameMatchesBrand();
    void versionIsSemantic();
    void orgIdentityConsistent();
};

void TestAppInfo::appNameMatchesBrand()
{
    QCOMPARE(AppInfo::kAppName, "FrameXT");
    QCOMPARE(AppInfo::versionText(), QStringLiteral("2.0.2"));
}

void TestAppInfo::versionIsSemantic()
{
    // 语义化版本：主.次.补丁，各段均为数字
    const QStringList parts = AppInfo::versionText().split(u'.');
    QCOMPARE(parts.size(), 3);
    for (const QString &part : parts) {
        QVERIFY(!part.isEmpty());
        for (const QChar c : part)
            QVERIFY2(c.isDigit(), qPrintable(QStringLiteral("版本段非法: %1").arg(part)));
    }
}

void TestAppInfo::orgIdentityConsistent()
{
    QCOMPARE(AppInfo::kOrgName, "FrameXT");
    QVERIFY(QString::fromLatin1(AppInfo::kOrgDomain) == QStringLiteral("framext.com"));
    QVERIFY(QString::fromLatin1(AppInfo::kHomePage).startsWith(QStringLiteral("https://")));
}

QTEST_MAIN(TestAppInfo)
#include "test_appinfo.moc"
