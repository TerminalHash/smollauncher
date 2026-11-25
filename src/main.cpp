#include "include/mainwindow.h"
#include "include/debug.h"
#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    QApplication::setApplicationName("SmolLauncher");
    QApplication::setOrganizationName("org.th.SmolLauncher");
    QApplication::setApplicationVersion("1.0.2");

    Debug::log(QStringLiteral("startup: version=%1").arg(QApplication::applicationVersion()));

    MainWindow w;
    w.show();
    return a.exec();
}
