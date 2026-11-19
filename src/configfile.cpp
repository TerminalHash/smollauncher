#include "include/configfile.h"

#include "include/appitem.h"
#include "include/commandline.h"
#include "include/launchermodel.h"

#include <QFile>
#include <QSaveFile>
#include <QStandardItemModel>
#include <QStringList>

#include <toml++/toml.hpp>

#include <algorithm>

namespace {

QString tomlString(const QString &s)
{
    // TOML strings may not contain raw control characters (tab is the one
    // exception). Values with such characters must use the escaped form,
    // otherwise the file we write cannot be read back.
    auto needsEscape = [](QChar c) {
        const ushort u = c.unicode();
        return u == '\'' || u == '\n' || u == '\r'
            || (u < 0x20 && u != '\t') || u == 0x7F;
    };

    bool literal = true;
    for (QChar c : s) {
        if (needsEscape(c)) { literal = false; break; }
    }

    // Single-line literal string when possible — no escaping needed.
    if (literal) {
        QString out;
        out.reserve(s.size() + 2);
        out += QLatin1Char('\'');
        out += s;
        out += QLatin1Char('\'');
        return out;
    }
    // Fall back to a double-quoted basic string with escapes.
    QString out;
    out.reserve(s.size() + 2);
    out += QLatin1Char('"');
    for (QChar c : s) {
        const ushort u = c.unicode();
        switch (u) {
        case '\\': out += QLatin1String("\\\\"); break;
        case '"':  out += QLatin1String("\\\""); break;
        case '\n': out += QLatin1String("\\n");  break;
        case '\r': out += QLatin1String("\\r");  break;
        case '\t': out += QLatin1String("\\t");  break;
        default:
            if ((u < 0x20 && u != '\t') || u == 0x7F)
                out += QStringLiteral("\\u%1").arg(u, 4, 16, QLatin1Char('0'));
            else
                out += c;
            break;
        }
    }
    out += QLatin1Char('"');
    return out;
}

QString tomlBool(bool b)
{
    return b ? QStringLiteral("true") : QStringLiteral("false");
}

QString tomlKey(const QString &k)
{
    // TOML bare keys are ASCII letters, digits, '_' and '-'. Everything else
    // (dots, spaces, non-ASCII) must be quoted.
    bool bare = !k.isEmpty();
    for (QChar c : k) {
        const ushort u = c.unicode();
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') ||
                        (u >= '0' && u <= '9') || u == '_' || u == '-';
        if (!ok) { bare = false; break; }
    }
    return bare ? k : tomlString(k);
}

} // namespace

