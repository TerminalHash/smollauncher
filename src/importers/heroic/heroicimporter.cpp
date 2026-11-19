#include "heroicimporter.h"

#include "../../include/platformpaths.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QUrl>

#include <algorithm>

namespace {

QString heroicConfigDir()
{
    return PlatformPaths::configHome() + QStringLiteral("/heroic");
}

// Extracts Wine setup from GamesConfig/<appName>.json.
// isWine == true only when we can reconstruct a legendary launch line.
struct WineSetup
{
    QString wineBin;
    QString winePrefix;
    QString protonPath;                  // directory containing the proton script
    bool    isWine   = false;
    bool    isProton = false;
};

WineSetup parseGameConfig(const QString &appName)
{
    WineSetup setup;
    const QString path =
        heroicConfigDir() + "/GamesConfig/" + appName + ".json";

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return setup;

    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return setup;

    const QJsonObject root = doc.object();
    QJsonObject obj = root.value(appName).toObject();
    if (obj.isEmpty()) obj = root;

    const QString prefix = obj.value("winePrefix").toString();
    if (prefix.isEmpty()) return setup;

    QString bin, type;
    const QJsonValue wineVal = obj.value("wineVersion");
    if (wineVal.isString()) {
        bin  = wineVal.toString();
        type = QStringLiteral("wine");
    } else if (wineVal.isObject()) {
        const QJsonObject w = wineVal.toObject();
        bin  = w.value("bin").toString();
        type = w.value("type").toString();
    }

    if (!bin.isEmpty() && type == QLatin1String("wine")) {
        setup.wineBin    = bin;
        setup.winePrefix = prefix;
        setup.isWine     = true;
    } else if (!bin.isEmpty() && type == QLatin1String("proton")) {
        // PROTONPATH is the directory that contains the proton script.
        // wineVersion.bin points at the proton executable itself.
        setup.protonPath = QFileInfo(bin).absolutePath();
        setup.winePrefix = prefix;
        setup.isProton   = true;
    }
    return setup;
}

QString resolveHeroicIcon(const QString &appName, const QString &installPath)
{
    const QStringList candidates = {
        heroicConfigDir() + "/icons/" + appName + ".png",
        heroicConfigDir() + "/icons/" + appName + ".jpg",
        QDir::homePath() + "/.cache/heroic/icons/" + appName + ".png",
        installPath + "/icon.png",
        installPath + "/icon.ico",
        installPath + "/game.ico",
    };
    for (const QString &p : candidates)
        if (QFileInfo::exists(p))
            return p;
    return QString();
}

QList<HeroicGame> parseInstalledJson(const QString &jsonPath,
                                     const QString &runner)
{
    QList<HeroicGame> out;

    QFile f(jsonPath);
    if (!f.open(QIODevice::ReadOnly)) return out;

    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return out;

    const QJsonObject root = doc.object();

    auto addGame = [&](const QString &mapKey, const QJsonObject &g) {
        if (g.isEmpty()) return;

        // Current Heroic stores entries in an "installed" array where the id
        // is a field; older files used an object map keyed by app name.
        QString appName = g.value("appName").toString();
        if (appName.isEmpty())
            appName = g.value("app_name").toString();
        if (appName.isEmpty())
            appName = mapKey;
        if (appName.isEmpty()) return;

        const QString installPath = g.value("install_path").toString();
        if (installPath.isEmpty() || !QFileInfo(installPath).isDir())
            return;

        QString title = g.value("title").toString();
        if (title.isEmpty())
            title = g.value("app_name").toString();
        if (title.isEmpty())
            title = QFileInfo(installPath).fileName();
        if (title.isEmpty()) return;

        QString executable = g.value("executable").toString();
        if (executable.isEmpty())
            executable = g.value("executablePath").toString();
        if (executable.isEmpty())
            executable = g.value("exe").toString();

        HeroicGame game;
        game.runner      = runner;
        game.appName     = appName;
        game.title       = title;
        game.installPath = installPath;
        game.iconPath    = resolveHeroicIcon(appName, installPath);
        game.executablePath = executable.isEmpty()
            ? QString()
            : (QFileInfo(executable).isAbsolute()
                   ? executable
                   : QDir(installPath).filePath(executable));
        out.append(game);
    };

    const QJsonValue installed = root.value("installed");
    if (installed.isArray()) {
        const QJsonArray arr = installed.toArray();
        for (const QJsonValue &v : arr)
            if (v.isObject())
                addGame(QString(), v.toObject());
    } else if (installed.isObject()) {
        const QJsonObject obj = installed.toObject();
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it)
            addGame(it.key(), it.value().toObject());
    } else {
        for (auto it = root.constBegin(); it != root.constEnd(); ++it)
            addGame(it.key(), it.value().toObject());
    }

