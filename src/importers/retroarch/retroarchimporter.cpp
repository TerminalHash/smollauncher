#include "retroarchimporter.h"

#include "../../include/platformpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

QString prettifyRomTitle(const QString &fileName)
{
    QString t = QFileInfo(fileName).completeBaseName();

    // Strip common ROM tags: (USA), [!], (Rev A), etc.
    static const QRegularExpression tags(
        R"(\s*[\(\[](?:[^\)\]]*)[\)\]])");
    t.remove(tags);

    t.replace('_', ' ');
    t.replace('-', ' ');
    t = t.simplified();

    bool cap = true;
    for (auto &c : t) {
        if (c.isSpace()) cap = true;
        else if (cap) { c = c.toUpper(); cap = false; }
    }
    return t.trimmed();
}

} // namespace

QString RetroArchImporter::playlistsDirectory()
{
#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    const QStringList candidates = {
        PlatformPaths::configHome() + QStringLiteral("/RetroArch/playlists"),
    };
#else
    const QStringList candidates = {
        QDir::homePath() + "/.config/retroarch/playlists",
        QDir::homePath() + "/.var/app/org.libretro.RetroArch/config/retroarch/playlists",
    };
#endif
    for (const QString &p : candidates)
        if (QFileInfo(p).isDir())
            return p;
    return candidates.first();
}

bool RetroArchImporter::isAvailable()
{
#if defined(Q_OS_MACOS)
    if (!QStandardPaths::findExecutable(QStringLiteral("retroarch")).isEmpty())
        return true;
    return QFileInfo::exists(QStringLiteral("/Applications/RetroArch.app"))
        || QFileInfo::exists(QDir::homePath()
                             + QStringLiteral("/Applications/RetroArch.app"));
#elif defined(Q_OS_WIN)
    const LaunchCommand cmd = RetroArchImporter::launchCommand();
    if (QFileInfo(cmd.program).isAbsolute())
        return QFileInfo(cmd.program).isExecutable();
    return !QStandardPaths::findExecutable(QStringLiteral("retroarch")).isEmpty();
#else
    return !QStandardPaths::findExecutable(QStringLiteral("retroarch")).isEmpty();
#endif
}

LaunchCommand RetroArchImporter::launchCommand()
{
#if defined(Q_OS_MACOS)
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("retroarch"));
    if (!cli.isEmpty())
        return { cli, {} };
    return { QStringLiteral("/usr/bin/open"),
             { QStringLiteral("-a"), QStringLiteral("RetroArch"),
               QStringLiteral("--args") } };
#elif defined(Q_OS_WIN)
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("retroarch"));
    if (!cli.isEmpty())
        return { cli, {} };

    const QStringList candidates = {
        qEnvironmentVariable("ProgramFiles")
            + QStringLiteral("/RetroArch/retroarch.exe"),
        qEnvironmentVariable("ProgramFiles(x86)")
            + QStringLiteral("/RetroArch/retroarch.exe"),
        qEnvironmentVariable("LOCALAPPDATA")
            + QStringLiteral("/RetroArch/retroarch.exe"),
        qEnvironmentVariable("APPDATA")
            + QStringLiteral("/RetroArch/retroarch.exe"),
    };
    for (const QString &path : candidates) {
        if (!path.startsWith(QLatin1Char('/')) && QFileInfo::exists(path))
            return { path, {} };
    }
    return { QStringLiteral("retroarch"), {} };
#else
    return { QStringLiteral("retroarch"), {} };
#endif
}

QList<RetroArchGame> RetroArchImporter::scan()
{
    QList<RetroArchGame> out;
    QSet<QString> seenRoms;   // dedup by absolute ROM path

    QDir dir(playlistsDirectory());
    if (!dir.exists()) return out;

    const auto files = dir.entryInfoList(
        { "*.lpl" }, QDir::Files, QDir::Name);

    for (const QFileInfo &fi : files) {
        QFile f(fi.absoluteFilePath());
        if (!f.open(QIODevice::ReadOnly)) continue;

        QJsonParseError perr;
        const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
        if (perr.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const QJsonObject playlist = doc.object();
        const QString playlistName =
            fi.completeBaseName();   // "Nintendo - SNES.lpl" → "Nintendo - SNES"
        const QString defaultCore =
            playlist.value("default_core_path").toString();
        const QString defaultCoreName =
            playlist.value("default_core_name").toString();

        const auto items = playlist.value("items").toArray();
        for (const auto &itemVal : items) {
            const auto item = itemVal.toObject();
            const QString romPath = item.value("path").toString();
            if (romPath.isEmpty()) continue;
            if (!QFileInfo::exists(romPath)) continue;
            if (seenRoms.contains(romPath)) continue;

            // Resolve core: item-level explicit path wins, else playlist default,
            // else "DETECT" (RetroArch auto-picks).
            QString corePath = item.value("core_path").toString();
            QString coreName = item.value("core_name").toString();

            const bool itemDetect =
                corePath.isEmpty()
                || corePath.compare("DETECT", Qt::CaseInsensitive) == 0;

            if (itemDetect) {
                corePath = defaultCore;
                if (coreName.isEmpty())
                    coreName = defaultCoreName;
            }

            RetroArchGame g;
            g.playlistName = playlistName;
            g.playlistPath = fi.absoluteFilePath();
            g.romPath      = romPath;
            g.romTitle     = prettifyRomTitle(romPath);
            g.corePath     = corePath;
            g.coreName     = coreName;

            if (corePath.isEmpty() || !QFileInfo::exists(corePath)) {
                g.warning = QStringLiteral(
                    "Core unresolved; RetroArch will auto-detect");
            }

            out.append(g);
            seenRoms.insert(romPath);
        }
    }

    std::sort(out.begin(), out.end(),
              [](const RetroArchGame &a, const RetroArchGame &b){
        // Group by playlist, then sort by title within group.
        if (a.playlistName != b.playlistName)
            return a.playlistName.localeAwareCompare(b.playlistName) < 0;
        return a.romTitle.localeAwareCompare(b.romTitle) < 0;
    });
    return out;
}