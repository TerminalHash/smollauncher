#include <QtTest>
#include "include/desktopentry.h"

class DesktopEntryTest : public QObject
{
    Q_OBJECT
private slots:
    void splitsQuotedAndEscapedArguments();
    void keepsEmptyQuotedArgument();
    void escapesPercentBeforeRemovingFieldCodes();
};

static QStringList split(const QString &raw, QString *program = nullptr)
{
    QString p;
    QStringList a;
    DesktopEntryScanner::splitExec(raw, p, a);
    if (program) *program = p;
    return a;
}

void DesktopEntryTest::splitsQuotedAndEscapedArguments()
{
    QString program;
    const QStringList args = split(
        QStringLiteral("foo \"a b\" c\\\\d \"e\\sf\" %% g"), &program);

    QCOMPARE(program, QStringLiteral("foo"));
    QCOMPARE(args, QStringList({ QStringLiteral("a b"),
                                 QStringLiteral("c\\d"),
                                 QStringLiteral("e f"),
                                 QStringLiteral("%"),
                                 QStringLiteral("g") }));
}

void DesktopEntryTest::keepsEmptyQuotedArgument()
{
    QString program;
    const QStringList args = split(
        QStringLiteral("foo \"\" bar \"a\\\"b\""), &program);

    QCOMPARE(program, QStringLiteral("foo"));
    QCOMPARE(args, QStringList({ QString(),
                                 QStringLiteral("bar"),
                                 QStringLiteral("a\"b") }));
}

void DesktopEntryTest::escapesPercentBeforeRemovingFieldCodes()
{
    // "%%f" is a literal "%f", not the %f field code.
    QString program;
    const QStringList args = split(QStringLiteral("foo %%f %u"), &program);

    QCOMPARE(program, QStringLiteral("foo"));
    QCOMPARE(args, QStringList({ QStringLiteral("%f") }));
}

QTEST_MAIN(DesktopEntryTest)
#include "tst_desktopentry.moc"
