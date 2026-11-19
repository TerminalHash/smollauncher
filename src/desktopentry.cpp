#include "include/desktopentry.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QStandardPaths>

// ---------------------------------------------------------------------------
// icon resolver — plain directory scan, no QIcon::fromTheme involvement
// ---------------------------------------------------------------------------
QString DesktopEntryScanner::resolveIconName(const QString &name)
{
    if (name.isEmpty()) return {};
    if (name.startsWith('/'))
        return QFileInfo::exists(name) ? name : QString();

    static const QStringList themes = {
        "hicolor", "Adwaita", "breeze", "breeze-dark",
        "Papirus", "Papirus-Dark", "Yaru", "gnome",
        "elementary", "Arc"
    };
    static const QStringList sizes = {
        "scalable", "512x512", "256x256", "128x128",
        "64x64", "48x48", "32x32", "24x24", "16x16"
    };
    static const QStringList exts = { "svg", "png", "xpm" };

    // Short path: /usr/share/pixmaps
    for (const QString &ext : exts) {
        const QString p = "/usr/share/pixmaps/" + name + '.' + ext;
        if (QFileInfo::exists(p)) return p;
    }

    // Icon bases
    QStringList bases;
    bases << QDir::homePath() + "/.local/share/icons"
          << QDir::homePath() + "/.icons";
    for (const QString &d : QStandardPaths::standardLocations(
             QStandardPaths::GenericDataLocation)) {
        bases << (d.endsWith("/icons") ? d : d + "/icons");
    }
    bases << "/usr/share/icons" << "/usr/local/share/icons";

    bases.removeDuplicates();

    for (const QString &base : bases) {
        if (!QDir(base).exists()) continue;
        for (const QString &theme : themes) {
            for (const QString &size : sizes) {
                for (const QString &ext : exts) {
                    const QString p = QString("%1/%2/%3/apps/%4.%5")
                                          .arg(base, theme, size, name, ext);
                    if (QFileInfo::exists(p)) return p;
                }
            }
        }
    }
    return {};
}

// ---------------------------------------------------------------------------
// Exec= parser
// ---------------------------------------------------------------------------
namespace {

QString decodeValueEscapes(const QString &raw)
{
    QString out;
    out.reserve(raw.size());
    for (int i = 0; i < raw.size(); ++i) {
        if (raw.at(i) != '\\' || i + 1 >= raw.size()) {
            out += raw.at(i);
            continue;
        }
        const QChar n = raw.at(++i);
        switch (n.unicode()) {
        case 's':  out += QLatin1Char(' ');  break;
        case 'n':  out += QLatin1Char('\n'); break;
        case 't':  out += QLatin1Char('\t'); break;
        case 'r':  out += QLatin1Char('\r'); break;
        case '\\': out += QLatin1Char('\\'); break;
        default:   out += QLatin1Char('\\'); out += n; break;
        }
    }
    return out;
}

} // namespace

void DesktopEntryScanner::splitExec(const QString &raw,
                                    QString &program, QStringList &args)
{
    program.clear();
    args.clear();

    // %-codes. "%%" is a literal percent, so parse them in one pass: a simple
    // replace-then-remove would turn "%%f" into a (removed) %f field code.
    QString line;
    line.reserve(raw.size());
    static const QString fieldCodes = QStringLiteral("fFuUiIckKdDnNvVmM");
    for (int i = 0; i < raw.size(); ++i) {
        const QChar ch = raw.at(i);
        if (ch != QLatin1Char('%')) { line += ch; continue; }
        if (i + 1 < raw.size() && raw.at(i + 1) == QLatin1Char('%')) {
            line += QLatin1Char('%');
            ++i;
        } else if (i + 1 < raw.size() && fieldCodes.contains(raw.at(i + 1))) {
            ++i;    // drop the field code
        } else {
            line += ch;
        }
    }

    // Tokenize with double-quote and backslash-escape support.
    QString cur;
    bool inQuote = false;
    bool escaped = false;
    bool started = false;
    for (QChar ch : line) {
        if (escaped) {
            switch (ch.unicode()) {
            case 's':  cur += QLatin1Char(' ');  break;
            case 'n':  cur += QLatin1Char('\n'); break;
            case 't':  cur += QLatin1Char('\t'); break;
            case 'r':  cur += QLatin1Char('\r'); break;
            case '\\': cur += QLatin1Char('\\'); break;
            case '"':  cur += QLatin1Char('"');  break;
            case '`':  cur += QLatin1Char('`');  break;
            case '$':  cur += QLatin1Char('$');  break;
            default:   cur += ch;                break;
            }
            escaped = false;
            started = true;
            continue;
        }
        if (ch == '\\') { escaped = true; continue; }
        if (ch == '"') { inQuote = !inQuote; started = true; continue; }
        if (!inQuote && ch.isSpace()) {
            if (started) { args << cur; cur.clear(); started = false; }
        } else {
            cur += ch;
            started = true;
        }
    }
    if (started) args << cur;

    if (!args.isEmpty()) program = args.takeFirst();
}

