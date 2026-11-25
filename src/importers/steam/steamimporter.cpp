#include "steamimporter.h"
#include "../../include/desktopentry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace {

// ─────────────────────────────────────────────────────────────────────────
// Valve KeyValues reader (libraryfolders.vdf, appmanifest_*.acf)
// ─────────────────────────────────────────────────────────────────────────
struct KVNode
{
    QString stringValue;
    QHash<QString, KVNode> children;
    bool isString = false;
    bool isBlock  = false;

    QString asString() const { return isString ? stringValue : QString(); }
};

class KVReader
{
public:
    explicit KVReader(const QString &text) : m_text(text) {}

    QHash<QString, KVNode> readRoot() { return readBlock(true, 0); }

private:
    static constexpr int kMaxDepth = 64;

    QHash<QString, KVNode> readBlock(bool topLevel, int depth)
    {
        QHash<QString, KVNode> result;
        // Malformed input can nest blocks without bound; stop before the
        // stack does. Valid Steam files are only a few levels deep.
        if (depth > kMaxDepth) return result;
        while (true) {
            skipWhitespaceAndComments();
            if (atEnd()) break;
            if (peek() == '}') {
                m_pos++;
                if (!topLevel) break;
                continue;   // tolerate stray } at top level
            }
            const QString key = readString();
            if (key.isEmpty()) break;

            skipWhitespaceAndComments();
            if (atEnd()) break;

            if (peek() == '{') {
                m_pos++;
                KVNode node;
                node.isBlock  = true;
                node.children = readBlock(false, depth + 1);
                result.insert(key, node);
            } else if (peek() == '"') {
                KVNode node;
                node.isString    = true;
                node.stringValue = readString();
                result.insert(key, node);
            } else {
                break;      // unexpected token
            }
        }
        return result;
    }

    void skipWhitespaceAndComments()
    {
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c.isSpace()) { m_pos++; continue; }
            if (c == '/' && m_pos + 1 < m_text.size()
                         && m_text.at(m_pos + 1) == '/') {
                while (m_pos < m_text.size() && m_text.at(m_pos) != '\n')
                    m_pos++;
                continue;
            }
            break;
        }
    }

    QChar peek() const
    {
        return m_pos < m_text.size() ? m_text.at(m_pos) : QChar();
    }
    bool atEnd() const { return m_pos >= m_text.size(); }

    QString readString()
    {
        if (atEnd() || peek() != '"') return QString();
        m_pos++;
        QString out;
        while (m_pos < m_text.size()) {
            const QChar c = m_text.at(m_pos);
            if (c == '\\' && m_pos + 1 < m_text.size()) {
                const QChar n = m_text.at(m_pos + 1);
                switch (n.unicode()) {
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case '\\': out += '\\'; break;
                case '"':  out += '"';  break;
                default:   out += n;    break;
                }
                m_pos += 2;
                continue;
            }
            if (c == '"') { m_pos++; return out; }
            out += c;
            m_pos++;
        }
        return out;
    }

    QString m_text;
    int     m_pos = 0;
};

// ─────────────────────────────────────────────────────────────────────────
// Steam path discovery
// ─────────────────────────────────────────────────────────────────────────
QStringList findSteamRoots()
{
    QStringList candidates;
#if defined(Q_OS_WIN)
    // Steam remembers its install path in the registry; fall back to the two
    // default Program Files locations (32-bit Steam is the usual one even on
    // 64-bit Windows).
    const QString regPath = QSettings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
        QSettings::NativeFormat).value(QStringLiteral("SteamPath")).toString();
    if (!regPath.isEmpty())
        candidates << QDir::fromNativeSeparators(regPath);
    for (const char *env : { "ProgramFiles(x86)", "ProgramFiles" }) {
        const QString base = qEnvironmentVariable(env);
        if (!base.isEmpty())
            candidates << base + QStringLiteral("/Steam");
    }
#elif defined(Q_OS_MACOS)
    candidates << QDir::homePath()
                      + QStringLiteral("/Library/Application Support/Steam");
#else
    candidates << QDir::homePath() + "/.local/share/Steam"
               << QDir::homePath() + "/.steam/steam";