    return out;
}

// GOG's installed.json does not record the executable for older installers.
// goggame-<id>.info lists the play tasks and the primary FileTask is the exe.
QString findGogExecutable(const QString &installPath, const QString &appName)
{
    QDir dir(installPath);
    QString infoPath = dir.filePath(
        QStringLiteral("goggame-%1.info").arg(appName));
    if (!QFileInfo::exists(infoPath)) {
        const QStringList infos = dir.entryList(
            { QStringLiteral("goggame-*.info") }, QDir::Files);
        if (infos.isEmpty()) return QString();
        infoPath = dir.filePath(infos.first());
    }

    QFile f(infoPath);
    if (!f.open(QIODevice::ReadOnly)) return QString();

    QJsonParseError perr;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return QString();

    const QJsonArray tasks = doc.object().value("playTasks").toArray();
    QString firstExe;
    for (const QJsonValue &v : tasks) {
        const QJsonObject task = v.toObject();
        if (task.value("type").toString() != QLatin1String("FileTask"))
            continue;
        QString path = task.value("path").toString();
        if (path.isEmpty()) continue;
        if (!QFileInfo(path).isAbsolute())
            path = dir.filePath(path);
        if (!QFileInfo(path).isFile()) continue;

        if (task.value("isPrimary").toBool())
            return path;
        if (firstExe.isEmpty())
            firstExe = path;
    }
    return firstExe;
}

// Wine/umu-run run with the game directory as cwd, so pass only the path
// relative to it instead of duplicating the full path in the arguments.
QString launchArgumentFor(const HeroicGame &game, const QString &exePath)
{
    if (game.installPath.isEmpty())
        return exePath;
    const QString rel = QDir(game.installPath).relativeFilePath(exePath);
    return rel.startsWith(QLatin1String("..")) ? exePath : rel;
}

// Given a Windows game with WineSetup resolved and an .exe path, fill in
// launchBinary / launchArgs / env for direct launching. Returns false if
// neither wine nor umu-run is usable (caller falls back to Heroic URI).
bool resolveWindowsLaunch(const QString &exePath, const WineSetup &setup,
                          HeroicGame &game)
{
    if (exePath.isEmpty() || !QFileInfo(exePath).isFile())
        return false;

    // ── Proton → umu-run ─────────────────────────────────────────────
    if (setup.isProton && !setup.protonPath.isEmpty()) {
        if (QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty())
            return false;

        game.launchBinary = QStringLiteral("umu-run");
        game.launchArgs   = { launchArgumentFor(game, exePath) };
        game.env.insert(QStringLiteral("WINEPREFIX"), setup.winePrefix);
        game.env.insert(QStringLiteral("PROTONPATH"), setup.protonPath);
        return true;
    }

    // ── Plain Wine ───────────────────────────────────────────────────
    if (setup.isWine && !setup.wineBin.isEmpty()) {
        // Use Heroic's specific Wine build — a GOG prefix is created for a
        // precise Wine version, and the system wine may refuse the prefix.
        QString wineExe = setup.wineBin;
        if (!QFileInfo(wineExe).isAbsolute())
            wineExe = QStandardPaths::findExecutable(setup.wineBin);
        if (wineExe.isEmpty() || !QFileInfo(wineExe).isExecutable())
            return false;

        game.launchBinary = wineExe;
        game.launchArgs   = { launchArgumentFor(game, exePath) };
        game.env.insert(QStringLiteral("WINEPREFIX"), setup.winePrefix);
        return true;
    }

    return false;
}

} // namespace

