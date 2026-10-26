#include "../desktop/kde_shortcuts.h"
#include <QApplication>
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
    const QKeySequence key(Qt::META | Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_F11);
    if (!KGlobalAccel::isGlobalShortcutAvailable(key))
    {
        std::cerr << "Probe key already occupied; no changes made\n";
        return 3;
    }
    // This action has no monitor callback, and all probe bindings are removed.
    KdeShortcuts shortcuts;
    if (!shortcuts.bind("registration-probe", "Temporary shortcut registration test", key, [] {}))
        return 4;
    if (!KGlobalAccel::self()->globalShortcut("lginputswitch-desktop", "registration-probe").contains(key))
        return 5;
    shortcuts.clear();
    if (!KGlobalAccel::self()->globalShortcut("lginputswitch-desktop", "registration-probe").isEmpty())
        return 6;
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
    std::cout << "KDE registration, readback, cleanup and non-stealing conflict checks passed; no monitor "
                 "commands sent\n";
}