#endif

    QStringList roots;
    for (const QString &p : candidates) {
        const QFileInfo fi(p);
        if ((fi.isDir() || fi.isSymLink()) && !roots.contains(p))
            roots << p;
    }
    return roots;
}

QStringList findLibraryPaths(const QString &steamRoot)
{
    QStringList libs;
    libs << steamRoot;   // the root itself is always the primary library

    QFile vdf(steamRoot + "/steamapps/libraryfolders.vdf");
    if (!vdf.open(QIODevice::ReadOnly))
        return libs;

    const auto root = KVReader(QString::fromUtf8(vdf.readAll())).readRoot();

    // "libraryfolders" block holds numbered entries, each with "path"
    auto topIt = root.constFind(QStringLiteral("libraryfolders"));
    if (topIt == root.constEnd() || !topIt->isBlock) return libs;

    for (auto it = topIt->children.constBegin();
         it != topIt->children.constEnd(); ++it) {
        if (!it.value().isBlock) continue;
        const QString path =
            it.value().children.value(QStringLiteral("path")).asString();
        if (!path.isEmpty() && !libs.contains(path))
            libs << path;
    }
    return libs;
}

QString resolveSteamIcon(const QString &steamRoot, const QString &appId)
{
    // ── 1. Steam's per-game .desktop file (most authoritative) ────────────
    const QString desktopPath = QDir::homePath()
        + "/.local/share/applications/steam_" + appId + ".desktop";
    if (QFileInfo::exists(desktopPath)) {
        QFile f(desktopPath);
        if (f.open(QIODevice::ReadOnly)) {
            bool inDesktopSection = false;
            while (!f.atEnd()) {
                const QByteArray raw = f.readLine().trimmed();
                if (raw.startsWith('[')) {
                    inDesktopSection = (raw == "[Desktop Entry]");
                    continue;
                }
                if (!inDesktopSection || !raw.startsWith("Icon="))
                    continue;

                const QString iconValue =
                    QString::fromUtf8(raw.mid(5)).trimmed();
                if (iconValue.isEmpty()) break;

                // Absolute path — use directly
                if (iconValue.startsWith('/')) {
                    if (QFileInfo::exists(iconValue))
                        return iconValue;
                } else {
                    // Icon name — resolve via the same resolver used by the
                    // .desktop importer (searches all themes in size priority
                    // order: scalable, 512, 256, 128, …, 16).
                    const QString resolved =
                        DesktopEntryScanner::resolveIconName(iconValue);
                    if (!resolved.isEmpty())
                        return resolved;
                }
                break;   // only inspect the first Icon= line
            }
        }
    }

    // ── 2. Direct hicolor scan (Steam names game icons steam_icon_<appid>) ─
    static const QStringList sizes = {
        "256x256", "128x128", "64x64",
        "48x48",   "32x32",   "24x24", "16x16",
    };
    static const QStringList exts = { "png", "svg", "xpm" };
    const QStringList iconNames = {
        "steam_icon_" + appId,   // Steam-generated for most games
        "steam_" + appId,        // alternate naming used by some titles
        appId,                   // a few native games install bare appid
    };
    for (const QString &size : sizes) {
        for (const QString &name : iconNames) {
            for (const QString &ext : exts) {
                const QString p = QDir::homePath()
                    + "/.local/share/icons/hicolor/" + size + "/apps/"
                    + name + "." + ext;
                if (QFileInfo::exists(p))
                    return p;
            }
        }
    }

    // ── 3. Fallback: Steam's library cache (wide banners) ─────────────────
    const QString cacheBase = steamRoot + "/appcache/librarycache/";
    const QString appFolder = cacheBase + appId + "/";
    const QStringList fallbacks = {
        appFolder + "logo.png",
        appFolder + "library_600x900.jpg",
        appFolder + "library_capsule.jpg",
        appFolder + "library_header.jpg",
        appFolder + "header.jpg",
        cacheBase + appId + "_library_600x900.jpg",
        cacheBase + appId + "_header.jpg",
    };
    for (const QString &p : fallbacks)
        if (QFileInfo::exists(p))
            return p;

    return QString();
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────
// Public API
// ─────────────────────────────────────────────────────────────────────────
bool SteamImporter::isSteamAvailable()
{
#if defined(Q_OS_MACOS)
    if (!QStandardPaths::findExecutable(QStringLiteral("steam")).isEmpty())
        return true;
    return QFileInfo::exists(QStringLiteral("/Applications/Steam.app"))
        || QFileInfo::exists(QDir::homePath()
                             + QStringLiteral("/Applications/Steam.app"));
#elif defined(Q_OS_WIN)
    const LaunchCommand cmd = launchCommand();
    if (QFileInfo(cmd.program).isAbsolute())
        return QFileInfo(cmd.program).isExecutable();
    return !QStandardPaths::findExecutable(cmd.program).isEmpty();
#else
    return !QStandardPaths::findExecutable(QStringLiteral("steam")).isEmpty();
#endif
}

SteamImporter::LaunchCommand SteamImporter::launchCommand()
{
#if defined(Q_OS_MACOS)
    const QString cli = QStandardPaths::findExecutable(QStringLiteral("steam"));
    if (!cli.isEmpty())
        return { cli, {} };
    // The Steam.app bundle registers the steam:// URL handler.
    return { QStringLiteral("/usr/bin/open"),
             { QStringLiteral("-a"), QStringLiteral("Steam") } };
#elif defined(Q_OS_WIN)
    const QString regPath = QSettings(
        QStringLiteral("HKEY_CURRENT_USER\\Software\\Valve\\Steam"),
        QSettings::NativeFormat).value(QStringLiteral("SteamPath")).toString();
    if (!regPath.isEmpty()) {
        const QString exe = QDir::fromNativeSeparators(regPath)
            + QStringLiteral("/steam.exe");
        if (QFileInfo::exists(exe))
            return { exe, {} };
    }
    return { QStringLiteral("steam"), {} };
#else
    return { QStringLiteral("steam"), {} };
#endif
}

QList<SteamGame> SteamImporter::scan()
{
    QList<SteamGame> out;

    const QStringList roots = findSteamRoots();
    if (roots.isEmpty()) return out;

    const QString primaryRoot = roots.first();   // used for icon cache only
    QSet<QString> seenAppIds;

    for (const QString &root : roots) {
        for (const QString &lib : findLibraryPaths(root)) {
            QDir steamapps(lib + "/steamapps");
            if (!steamapps.exists()) continue;

            const QStringList files = steamapps.entryList(
                { QStringLiteral("appmanifest_*.acf") }, QDir::Files);

            for (const QString &fileName : files) {
                // appmanifest_<id>.acf
                QString appId = fileName.mid(12);
                if (appId.endsWith(QLatin1String(".acf")))
                    appId.chop(4);
                if (appId.isEmpty() || seenAppIds.contains(appId))
                    continue;

                QFile f(steamapps.absoluteFilePath(fileName));
                if (!f.open(QIODevice::ReadOnly)) continue;

                const auto tree =
                    KVReader(QString::fromUtf8(f.readAll())).readRoot();
                auto stateIt = tree.constFind(QStringLiteral("AppState"));
                if (stateIt == tree.constEnd() || !stateIt->isBlock)
                    continue;
                const auto &state = stateIt->children;

                const QString name =
                    state.value(QStringLiteral("name")).asString();
                if (name.isEmpty()) continue;

                // Only fully-installed entries (StateFlags == 4)
                const QString flagsStr =
                    state.value(QStringLiteral("StateFlags")).asString();
                bool ok = false;
                const int flags = flagsStr.toInt(&ok);
                if (!ok || flags != 4) continue;

                SteamGame g;
                g.appId      = appId;
                g.name       = name;
                g.installDir = state.value(
                    QStringLiteral("installdir")).asString();
                g.iconPath   = resolveSteamIcon(primaryRoot, appId);

                out.append(g);
                seenAppIds.insert(appId);
            }
        }
    }

    std::sort(out.begin(), out.end(),
              [](const SteamGame &a, const SteamGame &b){
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return out;
}