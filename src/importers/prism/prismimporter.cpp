#include "prismimporter.h"

#include "../../include/debug.h"
#include "../../include/platformpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>

namespace {

// instance.cfg is a plain INI file; values we need live in [General].
// Parsing it ourselves keeps names with non-ASCII characters intact and
// avoids QSettings quoting rules.
QHash<QString, QString> readIni(const QString &path)
{
    QHash<QString, QString> values;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return values;

    while (!f.atEnd()) {
        QString line = QString::fromUtf8(f.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))
            || line.startsWith(QLatin1Char(';'))
            || line.startsWith(QLatin1Char('[')))
            continue;

        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) continue;

        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && value.startsWith(QLatin1Char('"'))
            && value.endsWith(QLatin1Char('"')))
            value = value.mid(1, value.size() - 2);

        values.insert(line.left(eq).trimmed(), value);
    }
    return values;
}

QString minecraftVersion(const QString &instancePath)
{
    QFile f(instancePath + QStringLiteral("/mmc-pack.json"));
    if (!f.open(QIODevice::ReadOnly))
        return QString();

    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (!doc.isObject())
        return QString();

    const QJsonArray components = doc.object().value("components").toArray();
    for (const QJsonValue &value : components) {
        const QJsonObject component = value.toObject();
        if (component.value("cachedName").toString()
                .compare(QStringLiteral("Minecraft"), Qt::CaseInsensitive) != 0)
            continue;
        return component.value("cachedVersion").toString();
    }
    return QString();
}

QString instanceIcon(const QString &instancePath)
{
    // Prism stores the pack icon in minecraft/icon.png; a few instances
    // keep it at the instance root.
    const QString minecraftIcon =
        instancePath + QStringLiteral("/minecraft/icon.png");
    if (QFileInfo::exists(minecraftIcon))
        return minecraftIcon;

    const QString rootIcon = instancePath + QStringLiteral("/icon.png");
    if (QFileInfo::exists(rootIcon))
        return rootIcon;

    return QString();
}

} // namespace

QString PrismImporter::configFile()
{
#if defined(Q_OS_LINUX)
    const QString base = PlatformPaths::dataHome();
#else
    const QString base = PlatformPaths::configHome();
#endif
    return base + QStringLiteral("/PrismLauncher/prismlauncher.cfg");
}

bool PrismImporter::isAvailable()
{
#if defined(Q_OS_MACOS)
    if (!QStandardPaths::findExecutable(QStringLiteral("prismlauncher")).isEmpty())
        return true;
    return QFileInfo::exists(QStringLiteral("/Applications/Prism Launcher.app"))
        || QFileInfo::exists(QDir::homePath()
                             + QStringLiteral("/Applications/Prism Launcher.app"));
#elif defined(Q_OS_WIN)
    const LaunchCommand cmd = PrismImporter::launchCommand();
    if (QFileInfo(cmd.program).isAbsolute())
        return QFileInfo(cmd.program).isExecutable();
    return !QStandardPaths::findExecutable(QStringLiteral("prismlauncher")).isEmpty();
#else
    return !QStandardPaths::findExecutable(QStringLiteral("prismlauncher")).isEmpty();
#endif
}

LaunchCommand PrismImporter::launchCommand()
{
#if defined(Q_OS_MACOS)
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("prismlauncher"));
    if (!cli.isEmpty())
        return { cli, {} };
    return { QStringLiteral("/usr/bin/open"),
             { QStringLiteral("-a"), QStringLiteral("Prism Launcher"),
               QStringLiteral("--args") } };
#elif defined(Q_OS_WIN)
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("prismlauncher"));
    if (!cli.isEmpty())
        return { cli, {} };

    const QStringList candidates = {
        qEnvironmentVariable("LOCALAPPDATA")
            + QStringLiteral("/Programs/PrismLauncher/prismlauncher.exe"),
        qEnvironmentVariable("ProgramFiles")
            + QStringLiteral("/PrismLauncher/prismlauncher.exe"),
        qEnvironmentVariable("ProgramFiles(x86)")
            + QStringLiteral("/PrismLauncher/prismlauncher.exe"),
    };
    for (const QString &path : candidates) {
        if (!path.startsWith(QLatin1Char('/')) && QFileInfo::exists(path))
            return { path, {} };
    }
    return { QStringLiteral("prismlauncher"), {} };
#else
    return { QStringLiteral("prismlauncher"), {} };
#endif
}

QString PrismImporter::instanceDirectory()
{
    const QHash<QString, QString> config = readIni(configFile());
    const QString configured = config.value(QStringLiteral("InstanceDir"));
    if (!configured.isEmpty())
        return configured;

    return QFileInfo(configFile()).absolutePath()
        + QStringLiteral("/instances");
}

QList<PrismInstance> PrismImporter::scan()
{
    QList<PrismInstance> out;
    QSet<QString> seenUuids;

    QDir dir(instanceDirectory());
    if (!dir.exists()) {
        Debug::detail(QStringLiteral("prism: instance dir missing: \"%1\"")
            .arg(dir.absolutePath()));
        return out;
    }

    const QFileInfoList folders = dir.entryInfoList(
        QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

    for (const QFileInfo &folder : folders) {
        const QString cfgPath =
            folder.absoluteFilePath() + QStringLiteral("/instance.cfg");
        if (!QFileInfo::exists(cfgPath))
            continue;

        const QHash<QString, QString> cfg = readIni(cfgPath);
        const QString uuid = cfg.value(QStringLiteral("uuid")).trimmed();
        if (uuid.isEmpty())
            continue;                       // cannot be launched by Prism
        if (seenUuids.contains(uuid))
            continue;                       // duplicate config, ignore
        seenUuids.insert(uuid);

        PrismInstance inst;
        inst.uuid         = uuid;
        inst.instancePath = folder.absoluteFilePath();
        inst.name         = cfg.value(QStringLiteral("name")).trimmed();
        if (inst.name.isEmpty())
            inst.name = folder.fileName();
        inst.version  = minecraftVersion(inst.instancePath);
        inst.iconPath = instanceIcon(inst.instancePath);

        out.append(inst);
    }

    std::sort(out.begin(), out.end(),
              [](const PrismInstance &a, const PrismInstance &b){
        return a.displayName().compare(b.displayName(), Qt::CaseInsensitive) < 0;
    });

    Debug::log(QStringLiteral("prism: %1 instance(s) in \"%2\"")
        .arg(out.size()).arg(dir.absolutePath()));
    return out;
}
