#include "include/debug.h"

#include "include/configpaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>

#include <cstdio>

namespace {

bool debugEnabled()
{
    static const bool on = []{
        const QByteArray v = qgetenv("SL_DEBUG").trimmed().toLower();
        return v == "1" || v == "true" || v == "yes";
    }();
    return on;
}

QString logFilePath()
{
    static const QString path = []{
        const QString dir = launcherConfigDir() + QStringLiteral("/Logs");
        QDir().mkpath(dir);
        return dir + QStringLiteral("/smollauncher.log");
    }();
    return path;
}

void writeLine(const QString &message)
{
    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        + QStringLiteral(" [%1] ").arg(QCoreApplication::applicationPid())
        + message;

    std::fprintf(stderr, "%s\n", qUtf8Printable(line));

    QFile f(logFilePath());
    if (f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
        f.write(line.toUtf8() + '\n');
}

} // namespace

namespace Debug {

bool enabled()
{
    return debugEnabled();
}

void log(const QString &message)
{
    writeLine(message);
}

void detail(const QString &message)
{
    if (!debugEnabled())
        return;
    writeLine(message);
}

} // namespace Debug
