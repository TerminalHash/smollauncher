#ifndef APPITEM_H
#define APPITEM_H

#include <QStandardItem>
#include <QMap>
#include <QStringList>

class AppItem : public QStandardItem
{
public:
    explicit AppItem(const QString &name = QString())
        : QStandardItem(name), instanceId(nextInstanceId()) {}

    // Stable per-entry identity used for single-instance process bookkeeping.
    const quint64 instanceId;

    QString binaryPath;
    QString workDir;
    QStringList arguments;
    QString iconPath;
    QString desktopFileId;

    QMap<QString, QString> env;

    bool launchInTerminal    = false;
    bool launchWithDebugger  = false;
    bool singleInstance      = true;
    bool isScript            = false;

private:
    static quint64 nextInstanceId()
    {
        static quint64 id = 0;
        return ++id;
    }
};

#endif // APPITEM_H