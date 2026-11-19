#include "include/commandline.h"

#include <QStandardPaths>

namespace CommandLine {

QString shellQuote(const QString &s)
{
    if (s.isEmpty())
        return QStringLiteral("''");

    // Fast path: alphanumerics and common path chars go unquoted
    bool needsQuote = false;
    for (QChar c : s) {
        if (c.isLetterOrNumber() || c == '_' || c == '-' || c == '.' ||
            c == '/' || c == ':' || c == '=' || c == '@' || c == '%' ||
            c == '+' || c == ',') {
            continue;
        }
        needsQuote = true;
        break;
    }
    if (!needsQuote)
        return s;

    // Single-quote; escape embedded single quotes as '\''
    QString out;
    out.reserve(s.size() + 4);
    out += QLatin1Char('\'');
    for (QChar c : s) {
        if (c == '\'')
            out += QLatin1String("'\\''");
        else
            out += c;
    }
    out += QLatin1Char('\'');
    return out;
}

QStringList splitArgs(const QString &line)
{
    QStringList out;
    QString cur;
    QChar quote;
    bool started = false;

    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (!quote.isNull()) {
            if (c == quote) {
                quote = QChar();
            } else if (c == '\\' && quote == '"' && i + 1 < line.size()) {
                cur += line.at(++i);
            } else {
                cur += c;
            }
            continue;
        }

        if (c.isSpace()) {
            if (started) {
                out << cur;
                cur.clear();
                started = false;
            }
        } else if (c == '\'' || c == '"') {
            quote = c;
            started = true;
        } else if (c == '\\' && i + 1 < line.size()) {
            cur += line.at(++i);
            started = true;
        } else {
            cur += c;
            started = true;
        }
    }

    if (started)
        out << cur;
    return out;
}

QString joinArgs(const QStringList &args)
{
    QStringList quoted;
    quoted.reserve(args.size());
    for (const QString &a : args)
        quoted << shellQuote(a);
    return quoted.join(QLatin1Char(' '));
}

QString resolveCommandPath(const QString &path)
{
    if (path.isEmpty() || path.contains('/'))
        return path;
    return QStandardPaths::findExecutable(path);
}

} // namespace CommandLine
