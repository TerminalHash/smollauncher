#include "include/platformpaths.h"

#include <QDir>
#include <QStandardPaths>

namespace {

#if defined(Q_OS_MACOS)
QString macApplicationSupport()
{
    return QDir::homePath() + QStringLiteral("/Library/Application Support");
}
#endif

} // namespace

namespace PlatformPaths {

QString configHome()
{
#if defined(Q_OS_MACOS)
    return macApplicationSupport();
#else
#  if defined(Q_OS_WIN)
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        return appData;
#  endif
    const QString path = QStandardPaths::writableLocation(
        QStandardPaths::GenericConfigLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.config")
                          : path;
#endif
}

QString dataHome()
{
#if defined(Q_OS_MACOS)
    return macApplicationSupport();
#else
#  if defined(Q_OS_WIN)
    const QString local = qEnvironmentVariable("LOCALAPPDATA");
    if (!local.isEmpty())
        return local;
#  endif
    const QString path = QStandardPaths::writableLocation(
        QStandardPaths::GenericDataLocation);
    return path.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share")
                          : path;
#endif
}

} // namespace PlatformPaths
