#include "include/debug.h"

#include "include/configpaths.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>

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

void rotateLogIfNeeded(const QString &path)
{
    // Rotate once per process at the first write, keeping one previous file.
    static bool checked = false;
    if (checked) return;
    checked = true;

    if (QFileInfo(path).size() < 1024 * 1024)
        return;

    const QString old = path + QStringLiteral(".1");
    QFile::remove(old);
    QFile::rename(path, old);
}

void writeLine(const QString &message)
{
    const QString path = logFilePath();
    rotateLogIfNeeded(path);

    const QString line = QDateTime::currentDateTime().toString(Qt::ISODateWithMs)
        + QStringLiteral(" [%1] ").arg(QCoreApplication::applicationPid())
        + message;

    std::fprintf(stderr, "%s\n", qUtf8Printable(line));

    QFile f(path);
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