namespace HeroicImporter {

bool isLegendaryAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("legendary")).isEmpty();
}

bool isHeroicAvailable()
{
    return !QStandardPaths::findExecutable(QStringLiteral("heroic")).isEmpty();
}

QList<HeroicGame> scanEpic()
{
    QList<HeroicGame> games = parseInstalledJson(
        heroicConfigDir() + "/legendaryConfig/legendary/installed.json",
        QStringLiteral("legendary"));

    const bool umuAvailable =
        !QStandardPaths::findExecutable(QStringLiteral("umu-run")).isEmpty();

    for (auto &g : games) {
        const WineSetup setup = parseGameConfig(g.appName);

        // ── Proton → umu-run ───────────────────────────────────────────
        if (setup.isProton && umuAvailable && !g.executablePath.isEmpty()) {
            g.launchBinary = QStringLiteral("umu-run");
            g.launchArgs   = { launchArgumentFor(g, g.executablePath) };
            g.env.insert(QStringLiteral("WINEPREFIX"), setup.winePrefix);
            g.env.insert(QStringLiteral("PROTONPATH"), setup.protonPath);
            // GAMEID is optional; omit it to avoid mismatching protonfixes
            // that expect a specific umu-database entry.
            continue;
        }

        // ── Plain Wine → legendary direct ──────────────────────────────
        if (setup.isWine) {
            g.launchBinary = QStringLiteral("legendary");
            g.launchArgs   = { QStringLiteral("launch"), g.appName,
                               QStringLiteral("--wine"), setup.wineBin,
                               QStringLiteral("--wine-prefix"), setup.winePrefix };
            continue;
        }

        // ── Fallback → Heroic URI ──────────────────────────────────────
        g.launchBinary = QStringLiteral("heroic");
        g.launchArgs   = { QStringLiteral(
            "heroic://launch?appName=%1&runner=legendary")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(g.appName))) };
        g.warning      = QCoreApplication::translate(
            "HeroicImporter", "Uses Heroic launcher (Proton config not found)");
    }

    std::sort(games.begin(), games.end(),
              [](const HeroicGame &a, const HeroicGame &b){
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return games;
}

QList<HeroicGame> scanGog()
{
    QList<HeroicGame> games = parseInstalledJson(
        heroicConfigDir() + "/gog_store/installed.json",
        QStringLiteral("gog"));

    for (auto &g : games) {
        // installed.json often leaves "executable" empty; recover it from
        // goggame-<id>.info so Wine/Proton entries can launch directly.
        if (g.executablePath.isEmpty())
            g.executablePath = findGogExecutable(g.installPath, g.appName);

        // ── 1. Native Linux — prefer the game's own start.sh ─────────────
        const QString startScript = g.installPath + "/start.sh";
        const QFileInfo scriptInfo(startScript);
        if (scriptInfo.isFile()) {
            g.isNative = true;
            g.launchBinary = scriptInfo.isExecutable()
                                 ? startScript
                                 : QStringLiteral("/bin/sh");
            if (!scriptInfo.isExecutable())
                g.launchArgs = { startScript };
            continue;
        }

        // ── 2. Windows game — wine / umu-run via GamesConfig ─────────────
        const WineSetup setup = parseGameConfig(g.appName);
        if (!g.executablePath.isEmpty() &&
            resolveWindowsLaunch(g.executablePath, setup, g)) {
            continue;   // direct launch, no Heroic needed
        }

        // ── 3. Fallback ─────────────────────────────────────────────────
        g.launchBinary = QStringLiteral("heroic");
        g.launchArgs   = { QStringLiteral(
            "heroic://launch?appName=%1&runner=gog")
            .arg(QString::fromLatin1(QUrl::toPercentEncoding(g.appName))) };
        g.warning = QCoreApplication::translate(
            "HeroicImporter",
            "Uses Heroic launcher (Windows executable or Wine config not found)");
    }

    std::sort(games.begin(), games.end(),
              [](const HeroicGame &a, const HeroicGame &b){
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return games;
}

} // namespace HeroicImporter