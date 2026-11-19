/*!
 * \file main.cpp
 * \brief 星网转台数据解析与可视化软件入口。
 *
 * 技术栈：Qt 6.11.0 + QMake + QCustomPlot 2.1.1（C++17）。
 */

#include "ui/mainwindow.h"

#include <QApplication>
#include <QFileInfo>
#include <QFont>

#ifndef APP_VERSION
#define APP_VERSION "1.0.0" // 兜底；正常由 XingWang.pro 注入
#endif

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("星网转台数据解析与可视化软件"));
    QApplication::setApplicationVersion(QStringLiteral(APP_VERSION));
    QApplication::setOrganizationName(QStringLiteral("XingWang"));
    QApplication::setOrganizationDomain(QStringLiteral("xingwang.local"));

    QFont font = app.font();
    font.setFamily(QStringLiteral("Microsoft YaHei"));
    font.setPointSize(9);
    app.setFont(font);

    xn::MainWindow window;
    window.show();

    // 支持通过命令行直接打开数据文件：XingWangTurntable.exe data.txt
    const QStringList args = app.arguments();
    if (args.size() > 1) {
        const QString path = args.at(1);
        if (QFileInfo::exists(path))
            window.openPath(path);
    }

    return app.exec();
}
