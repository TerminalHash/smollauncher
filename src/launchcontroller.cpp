#include "launchcontroller.h"
#include "include/mainwindow.h"
#include "include/appitem.h"
#include "include/commandline.h"
#include "include/debug.h"
#include "include/terminalsettings.h"

#include <QApplication>
#include <QDateTime>
#include <QFileInfo>
#include <QMessageBox>
#include <QPointer>
#include <QProcess>
#include <QProcessEnvironment>
#ifdef Q_OS_LINUX
#include <QStandardPaths>
#endif

#ifdef Q_OS_UNIX
#include <unistd.h>
#endif

namespace {

QProcessEnvironment appEnvironment(const AppItem *app)
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    if (app) {
        for (auto it = app->env.constBegin(); it != app->env.constEnd(); ++it)
            env.insert(it.key(), it.value());
    }
    return env;
}

#if defined(Q_OS_LINUX)
QString findTerminalProgram()
{
    static const QStringList candidates = {
        "x-terminal-emulator", "gnome-terminal", "konsole",
        "xfce4-terminal", "xterm"
    };
    for (const QString &t : candidates) {
        if (!QStandardPaths::findExecutable(t).isEmpty())
            return t;
    }
    return QString();
}
#endif

} // namespace

LaunchController::LaunchController(MainWindow *window)
    : m_w(window)
{
}

