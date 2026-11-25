#ifndef PRISMIMPORTER_H
#define PRISMIMPORTER_H

#include <QString>
#include <QList>

#include "../../include/launchcommand.h"

struct PrismInstance
{
    QString name;          // from instance.cfg
    QString uuid;          // launch identifier
    QString version;       // Minecraft version from mmc-pack.json
    QString instancePath;  // absolute path to the instance folder
    QString iconPath;      // resolved icon (may be empty)

    QString displayName() const
    {
        if (version.isEmpty())
            return name;
        return QStringLiteral("%1 (%2)").arg(name, version);
    }

    QString desktopFileId() const
    {
        return QStringLiteral("prism-") + uuid;
    }
};

namespace PrismImporter {

QString configFile();          // prismlauncher.cfg
QString instanceDirectory();   // InstanceDir, or the default next to the config
QList<PrismInstance> scan();

bool isAvailable();
LaunchCommand launchCommand();

} // namespace PrismImporter

#endif // PRISMIMPORTER_H
