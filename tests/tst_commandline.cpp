#include <QtTest>

#include "include/commandline.h"

class CommandLineTest : public QObject
{
    Q_OBJECT
private slots:
    void splitsWhitespace();
    void splitsQuotes();
    void splitsEscapes();
    void keepsEmptyQuotedArgument();
    void keepsUnclosedQuoteText();
    void quotesShellMetacharacters();
    void windowsQuotesArguments();
    void joinArgsRoundTrips();
    void leavesSlashPathsAlone();
};

void CommandLineTest::splitsWhitespace()
{
    QCOMPARE(CommandLine::splitArgs(QStringLiteral("  a   b\tc ")),
             QStringList({ QStringLiteral("a"),
                           QStringLiteral("b"),
                           QStringLiteral("c") }));
}

void CommandLineTest::splitsQuotes()
{
    QCOMPARE(CommandLine::splitArgs(QStringLiteral("a \"b c\" 'd e'")),
             QStringList({ QStringLiteral("a"),
                           QStringLiteral("b c"),
                           QStringLiteral("d e") }));
}

void CommandLineTest::splitsEscapes()
{
    QCOMPARE(CommandLine::splitArgs(QStringLiteral("a\\ b c\\\"d")),
             QStringList({ QStringLiteral("a b"),
                           QStringLiteral("c\"d") }));
}

void CommandLineTest::keepsEmptyQuotedArgument()
{
    QCOMPARE(CommandLine::splitArgs(QStringLiteral("\"\" x")),
             QStringList({ QString(), QStringLiteral("x") }));
}

void CommandLineTest::keepsUnclosedQuoteText()
{
    QCOMPARE(CommandLine::splitArgs(QStringLiteral("a \"b c")),
             QStringList({ QStringLiteral("a"),
                           QStringLiteral("b c") }));
}

void CommandLineTest::quotesShellMetacharacters()
{
    QCOMPARE(CommandLine::shellQuote(QString()), QStringLiteral("''"));
    QCOMPARE(CommandLine::shellQuote(QStringLiteral("/opt/game/bin")),
             QStringLiteral("/opt/game/bin"));
    QCOMPARE(CommandLine::shellQuote(QStringLiteral("a b")),
             QStringLiteral("'a b'"));
    QCOMPARE(CommandLine::shellQuote(QStringLiteral("a;b")),
             QStringLiteral("'a;b'"));
    QCOMPARE(CommandLine::shellQuote(QStringLiteral("a'b")),
             QStringLiteral("'a'\\''b'"));
}

void CommandLineTest::windowsQuotesArguments()
{
    QCOMPARE(CommandLine::windowsQuote(QString()), QStringLiteral("\"\""));
    QCOMPARE(CommandLine::windowsQuote(QStringLiteral("plain")),
             QStringLiteral("plain"));
    QCOMPARE(CommandLine::windowsQuote(QStringLiteral("a b")),
             QStringLiteral("\"a b\""));
    QCOMPARE(CommandLine::windowsQuote(QStringLiteral("a&b")),
             QStringLiteral("\"a&b\""));
    QCOMPARE(CommandLine::windowsQuote(QStringLiteral("a\"b")),
             QStringLiteral("\"a\\\"b\""));
    QCOMPARE(CommandLine::windowsQuote(QStringLiteral("a b\\")),
             QStringLiteral("\"a b\\\\\""));

    QCOMPARE(CommandLine::windowsJoinArgs(
                 { QStringLiteral("game.exe"), QStringLiteral("a b") }),
             QStringLiteral("game.exe \"a b\""));
}

void CommandLineTest::joinArgsRoundTrips()
{
    const QStringList args = {
        QStringLiteral("plain"),
        QStringLiteral("a b"),
        QStringLiteral("c'd"),
        QStringLiteral("$e"),
        QString()
    };

    QCOMPARE(CommandLine::splitArgs(CommandLine::joinArgs(args)), args);
}

void CommandLineTest::leavesSlashPathsAlone()
{
    QCOMPARE(CommandLine::resolveCommandPath(QString()),
             QString());
    QCOMPARE(CommandLine::resolveCommandPath(QStringLiteral("/opt/game/bin")),
             QStringLiteral("/opt/game/bin"));
}

QTEST_MAIN(CommandLineTest)
#include "tst_commandline.moc"
