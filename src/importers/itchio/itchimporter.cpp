#include "itchimporter.h"

#include "../../include/debug.h"
#include "../../include/platformpaths.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QQueue>
#include <QRegularExpression>
#include <QVector>

#include <toml++/toml.hpp>

#include <algorithm>

namespace {

// Removes a named SQL connection only after every QSqlDatabase/QSqlQuery
// handle declared after it has been destroyed. Calling removeDatabase() while
// such handles are still alive logs a warning and leaks the connection.
struct SqlConnectionCloser
{
    QString name;
    ~SqlConnectionCloser() { QSqlDatabase::removeDatabase(name); }
};

QString deriveTitle(const QString &slug)
{
    QString t = slug;

    // Strip version suffix BEFORE the prettifying pass, so
    // "Goddesses'_Whim-0.4.7-pc" → "Goddesses' Whim"
    static const QRegularExpression suffix(
        R"(\s*[-_ ]?\s*v?\d+\.\d+(\.\d+)?[ -]?(pc|linux|mac|win)?$)",
        QRegularExpression::CaseInsensitiveOption);
    t.remove(suffix);

    t.replace('-', ' ');
    t.replace('_', ' ');

    bool cap = true;
    for (auto &c : t) {
        if (c.isSpace()) cap = true;
        else if (cap) { c = c.toUpper(); cap = false; }
    }
    return t.trimmed();
}

bool parseItchToml(ItchGame &game)
{
    const QString path = game.installPath + "/.itch.toml";
    if (!QFileInfo::exists(path)) return false;

    toml::table config;
    try {
        config = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error &) {
        return false;
    }

    auto *actions = config["actions"].as_array();
    if (!actions) return false;

    for (const auto &actionNode : *actions) {
        auto *action = actionNode.as_table();
        if (!action) continue;

        const QString platform = QString::fromStdString(
            (*action)["platform"].value_or(std::string{}));
        if (!platform.isEmpty() && platform != "linux")
            continue;

        const QString relPath = QString::fromStdString(
            (*action)["path"].value_or(std::string{}));
        if (relPath.isEmpty()) continue;

        const QString full = QDir(game.installPath).filePath(relPath);
        if (!QFileInfo::exists(full)) continue;

        game.executable = full;

        if (auto *args = (*action)["args"].as_array()) {
            QStringList list;
            for (const auto &a : *args) {
                if (auto s = a.value<std::string>())
                    list << QString::fromStdString(*s);
            }
            game.arguments = list;
        }
        return true;
    }
    return false;
}


QString itchConfigDir()
{
    return PlatformPaths::configHome() + QStringLiteral("/itch");
}


// Strips version suffixes, punctuation, lowercases. Used for fuzzy match.
QString normalizeForMatch(const QString &s)
{
    static const QRegularExpression versionSuffix(
        R"(\s*[-_ ]?\s*v?\d+\.\d+(\.\d+)?[ -]?(pc|linux|mac|win)?\s*$)",
        QRegularExpression::CaseInsensitiveOption);

    QString t = s;
    t.remove(versionSuffix);
    t = t.toLower();

    QString out;
    out.reserve(t.size());
    for (QChar c : t)
        if (c.isLetterOrNumber())
            out += c;
    return out;
}

// Levenshtein distance — small, no dependency.
int editDistance(const QString &a, const QString &b)
{
    const int m = a.size(), n = b.size();
    if (m == 0) return n;
    if (n == 0) return m;

    QVector<int> prev(n + 1), curr(n + 1);
    for (int j = 0; j <= n; ++j) prev[j] = j;

    for (int i = 1; i <= m; ++i) {
        curr[0] = i;
        for (int j = 1; j <= n; ++j) {
            const int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            curr[j] = std::min({ prev[j] + 1,
                                 curr[j-1] + 1,
                                 prev[j-1] + cost });
        }
        std::swap(prev, curr);
    }
    return prev[n];
}

// Extracts the first "1.2" or "1.2.3" appearing in a name. Empty if none.
QString versionFromName(const QString &name)
{
    static const QRegularExpression re(
        QStringLiteral(R"((\d+\.\d+(?:\.\d+)?))"));
    const auto m = re.match(name);
    return m.hasMatch() ? m.captured(1) : QString();
}

// Numeric comparison of version tuples: "1.10" > "1.9" (unlike string compare).
// Returns >0 if a is newer than b.
int compareVersions(const QString &a, const QString &b)
{
    const QStringList ap = a.split('.');
    const QStringList bp = b.split('.');
    const int n = std::max(ap.size(), bp.size());
    for (int i = 0; i < n; ++i) {
        const int av = i < ap.size() ? ap[i].toInt() : 0;
        const int bv = i < bp.size() ? bp[i].toInt() : 0;
        if (av != bv) return av - bv;
    }
    return 0;
}

// If `path` contains two or more subdirectories that are version variants
// of the same base name, return the newest one by numeric comparison.
// Returns empty otherwise.
QString newestVersionSubfolder(const QString &path)
{
    QDir dir(path);
    if (!dir.exists()) return {};

    // Group subdirs by the part of the name BEFORE their version number.
    QHash<QString, QList<QFileInfo>> byBase;

    for (const QFileInfo &fi : dir.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString version = versionFromName(fi.fileName());
        if (version.isEmpty()) continue;

        const int idx = fi.fileName().indexOf(version);
        if (idx <= 0) continue;

        const QString base = normalizeForMatch(fi.fileName().left(idx));
        if (base.isEmpty()) continue;

        byBase[base].append(fi);
    }

    for (auto it = byBase.constBegin(); it != byBase.constEnd(); ++it) {
        if (it.value().size() < 2) continue;   // need >=2 versions to be a pattern

        auto list = it.value();
        std::sort(list.begin(), list.end(),
                  [](const QFileInfo &a, const QFileInfo &b){
            return compareVersions(versionFromName(a.fileName()),
                                   versionFromName(b.fileName())) > 0;
        });
        return list.first().absoluteFilePath();
    }
    return {};
}

// Search for a sibling folder whose derived title matches `dbTitle`.
// Picks the newest by mtime when multiple match (versioned siblings).
QString findRenamedFolder(const QString &parentPath, const QString &dbTitle)
{
    const QString dbNorm = normalizeForMatch(dbTitle);
    if (dbNorm.size() < 4) return {};

    QDir parent(parentPath);
    if (!parent.exists()) return {};

    Debug::detail(QStringLiteral("findRenamedFolder: dbTitle=\"%1\" norm=\"%2\"")
        .arg(dbTitle, dbNorm));

    QList<QFileInfo> candidates;
    for (const QFileInfo &fi : parent.entryInfoList(
             QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        const QString derived = deriveTitle(fi.fileName());
        const QString norm    = normalizeForMatch(derived);
        const QString version = versionFromName(fi.fileName());

        const bool first3 = (norm.left(3) == dbNorm.left(3));
        const int  dist   = editDistance(norm, dbNorm);
        const bool pass   = first3 && dist <= 2;

        Debug::detail(QStringLiteral("  folder=\"%1\" derived=\"%2\" norm=\"%3\" version=\"%4\" dist=%5 first3=%6 %7")
            .arg(fi.fileName(), derived, norm,
                 version.isEmpty() ? QStringLiteral("(none)") : version)
            .arg(dist)
            .arg(first3)
            .arg(pass ? QStringLiteral("candidate") : QStringLiteral("skip")));

        if (pass)
            candidates << fi;
    }

    Debug::detail(QStringLiteral("  candidates=%1").arg(candidates.size()));

    if (candidates.isEmpty()) return {};

    std::sort(candidates.begin(), candidates.end(),
              [](const QFileInfo &a, const QFileInfo &b){
        const QString av = versionFromName(a.fileName());
        const QString bv = versionFromName(b.fileName());
        if (!av.isEmpty() && !bv.isEmpty()) {
            const int c = compareVersions(av, bv);
            if (c != 0) return c > 0;
        }
        if (av.isEmpty() != bv.isEmpty())
            return !av.isEmpty();
        return a.lastModified() > b.lastModified();
    });

    for (const auto &fi : candidates) {
        Debug::detail(QStringLiteral("  sorted=\"%1\" version=\"%2\"")
            .arg(fi.fileName(), versionFromName(fi.fileName())));
    }

    return candidates.first().absoluteFilePath();
}

QString detectIcon(const QString &installPath)
{
    const QStringList names = {
        "icon.png", "icon.svg",
        "logo.png", "logo.svg",
        "Icon.png", "Logo.png",
    };
    for (const QString &n : names) {
        const QString p = installPath + "/" + n;
        if (QFileInfo::exists(p)) return p;
    }
    return QString();
}

// Windows builds have no Linux executable. Look for the most plausible .exe
// (case insensitive) so the game can be launched through Wine, mirroring how
// Heroic treats Windows titles. Returns an empty string when none is found.
QString detectWindowsExecutable(const QString &installPath)
{
    // Pick the largest .exe in a directory, skipping uninstallers/helpers.
    auto bestInDir = [](const QDir &dir) -> QString {
        QFileInfo best;
        const auto files = dir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (fi.suffix().compare(QStringLiteral("exe"),
                                    Qt::CaseInsensitive) != 0)
                continue;
            const QString base = fi.completeBaseName().toLower();
            if (base.contains("unins") || base.contains("setup")
                || base.contains("crash") || base.contains("dxsetup"))
                continue;
            if (!best.exists() || fi.size() > best.size())
                best = fi;
        }
        return best.exists() ? best.absoluteFilePath() : QString();
    };

    const QStringList directDirs = {
        installPath,
        installPath + "/bin",
        installPath + "/game",
        installPath + "/windows",
        installPath + "/win",
    };

    for (const QString &dir : directDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const QString exe = bestInDir(d);
        if (!exe.isEmpty()) return exe;
    }

    QQueue<QDir> queue;
    queue.enqueue(QDir(installPath));
    int depth = 0;
    while (!queue.isEmpty() && depth < 3) {
        const int levelSize = queue.size();
        for (int i = 0; i < levelSize; ++i) {
            QDir cur = queue.dequeue();
            const QString exe = bestInDir(cur);
            if (!exe.isEmpty()) return exe;
            for (const QFileInfo &sub : cur.entryInfoList(
                     QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                queue.enqueue(QDir(sub.absoluteFilePath()));
            }
        }
        ++depth;
    }
    return {};
}


bool detectExecutable(ItchGame &game)
{
    if (parseItchToml(game)) return true;

    // If the install folder is really a container holding multiple
    // version subfolders of the same game, descend into the newest one
    // before the extension/depth scan. Fixes the case where a manual
    // version update left "Game - 0.4.5 PC", "Game-0.4.6-pc", "Game-0.4.7-pc"
    // side by side and alphabetical BFS picked the oldest.
    const QString newest = newestVersionSubfolder(game.installPath);
    if (!newest.isEmpty()) {
        Debug::detail(QStringLiteral("detectExecutable: newest version \"%1\" (was \"%2\")")
            .arg(newest, game.installPath));
        game.installPath = newest;
    }

    // ── Priority: direct directory scan ────────────────────────────────
    const QStringList directDirs = {
        game.installPath,
        game.installPath + "/bin",
        game.installPath + "/game",
        game.installPath + "/linux",
    };
    const QStringList exts = { "*.x86_64", "*.x86", "*.sh" };

    for (const QString &dir : directDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        for (const QString &glob : exts) {
            const auto files = d.entryInfoList({glob}, QDir::Files, QDir::Name);
            for (const auto &fi : files) {
                if (fi.isExecutable()) {
                    game.executable = fi.absoluteFilePath();
                    return true;
                }
            }
        }
    }

    // ── Recursive BFS up to depth 3 ────────────────────────────────────
    QQueue<QDir> queue;
    queue.enqueue(QDir(game.installPath));
    int depth = 0;

    while (!queue.isEmpty() && depth < 3) {
        const int levelSize = queue.size();
        for (int i = 0; i < levelSize; ++i) {
            QDir cur = queue.dequeue();
            for (const QString &glob : exts) {
                const auto files = cur.entryInfoList(
                    {glob}, QDir::Files | QDir::Executable, QDir::Name);
                if (!files.isEmpty()) {
                    game.executable = files.first().absoluteFilePath();
                    return true;
                }
            }
            for (const QFileInfo &sub : cur.entryInfoList(
                     QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
                queue.enqueue(QDir(sub.absoluteFilePath()));
            }
        }
        ++depth;
    }

    // ── Non-executable .sh fallback (existing) ────────────────────────
    for (const QString &dir : directDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        const auto files = d.entryInfoList({"*.sh"}, QDir::Files, QDir::Name);
        if (!files.isEmpty()) {
            game.executable = "/bin/sh";
            game.arguments  = QStringList{ files.first().absoluteFilePath() };
            game.warning    = QStringLiteral(
                "Non-executable script; running via /bin/sh");
            return true;
        }
    }

    // ── Last resort: any executable in direct dirs ────────────────────
    const QStringList skip = {
        "so", "dll", "a", "json", "png", "jpg", "jpeg", "gif",
        "svg", "txt", "md", "xml", "ttf", "zip", "gz", "log",
        "cfg", "ini", "toml", "pfx", "pdb",
        "exe", "bat", "cmd",
    };
    for (const QString &dir : directDirs) {
        QDir d(dir);
        if (!d.exists()) continue;
        for (const auto &fi : d.entryInfoList(QDir::Files, QDir::Name)) {
            if (skip.contains(fi.suffix().toLower())) continue;
            if (fi.isExecutable()) {
                game.executable = fi.absoluteFilePath();
                game.warning    = QStringLiteral(
                    "Executable detected by heuristic; verify it is correct");
                return true;
            }
        }
    }

    // ── Windows build ─────────────────────────────────────────────────
    // Keep the detected .exe; the import flow runs it through Wine.
    const QString windowsExe = detectWindowsExecutable(game.installPath);
    if (!windowsExe.isEmpty()) {
        game.windowsOnly = true;
        game.executable  = windowsExe;
        game.arguments.clear();
        if (game.warning.isEmpty())
            game.warning = QStringLiteral(
                "Windows build; launched through Wine");
        return true;
    }

    return false;
}

} // namespace

QList<ItchGame> ItchImporter::scanRoots(const QStringList &roots)
{
    QList<ItchGame> out;
    QSet<QString> seenSlugs;

    for (const QString &root : roots) {
        QDir d(root);
        if (!d.exists()) continue;

        const auto entries = d.entryInfoList(
            QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);

        for (const QFileInfo &fi : entries) {
            const QString slug = fi.fileName();
            if (seenSlugs.contains(slug)) continue;

            ItchGame game;
            // Detect sibling folders with the same base name (version-suffixed).
            static const QRegularExpression versionSuffix(
            R"(\s*[-_ ]?\s*v?\d+\.\d+(\.\d+)?[ -]?(pc|linux|win)?\s*$)",
            QRegularExpression::CaseInsensitiveOption);

            const QString base = slug;
            if (versionSuffix.match(base).hasMatch()) {
                game.warning = QStringLiteral(
                "Version-suffixed folder; another version may also be installed");
            }

            game.slug        = slug;
            game.installPath = fi.absoluteFilePath();
            game.title       = deriveTitle(slug);

            if (!detectExecutable(game)) continue;

            game.iconPath = detectIcon(game.installPath);
            out.append(game);
            seenSlugs.insert(slug);
        }
    }

    std::sort(out.begin(), out.end(),
              [](const ItchGame &a, const ItchGame &b){
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return out;
}

QString ItchImporter::databasePath()
{
    // itch keeps its butler database beside the config on every platform.
    return itchConfigDir() + QStringLiteral("/db/butler.db");
}

QList<ItchGame> ItchImporter::scanFromDatabase()
{
    QList<ItchGame> out;

    if (!QSqlDatabase::isDriverAvailable("QSQLITE")) {
        qWarning() << "QSQLITE driver unavailable. Available:"
                   << QSqlDatabase::drivers();
        return out;
    }

    const QString dbPath = databasePath();
    if (!QFileInfo::exists(dbPath)) {
        Debug::detail(QStringLiteral("itch DB not found: %1").arg(dbPath));
        return out;
    }

    QTemporaryDir tempDir;
    if (!tempDir.isValid()) return out;

    const QString tmpDb = tempDir.path() + "/butler.db";
    if (!QFile::copy(dbPath, tmpDb)) {
        qWarning() << "could not copy itch DB";
        return out;
    }
    for (const char *suffix : { "-wal", "-shm" }) {
        if (QFileInfo::exists(dbPath + suffix))
            QFile::copy(dbPath + suffix, tmpDb + suffix);
    }

    const QString conn = QStringLiteral("itch-caves");
    SqlConnectionCloser closer{conn};
    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", conn);
        db.setDatabaseName(tmpDb);
        if (!db.open()) {
            qWarning() << "itch DB open failed:" << db.lastError();
            return out;
        }

        // ── Introspect schema ───────────────────────────────────────────
        auto columnsOf = [&](const QString &table) -> QStringList {
            QStringList cols;
            QSqlQuery q(db);
            if (q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table)))
                while (q.next()) cols << q.value(1).toString();
            return cols;
        };

        const QStringList tables = [&]{
            QStringList t;
            QSqlQuery q(db);
            if (q.exec("SELECT name FROM sqlite_master WHERE type='table'"))
                while (q.next()) t << q.value(0).toString();
            return t;
        }();

        Debug::detail(QStringLiteral("itch tables: %1").arg(tables.join(QStringLiteral(", "))));

        const QStringList cavesCols = columnsOf("caves");
        const QStringList gamesCols = columnsOf("games");
        Debug::detail(QStringLiteral("caves cols: %1").arg(cavesCols.join(QStringLiteral(", "))));
        Debug::detail(QStringLiteral("games cols: %1").arg(gamesCols.join(QStringLiteral(", "))));

        if (cavesCols.isEmpty()) {
            qWarning() << "no caves table — cannot import from DB";
            db.close();
            return out;
        }

// --- Diagnostics: how many games at each stage? --------------------
{
    QSqlQuery c(db);
    if (c.exec("SELECT COUNT(*) FROM caves") && c.next())
        Debug::detail(QStringLiteral("DIAG total caves rows: %1").arg(c.value(0).toInt()));

    if (c.exec("SELECT COUNT(*) FROM caves WHERE game_id IS NOT NULL")
        && c.next())
        Debug::detail(QStringLiteral("DIAG caves with game_id: %1").arg(c.value(0).toInt()));

    if (c.exec("SELECT COUNT(*) FROM caves WHERE install_location_id IS NOT NULL")
        && c.next())
        Debug::detail(QStringLiteral("DIAG caves with install_location_id: %1").arg(c.value(0).toInt()));

    if (c.exec("SELECT COUNT(*) FROM install_locations") && c.next())
        Debug::detail(QStringLiteral("DIAG install_locations rows: %1").arg(c.value(0).toInt()));

    if (c.exec("SELECT id, path FROM install_locations")) {
        while (c.next())
            Debug::detail(QStringLiteral("DIAG location: %1=%2")
                .arg(c.value(0).toInt()).arg(c.value(1).toString()));
    }

    // Sample 5 rows so we can see the raw fields
    if (c.exec("SELECT caves.id, caves.game_id, caves.install_location_id, "
               "       caves.install_folder_name, caves.custom_install_folder "
               "FROM caves LIMIT 5")) {
        while (c.next()) {
            Debug::detail(QStringLiteral("DIAG cave row: id=%1 game_id=%2 loc_id=%3 folder=\"%4\" custom=\"%5\"")
                .arg(c.value(0).toInt())
                .arg(c.value(1).toInt())
                .arg(c.value(2).toInt())
                .arg(c.value(3).toString(), c.value(4).toString()));
        }
    }
}

// ── Introspect install_locations too ────────────────────────────────
const QStringList installLocCols = columnsOf("install_locations");
Debug::detail(QStringLiteral("install_locations cols: %1")
    .arg(installLocCols.join(QStringLiteral(", "))));

QString locPathCol;
for (const char *c : { "path", "install_path", "location", "dir" }) {
    if (installLocCols.contains(c)) {
        locPathCol = QString::fromLatin1(c);
        break;
    }
}

// ── Pick a title column (games.title in this schema) ────────────────
QString titleCol;
if (!gamesCols.isEmpty()) {
    for (const char *c : { "title", "name", "slug" }) {
        if (gamesCols.contains(c)) {
            titleCol = QString::fromLatin1(c);
            break;
        }
    }
}

// ── Decide what we can actually select ──────────────────────────────
const bool hasFolderName    = cavesCols.contains("install_folder_name");
const bool hasCustomFolder  = cavesCols.contains("custom_install_folder");
const bool hasLocJoin       = !locPathCol.isEmpty()
                           && cavesCols.contains("install_location_id");
const bool hasGameId        = cavesCols.contains("game_id");
const bool hasTitle         = !titleCol.isEmpty() && hasGameId;

if (!hasFolderName && !hasLocJoin) {
    qWarning() << "caves has no install_folder_name and no install_locations join"
               << "— cannot determine game paths";
    db.close();
    return out;
}

// ── Build the query ─────────────────────────────────────────────────
QStringList selectParts;
if (hasFolderName)   selectParts << QStringLiteral("caves.install_folder_name");
if (hasCustomFolder) selectParts << QStringLiteral("caves.custom_install_folder");
if (hasLocJoin)      selectParts << QStringLiteral("install_locations.%1").arg(locPathCol);
if (hasTitle)        selectParts << QStringLiteral("games.%1").arg(titleCol);

QString sql = QStringLiteral("SELECT ") + selectParts.join(", ")
            + QStringLiteral(" FROM caves");
if (hasTitle)
    sql += QStringLiteral(" LEFT JOIN games ON caves.game_id = games.id");
if (hasLocJoin)
    sql += QStringLiteral(" LEFT JOIN install_locations"
                          " ON caves.install_location_id = install_locations.id");

Debug::detail(QStringLiteral("itch sql: %1").arg(sql));

QSqlQuery q(db);
if (!q.exec(sql)) {
    qWarning() << "itch query failed:" << q.lastError()
               << "\nsql was:" << sql;
    db.close();
    return out;
}

// ── Row processing ──────────────────────────────────────────────────
QSet<QString> seen;
int rows = 0, skippedNoPath = 0, fuzzyMatched = 0, skippedNoExe = 0;

while (q.next()) {
    ++rows;

    QString folderName, customFolder, locPath, title;
    int col = 0;
    if (hasFolderName)   folderName   = q.value(col++).toString();
    if (hasCustomFolder) customFolder = q.value(col++).toString();
    if (hasLocJoin)      locPath      = q.value(col++).toString();
    if (hasTitle)        title        = q.value(col++).toString();

    const QString dbTitle = title.isEmpty() ? deriveTitle(folderName) : title;

    // ── Step 1: resolve path ────────────────────────────────────────
    QString path;

    if (path.isEmpty() && !locPath.isEmpty() && !folderName.isEmpty()) {
        const QString candidate = locPath + QLatin1Char('/') + folderName;
        if (QFileInfo(candidate).isDir()) {
            path = candidate;
            Debug::detail(QStringLiteral("STEP1 exact match: %1").arg(path));
        }
    }

    if (path.isEmpty() && !locPath.isEmpty() && !folderName.isEmpty()) {
        const QString candidate =
            locPath + QLatin1Char('/') + folderName;
        if (QFileInfo(candidate).isDir()) path = candidate;
    }

    QString warning;
// Step 1b — even if STEP1 succeeded, check for a newer versioned sibling.
// itch users commonly rename install folders with version suffixes, leaving
// the DB-named folder as an older copy. In that case prefer the newer one.
if (!locPath.isEmpty()) {
    const QString sibling = findRenamedFolder(locPath, dbTitle);
    if (!sibling.isEmpty()) {
        const QString siblingName = QFileInfo(sibling).fileName();

        if (siblingName != folderName) {
            const QString step1Version   = versionFromName(folderName);
            const QString siblingVersion = versionFromName(siblingName);

            bool preferSibling = false;

            if (step1Version.isEmpty()) {
                // DB folder has no version in its name — sibling with a
                // version is very likely the newer install.
                preferSibling = true;
            } else if (!siblingVersion.isEmpty()
                       && compareVersions(siblingVersion, step1Version) > 0) {
                // Both have versions — sibling is strictly newer.
                preferSibling = true;
            }

            if (preferSibling) {
                Debug::detail(QStringLiteral("STEP1b preferring newer sibling \"%1\" (v%2) over \"%3\"")
                    .arg(siblingName,
                         siblingVersion.isEmpty() ? QStringLiteral("?") : siblingVersion,
                         path));
                path = sibling;
                warning = QObject::tr(
                    "Newer versioned folder found on disk: %1")
                        .arg(siblingName);
            } else {
                Debug::detail(QStringLiteral("STEP1b keeping DB folder \"%1\" (sibling \"%2\" not newer)")
                    .arg(folderName, siblingName));
            }
        } else {
            Debug::detail(QStringLiteral("STEP1b no distinct sibling found for \"%1\"")
                .arg(dbTitle));
        }
    }
}

    // ── Step 2: fuzzy fallback — folder renamed on disk ─────────────
    if (path.isEmpty() && !locPath.isEmpty()) {
        Debug::detail(QStringLiteral("STEP2 fuzzy for \"%1\"").arg(dbTitle));
        const QString fuzzy = findRenamedFolder(locPath, dbTitle);
        if (!fuzzy.isEmpty()) {
            path = fuzzy;
            warning = QObject::tr(
                "Folder renamed on disk; matched by title");
            ++fuzzyMatched;
            Debug::detail(QStringLiteral("fuzzy matched \"%1\" -> \"%2\"")
                .arg(dbTitle, fuzzy));
        }
    }

    if (path.isEmpty()) {
        ++skippedNoPath;
        if (skippedNoPath <= 3) {
            Debug::detail(QStringLiteral("unresolved \"%1\" folder=\"%2\" loc=\"%3\"")
                .arg(dbTitle, folderName, locPath));
        }
        continue;
    }

    const QString slug = QFileInfo(path).fileName();
    if (seen.contains(slug)) continue;
    seen.insert(slug);

    ItchGame game;
    game.slug        = slug;
    game.installPath = path;
    game.title       = dbTitle;
    game.warning     = warning;

    if (!detectExecutable(game)) {
        ++skippedNoExe;
        if (skippedNoExe <= 3) {
            const QDir dir(path);
            const auto entries = dir.entryList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot,
                QDir::Name);
            Debug::detail(QStringLiteral("no executable found in \"%1\": %2")
                .arg(path, entries.mid(0, 12).join(QStringLiteral(", "))));
        }
        continue;
    }

    game.iconPath = detectIcon(game.installPath);
    out.append(game);
}

Debug::detail(QStringLiteral("itch rows processed=%1 fuzzyMatched=%2 skippedPath=%3 skippedNoExe=%4 kept=%5")
    .arg(rows).arg(fuzzyMatched).arg(skippedNoPath)
    .arg(skippedNoExe).arg(out.size()));

        db.close();
    }

    std::sort(out.begin(), out.end(),
              [](const ItchGame &a, const ItchGame &b){
        return a.title.localeAwareCompare(b.title) < 0;
    });
    return out;
}