#include "../desktop/kde_shortcuts.h"
#include <QApplication>
#include <QElapsedTimer>
#include <QProcess>
#include <QThread>
#include <iostream>
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setApplicationName("LGInputSwitchShortcutProbe");
    if (!KdeShortcuts::available())
    {
        std::cerr << "KDE shortcut service unavailable\n";
        return 2;
    }
    if (app.arguments().contains("--external-clear"))
    {
        QAction external("Temporary external editor");
        external.setObjectName("sync-probe");
        external.setProperty("componentName", "lginputswitch-shortcut-probe");
        external.setProperty("isConfigurationAction", true);
        return KGlobalAccel::self()->setShortcut(&external, {}, KGlobalAccel::NoAutoloading) ? 0 : 10;
    }
    const QKeySequence key(Qt::META | Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);
    if (!KGlobalAccel::isGlobalShortcutAvailable(key))
    {
        std::cerr << "Probe key already occupied; no changes made\n";
        return 3;
    }
    // This action has no monitor callback, and all probe bindings are removed.
    KdeShortcuts shortcuts("lginputswitch-shortcut-probe");
    if (!shortcuts.bind("registration-probe", "Temporary shortcut registration test", key, [] {}))
        return 4;
    if (!KGlobalAccel::self()
             ->globalShortcut("lginputswitch-shortcut-probe", "registration-probe")
             .contains(key))
        return 5;
    shortcuts.clear(true);
    if (!KGlobalAccel::self()->globalShortcut("lginputswitch-shortcut-probe", "registration-probe").isEmpty())
        return 6;
    QKeySequence observed;
    if (!shortcuts.bind(
            "sync-probe", "Temporary linked shortcut test", key, [] {},
            [&](const QKeySequence &actual) { observed = actual; }))
        return 9;
    QProcess editor;
    editor.start(QCoreApplication::applicationFilePath(), {"--external-clear"});
    if (!editor.waitForFinished(5000) || editor.exitCode() != 0)
        return 10;
    QElapsedTimer timer;
    timer.start();
    while (!observed.isEmpty() && timer.elapsed() < 3000)
    {
        QCoreApplication::processEvents();
        QThread::msleep(5);
    }
    if (!observed.isEmpty())
        return 11;
    shortcuts.clear(); // Deactivate but keep the assignment made by Plasma.
    if (!shortcuts.bind("sync-probe", "Temporary linked shortcut test", key, [] {}, {}, false))
        return 12;
    if (!KGlobalAccel::self()->globalShortcut("lginputswitch-shortcut-probe", "sync-probe").isEmpty())
        return 13;
    shortcuts.clear(true);
    QAction owner("Temporary conflict owner");
    owner.setObjectName("conflict-probe");
    owner.setProperty("componentName", "lginputswitch-conflict-probe");
    if (!KGlobalAccel::self()->setShortcut(&owner, {key}, KGlobalAccel::NoAutoloading))
        return 7;
    const bool refused =
        !shortcuts.bind("registration-probe", "Temporary shortcut registration test", key, [] {});
    const bool retained = KGlobalAccel::self()->shortcut(&owner).contains(key);
    KGlobalAccel::self()->removeAllShortcuts(&owner);
    if (!refused || !retained)
        return 8;
    std::cout << "KDE registration, two-way synchronization, persistent reload, cleanup and non-stealing "
                 "conflict checks passed; no monitor "
                 "commands sent\n";
}