// ---------------------------------------------------------------------------
// scanner
// ---------------------------------------------------------------------------
QList<DesktopEntry> DesktopEntryScanner::scan()
{
    QList<DesktopEntry> result;
    QSet<QString> seenIds;   // dedupe by filename (higher dir wins)

    // Search order (highest priority first)
    QStringList dirs;
    dirs << QDir::homePath() + "/.local/share/applications";
    for (const QString &d : QStandardPaths::standardLocations(
             QStandardPaths::ApplicationsLocation))
        dirs << d;
    dirs << "/usr/local/share/applications"
         << "/usr/share/applications";

    dirs.removeDuplicates();

    for (const QString &dirPath : dirs) {
        QDir dir(dirPath);
        if (!dir.exists()) continue;

        const auto files = dir.entryInfoList({ "*.desktop" },
                                             QDir::Files, QDir::Name);
        for (const QFileInfo &fi : files) {
            if (seenIds.contains(fi.fileName())) continue;
            seenIds.insert(fi.fileName());

            QFile f(fi.absoluteFilePath());
            if (!f.open(QIODevice::ReadOnly)) continue;

            bool inDesktopSection = false;
            QString name, comment, exec, tryExec, icon, path, type;
            bool terminal = false, noDisplay = false, hidden = false;

            while (!f.atEnd()) {
                QByteArray rawLine = f.readLine();
                // strip CR/LF
                while (rawLine.endsWith('\n') || rawLine.endsWith('\r'))
                    rawLine.chop(1);

                const QByteArray trimmed = rawLine.trimmed();
                if (trimmed.isEmpty() || trimmed.startsWith('#'))
                    continue;

                if (trimmed.startsWith('[')) {
                    inDesktopSection = (trimmed == "[Desktop Entry]");
                    continue;
                }
                if (!inDesktopSection) continue;

                const int eq = trimmed.indexOf('=');
                if (eq < 0) continue;

                const QByteArray key   = trimmed.left(eq).trimmed();
                const QByteArray value = trimmed.mid(eq + 1).trimmed();

                // Ignore localized keys like Name[ru] — they don't equal "Name"
                if      (key == "Type")      type     = QString::fromUtf8(value);
                else if (key == "Name")      name     = name.isEmpty()
                                                     ? decodeValueEscapes(QString::fromUtf8(value)) : name;
                else if (key == "Comment")   comment  = comment.isEmpty()
                                                     ? decodeValueEscapes(QString::fromUtf8(value)) : comment;
                else if (key == "Exec")      exec     = exec.isEmpty()
                                                     ? QString::fromUtf8(value) : exec;
                else if (key == "TryExec")   tryExec  = tryExec.isEmpty()
                                                     ? decodeValueEscapes(QString::fromUtf8(value)) : tryExec;
                else if (key == "Icon")      icon     = icon.isEmpty()
                                                     ? decodeValueEscapes(QString::fromUtf8(value)) : icon;
                else if (key == "Path")      path     = path.isEmpty()
                                                     ? decodeValueEscapes(QString::fromUtf8(value)) : path;
                else if (key == "Terminal")  terminal  = (value == "true");
                else if (key == "NoDisplay") noDisplay = (value == "true");
                else if (key == "Hidden")    hidden    = (value == "true");
            }

            if (type != "Application") continue;
            if (name.isEmpty() || exec.isEmpty()) continue;
            if (noDisplay || hidden) continue;

            // TryExec check — spec says the app is unavailable if it's not found
            if (!tryExec.isEmpty() &&
                QStandardPaths::findExecutable(tryExec).isEmpty())
                continue;

            DesktopEntry e;
            e.id              = fi.completeBaseName();
            e.desktopFilePath = fi.absoluteFilePath();
            e.name            = name;
            e.comment         = comment;
            e.exec            = exec;
            e.tryExec         = tryExec;
            e.icon            = icon;
            e.path            = path;
            e.terminal        = terminal;

            splitExec(e.exec, e.program, e.arguments);
            if (e.program.isEmpty()) continue;

            result.append(e);
        }
    }

    std::sort(result.begin(), result.end(),
              [](const DesktopEntry &a, const DesktopEntry &b){
        return a.name.localeAwareCompare(b.name) < 0;
    });
    return result;
}