#include <QApplication>
#include <QFile>
#include <QFont>
#include <QIcon>

#include "AppInfo.h"
#include "MainWindow.h"

int main(int argc, char *argv[])
{
    // Qt6 默认启用 Per-Monitor V2 DPI 感知，自绘标题栏的命中测试依赖该模式
    QApplication app(argc, argv);
    QApplication::setApplicationName(fxt::AppInfo::kAppName);
    QApplication::setApplicationVersion(fxt::AppInfo::versionText());
    QApplication::setOrganizationName(fxt::AppInfo::kOrgName);
    QApplication::setOrganizationDomain(fxt::AppInfo::kOrgDomain);

    // 浅色主题：全局 QSS（Token 对齐 04-UI设计.md §1 色板，全局圆角 5px）
    QFile qss(QStringLiteral(":/theme/theme-light.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // 基础字体：中文环境用微软雅黑（正文 12px，04-UI设计.md §1.2）
    QFont font(QStringLiteral("Microsoft YaHei UI"), 9);
    app.setFont(font);

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo-1024px.png")));

    fxt::MainWindow win;
    win.show();

    return app.exec();
}