void LaunchController::launch(AppItem *app)
{
    if (!app) return;
    if (app->binaryPath.isEmpty()) {
        QMessageBox::warning(m_w, MainWindow::tr("SmolLauncher"),
                             MainWindow::tr("Please choose a binary first."));
        return;
    }

    // ----- single-instance check (doc requirement) -----------------------
    // Key by the AppItem's stable identity, so two entries with the same
    // command but different workdir/env stay independent.
    const QString key = QString::number(app->instanceId);
    if (app->singleInstance && m_w->m_running.contains(key)) {
        QProcess *p = m_w->m_running.value(key);
        if (p && p->state() != QProcess::NotRunning) {
            Debug::log(QStringLiteral("launch skipped (already running): \"%1\"")
                .arg(app->text()));
            m_w->updateStatus(MainWindow::tr("%1 is already running").arg(app->text()));
            return;
        }
        m_w->m_running.remove(key);
    }

    // ----- program & args ------------------------------------------------
    QString program = app->binaryPath;
    QStringList args = app->arguments;

    // ----- debugger ------------------------------------------------------
    if (app->launchWithDebugger) {
#ifdef Q_OS_LINUX
        if (QStandardPaths::findExecutable("gdb").isEmpty()) {
            QMessageBox::warning(m_w, MainWindow::tr("SmolLauncher"),
                                 MainWindow::tr("gdb was not found on this system."));
            return;
        }
        args.prepend(program);
        args.prepend("--args");
        program = "gdb";
#else
        QMessageBox::information(m_w, MainWindow::tr("SmolLauncher"),
                                 MainWindow::tr("Debugger launch is only supported on Linux."));
        return;
#endif
    }

    // ----- terminal wrapper ---------------------------------------------
    if (app->launchInTerminal) {
#if defined(Q_OS_LINUX)
        const TerminalSettings::Config config = TerminalSettings::load();
        QString term;
        QStringList termPrefix;
        if (config.custom) {
            term = config.customBin.trimmed();
            termPrefix = CommandLine::splitArgs(config.customArgs);
        } else {
            term = config.program;
            if (term.isEmpty())
                term = findTerminalProgram();
            termPrefix = TerminalSettings::commandPrefix(term);
        }
        if (term.isEmpty() || QStandardPaths::findExecutable(term).isEmpty()) {
            QMessageBox::warning(
                m_w, MainWindow::tr("SmolLauncher"),
                term.isEmpty()
                    ? MainWindow::tr("No terminal emulator found.")
                    : MainWindow::tr("Terminal emulator not found:\n%1").arg(term));
            return;
        }
        termPrefix << program << args;
        program = term;
        args    = termPrefix;
#elif defined(Q_OS_WIN)
        QStringList full = QStringList() << program << args;
        args = QStringList() << "/c" << "start" << "cmd" << "/k"
                             << full.join(' ');
        program = "cmd.exe";
#else
        QMessageBox::information(m_w, MainWindow::tr("SmolLauncher"),
                                 MainWindow::tr("Terminal launch is not supported here."));
        return;
#endif
    }

    // ----- prepare QProcess ---------------------------------------------
    auto *process = new QProcess();     // parentless: survives the launcher
    process->setProgram(program);
    process->setArguments(args);

    QString workDir = app->workDir;
    if (workDir.isEmpty())
        workDir = QFileInfo(app->binaryPath).absolutePath();
    if (!workDir.isEmpty())
        process->setWorkingDirectory(workDir);

    QProcessEnvironment env = appEnvironment(app);
    process->setProcessEnvironment(env);
    // No capture is needed, so forward output instead of buffering it.
    process->setProcessChannelMode(QProcess::ForwardedChannels);

#ifdef Q_OS_UNIX
    // Run setsid() in the forked child just before exec(), so the app
    // becomes its own session leader and survives the launcher exiting.
    process->setChildProcessModifier([]{ ::setsid(); });
#endif

    // ----- bookkeeping ---------------------------------------------------
    const QString appName = app->text();
    QPointer<MainWindow> self = m_w;

    QObject::connect(process, &QProcess::started, process, [self, appName, process]{
        Debug::log(QStringLiteral("launched: \"%1\" pid=%2")
            .arg(appName).arg(process->processId()));
        if (self) self->updateStatus(MainWindow::tr("Launched: %1").arg(appName));
    });

    QObject::connect(process, &QProcess::errorOccurred, process,
            [process, self, key](QProcess::ProcessError err){
        if (err == QProcess::FailedToStart) {
            Debug::log(QStringLiteral("launch failed: %1").arg(process->errorString()));
            if (self) {
                if (self->m_running.value(key) == process)
                    self->m_running.remove(key);
                self->updateStatus(MainWindow::tr("Failed to start: %1")
                                       .arg(process->errorString()));
            }
            process->deleteLater();
        }
    });

    const QDateTime startedAt = QDateTime::currentDateTime();
    QObject::connect(process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            process, [process, self, key, appName, startedAt]
                    (int code, QProcess::ExitStatus st){
        const qint64 ms = startedAt.msecsTo(QDateTime::currentDateTime());
        const bool daemonized = (st == QProcess::NormalExit
                                && code == 0
                                && ms < 1000);

        Debug::detail(QStringLiteral("process finished: \"%1\" code=%2 status=%3 ms=%4 daemonized=%5")
            .arg(appName)
            .arg(code)
            .arg(st == QProcess::CrashExit ? QStringLiteral("crash")
                                           : QStringLiteral("normal"))
            .arg(ms)
            .arg(daemonized));

        if (self) {
            if (self->m_running.value(key) == process)
                self->m_running.remove(key);

            if (daemonized) {
                self->updateStatus(MainWindow::tr("%1 launched in background").arg(appName));
            } else {
                self->updateStatus(
                    MainWindow::tr("%1 exited (%2, code %3)")
                        .arg(appName,
                            st == QProcess::CrashExit ? MainWindow::tr("crashed") : MainWindow::tr("normal"))
                        .arg(code));
            }
        }

        process->deleteLater();
    });

    m_w->m_running.insert(key, process);

    // ----- go ------------------------------------------------------------
    Debug::detail(QStringLiteral("launch: name=\"%1\" program=\"%2\" args=[%3] workDir=\"%4\" terminal=%5 debugger=%6")
        .arg(appName, program, args.join(QLatin1Char(' ')), workDir)
        .arg(app->launchInTerminal)
        .arg(app->launchWithDebugger));
    m_w->updateStatus(MainWindow::tr("Launching %1...").arg(appName));
    process->start();
}