namespace ConfigFile {

namespace {

bool knownKeys(const toml::table &tbl,
               std::initializer_list<std::string_view> keys)
{
    for (auto &&[k, v] : tbl) {
        (void)v;
        bool found = false;
        for (std::string_view key : keys) {
            if (k.str() == key) { found = true; break; }
        }
        if (!found) return false;
    }
    return true;
}

// A key that is present but has a different type would be silently replaced
// by value_or()'s default and then lost on the next save; reject it instead.
bool wrongType(const toml::table &tbl, std::string_view key,
               toml::node_type expected)
{
    const toml::node *node = tbl.get(key);
    return node && node->type() != expected;
}

} // namespace

ReadResult read(const QString &path)
{
    ReadResult result;

    toml::table config;
    try {
        config = toml::parse_file(path.toStdString());
    } catch (const toml::parse_error &e) {
        const auto &src = e.source();
        result.error = ReadError::ParseError;
        result.errorPath = src.path ? QString::fromStdString(*src.path) : path;
        result.errorLine = static_cast<int>(src.begin.line);
        const std::string_view desc = e.description();
        result.errorDescription = QString::fromUtf8(desc.data(), int(desc.size()));
        return result;
    }

    auto *cats = config["categories"].as_array();
    if (!cats) {
        result.error = ReadError::MissingCategories;
        return result;
    }

    for (auto &&[k, v] : config) {
        (void)v;
        if (k.str() != "categories") {
            result.error = ReadError::UnsupportedTopLevelKey;
            result.unsupportedKey = QString::fromStdString(std::string(k.str()));
            return result;
        }
    }

    for (const auto &catNode : *cats) {
        auto *catTbl = catNode.as_table();
        if (!catTbl ||
            !knownKeys(*catTbl, {"name", "icon", "collapsed", "items"})) {
            result.error = ReadError::UnsupportedStructure;
            return result;
        }
        if ((*catTbl).contains("items") && !(*catTbl)["items"].is_array()) {
            result.error = ReadError::UnsupportedStructure;
            return result;
        }
        if (wrongType(*catTbl, "name", toml::node_type::string) ||
            wrongType(*catTbl, "icon", toml::node_type::string) ||
            wrongType(*catTbl, "collapsed", toml::node_type::boolean)) {
            result.error = ReadError::UnsupportedStructure;
            return result;
        }

        Category category;
        category.name = QString::fromStdString(
            (*catTbl)["name"].value_or(std::string{"Category"}));
        category.iconPath = QString::fromStdString(
            (*catTbl)["icon"].value_or(std::string{}));
        category.collapsed = (*catTbl)["collapsed"].value_or(false);

        if (auto *items = (*catTbl)["items"].as_array()) {
            for (const auto &itemNode : *items) {
                auto *itemTbl = itemNode.as_table();
                if (!itemTbl ||
                    !knownKeys(*itemTbl,
                               {"separator", "name", "binary", "arguments",
                                "workDir", "icon", "desktopFileId",
                                "launchInTerminal", "launchWithDebugger",
                                "singleInstance", "isScript", "env"})) {
                    result.error = ReadError::UnsupportedStructure;
                    return result;
                }
                if (wrongType(*itemTbl, "separator", toml::node_type::boolean) ||
                    wrongType(*itemTbl, "name", toml::node_type::string) ||
                    wrongType(*itemTbl, "binary", toml::node_type::string) ||
                    wrongType(*itemTbl, "workDir", toml::node_type::string) ||
                    wrongType(*itemTbl, "icon", toml::node_type::string) ||
                    wrongType(*itemTbl, "desktopFileId", toml::node_type::string) ||
                    wrongType(*itemTbl, "launchInTerminal", toml::node_type::boolean) ||
                    wrongType(*itemTbl, "launchWithDebugger", toml::node_type::boolean) ||
                    wrongType(*itemTbl, "singleInstance", toml::node_type::boolean) ||
                    wrongType(*itemTbl, "isScript", toml::node_type::boolean)) {
                    result.error = ReadError::UnsupportedStructure;
                    return result;
                }

                App app;
                if ((*itemTbl)["separator"].value_or(false)) {
                    app.separator = true;
                    category.items.append(app);
                    continue;
                }

                app.name = QString::fromStdString(
                    (*itemTbl)["name"].value_or(std::string{"Application"}));
                app.binary = QString::fromStdString(
                    (*itemTbl)["binary"].value_or(std::string{}));

                if (auto *argArray = (*itemTbl)["arguments"].as_array()) {
                    for (const auto &arg : *argArray) {
                        if (auto v = arg.value<std::string>())
                            app.arguments << QString::fromStdString(*v);
                        else {
                            result.error = ReadError::UnsupportedStructure;
                            return result;
                        }
                    }
                } else if (auto v = (*itemTbl)["arguments"].value<std::string>()) {
                    app.arguments = CommandLine::splitArgs(
                        QString::fromStdString(*v));
                } else if ((*itemTbl).contains("arguments")) {
                    result.error = ReadError::UnsupportedStructure;
                    return result;
                }

                app.workDir = QString::fromStdString(
                    (*itemTbl)["workDir"].value_or(std::string{}));
                app.iconPath = QString::fromStdString(
                    (*itemTbl)["icon"].value_or(std::string{}));
                app.desktopFileId = QString::fromStdString(
                    (*itemTbl)["desktopFileId"].value_or(std::string{}));
                app.launchInTerminal = (*itemTbl)["launchInTerminal"].value_or(false);
                app.launchWithDebugger = (*itemTbl)["launchWithDebugger"].value_or(false);
                app.singleInstance = (*itemTbl)["singleInstance"].value_or(true);
                app.isScript = (*itemTbl)["isScript"].value_or(false);

                if (auto *env = (*itemTbl)["env"].as_table()) {
                    for (auto &&[k, v] : *env) {
                        if (!v.is_string()) {
                            result.error = ReadError::UnsupportedStructure;
                            return result;
                        }
                        app.env.insert(
                            QString::fromStdString(std::string(k.str())),
                            QString::fromStdString(v.value_or(std::string{})));
                    }
                } else if ((*itemTbl).contains("env")) {
                    // `env` must be a table; anything else would be silently
                    // ignored and then lost on the next save.
                    result.error = ReadError::UnsupportedStructure;
                    return result;
                }

                category.items.append(app);
            }
        }

        result.categories.append(category);
    }

    return result;
}

bool write(const QStandardItemModel &model, const QString &path, QString *error)
{
    QString out;
    out.reserve(8192);

    bool firstCategory = true;
    for (int i = 0; i < model.rowCount(); ++i) {
        auto *catItem = model.item(i);
        if (catItem->data(TypeRole).toInt() != TypeCategory) continue;

        if (!firstCategory)
            out += QLatin1String("\n\n");    // 1 blank line between categories
        firstCategory = false;

        // ── Category ────────────────────────────────────────────────
        out += QLatin1String("[[categories]]\n");
        out += QLatin1String("name = ")      + tomlString(catItem->text()) + QLatin1Char('\n');

        const QString catIcon = catItem->data(IconPathRole).toString();
        if (!catIcon.isEmpty())
            out += QLatin1String("icon = ")  + tomlString(catIcon)        + QLatin1Char('\n');

        const QVariant expanded = catItem->data(ExpandedRole);
        const bool collapsed    = expanded.isValid() && !expanded.toBool();
        out += QLatin1String("collapsed = ") + tomlBool(collapsed)        + QLatin1Char('\n');

        // ── Items ───────────────────────────────────────────────────
        for (int j = 0; j < catItem->rowCount(); ++j) {
            auto *it = catItem->child(j);

            if (it->data(TypeRole).toInt() == TypeSeparator) {
                out += QLatin1String("\n\n[[categories.items]]\n");
                out += QLatin1String("separator = true\n");
                continue;
            }

            auto *app = static_cast<AppItem *>(it);

            out += QLatin1String("\n\n[[categories.items]]\n");

            // 1. Identity
            out += QLatin1String("name = ")      + tomlString(app->text())       + QLatin1Char('\n');
            if (!app->iconPath.isEmpty())
                out += QLatin1String("icon = ")  + tomlString(app->iconPath)     + QLatin1Char('\n');
            if (!app->desktopFileId.isEmpty())
                out += QLatin1String("desktopFileId = ") + tomlString(app->desktopFileId) + QLatin1Char('\n');

            // 2. Launch target
            out += QLatin1String("binary = ")    + tomlString(app->binaryPath)   + QLatin1Char('\n');
            if (!app->workDir.isEmpty())
                out += QLatin1String("workDir = ") + tomlString(app->workDir)    + QLatin1Char('\n');
            if (!app->arguments.isEmpty()) {
                out += QLatin1String("arguments = [");
                for (int a = 0; a < app->arguments.size(); ++a) {
                    if (a > 0) out += QLatin1String(", ");
                    out += tomlString(app->arguments.at(a));
                }
                out += QLatin1String("]\n");
            }

            // 4. Behavior flags
            out += QLatin1String("isScript = ")           + tomlBool(app->isScript)           + QLatin1Char('\n');
            out += QLatin1String("launchInTerminal = ")   + tomlBool(app->launchInTerminal)   + QLatin1Char('\n');
            out += QLatin1String("launchWithDebugger = ") + tomlBool(app->launchWithDebugger) + QLatin1Char('\n');
            out += QLatin1String("singleInstance = ")     + tomlBool(app->singleInstance)     + QLatin1Char('\n');

            // 5. Environment (sub-table, last)
            if (!app->env.isEmpty()) {
                out += QLatin1String("\n  [categories.items.env]\n");
                QStringList keys = app->env.keys();
                std::sort(keys.begin(), keys.end(),
                          [](const QString &a, const QString &b){
                    return a.compare(b, Qt::CaseInsensitive) < 0;
                });
                for (const QString &k : keys)
                    out += QLatin1String("  ") + tomlKey(k) + QLatin1String(" = ")
                         + tomlString(app->env.value(k)) + QLatin1Char('\n');
            }
        }
    }

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        *error = f.errorString();
        return false;
    }
    const QByteArray data = out.toUtf8();
    if (f.write(data) != data.size()) {
        *error = f.errorString();
        f.cancelWriting();
        return false;
    }
    if (!f.commit()) {
        *error = f.errorString();
        return false;
    }
    return true;
}

} // namespace ConfigFile
