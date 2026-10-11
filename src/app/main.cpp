#include <QApplication>
#include <QFile>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QLibraryInfo>
#include <QSettings>
#include <QTranslator>

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

    // i18n（3.9）：源语言 = 简体中文（代码内 tr 源串），zh-TW/en 走 .qm；
    // 语言 key 持久化（3.10），切换后重启生效（TitleBar 菜单触发）
    QSettings settings;
    const QString language =
        settings.value(QStringLiteral("app/language"), QStringLiteral("zh-CN"))
            .toString();
    QTranslator translator;
    if (language == QLatin1String("zh-TW"))
        translator.load(QStringLiteral(":/i18n/app_zh_TW.qm"));
    else if (language == QLatin1String("en"))
        translator.load(QStringLiteral(":/i18n/app_en.qm"));
    if (!translator.isEmpty())
        app.installTranslator(&translator);
    // Qt 自带对话框文案（文件对话框等）同步翻译；
    // qtbase 磁盘文件名是下划线制（qtbase_zh_TW.qm），用 QLocale 匹配最稳
    QTranslator qtTranslator;
    if (language != QLatin1String("zh-CN")) {
        qtTranslator.load(QLocale(language), QStringLiteral("qtbase"),
                          QStringLiteral("_"),
                          QLibraryInfo::path(
                              QLibraryInfo::TranslationsPath));
    }
    if (!qtTranslator.isEmpty())
        app.installTranslator(&qtTranslator);

    // 浅色主题：全局 QSS（Token 对齐 04-UI设计.md §1 色板，全局圆角 5px）
    QFile qss(QStringLiteral(":/theme/theme-light.qss"));
    if (qss.open(QIODevice::ReadOnly | QIODevice::Text))
        app.setStyleSheet(QString::fromUtf8(qss.readAll()));

    // 全局字体：阿里巴巴普惠体 55 Regular（用户指令），随资源嵌入、
    // 无需系统安装；QSS 全局 font-family 同步引用（缺字回退微软雅黑）
    const int brandFontId = QFontDatabase::addApplicationFont(
        QStringLiteral(":/fonts/AlibabaPuHuiTi-3-55-Regular.ttf"));
    QString brandFamily = QStringLiteral("Alibaba PuHuiTi 3.0");
    if (brandFontId < 0) {
        // 嵌入失败（资源损坏等）回退系统字体，界面仍可用
        brandFamily = QStringLiteral("Microsoft YaHei UI");
    }
    QFont font(brandFamily, 9);
    font.setStyleStrategy(QFont::PreferAntialias);
    app.setFont(font);

    app.setWindowIcon(QIcon(QStringLiteral(":/icons/logo-1024px.png")));

    fxt::MainWindow win;
    win.show();

    return app.exec();
}
