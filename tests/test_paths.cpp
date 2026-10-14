// AppPaths 数据根路由测试（M6.1 便携版）：FrameXT.portable 标记决定
// 数据落点——便携 = <exeDir>/Data（随程序走），安装 = 平台标准位置
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QtTest>

#include "AppPaths.h"

using namespace fxt;

class TestPaths : public QObject
{
    Q_OBJECT

private slots:
    void noMarkerFallsBackToStandardLocation();
    void markerRoutesDataNextToExe();
};

// 安装模式（无标记）：回落 QStandardPaths 标准位置（Windows %APPDATA%）
void TestPaths::noMarkerFallsBackToStandardLocation()
{
    // 防御：清掉可能的残留标记（上一用例异常中断等）
    QFile::remove(QCoreApplication::applicationDirPath()
                  + QStringLiteral("/FrameXT.portable"));
    QCOMPARE(AppPaths::appDataRoot(),
             QStandardPaths::writableLocation(
                 QStandardPaths::AppLocalDataLocation));
}

// 便携模式：exe 同级放空标记文件（内容忽略）→ 数据根切到 <exeDir>/Data
void TestPaths::markerRoutesDataNextToExe()
{
    const QString marker = QCoreApplication::applicationDirPath()
                           + QStringLiteral("/FrameXT.portable");
    QFile markerFile(marker);
    QVERIFY(markerFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    markerFile.close();

    const QString root = AppPaths::appDataRoot();
    QCOMPARE(root, QCoreApplication::applicationDirPath()
                       + QStringLiteral("/Data"));
    QVERIFY(QDir(root).exists()); // 目录随之创建

    // 清理测试残留（此刻 Data 应为空目录）
    QFile::remove(marker);
    QVERIFY(QDir(root).rmdir(root));
}

QTEST_MAIN(TestPaths)
#include "test_paths.moc"
