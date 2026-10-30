#define LG_DESKTOP_TEST
#include "../desktop/main.cpp"
#include <iostream>
struct DesktopWindowProbe
{
    static int check(DesktopWindow &window)
    {
        const QString component = "lginputswitch-window-probe";
        const auto keys = KGlobalAccel::self()->globalShortcut(component, "cycle");
        const auto groupCycle = KGlobalAccel::self()->globalShortcut(component, "profile-group-cycle");
        const auto groupAction = KGlobalAccel::self()->globalShortcut(component, "profile-group-work");
        std::cout << "KDE available=" << window.kdeAvailable_ << " enabled=" << window.config_.globalShortcuts
                  << " recording=" << window.recordingShortcut_ << " focus="
                  << (window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none")
                  << " daemon active=" << KGlobalAccel::isComponentActive(component)
                  << " cycle keys=" << keys.size() << " group cycle keys=" << groupCycle.size()
                  << " group action keys=" << groupAction.size() << "\n";
        const bool passed =
            window.kdeAvailable_ && !keys.isEmpty() && !groupCycle.isEmpty() && !groupAction.isEmpty();
        window.kdeShortcuts_.clear(true);
        window.kdeShortcuts_.pruneInactive();
        return passed ? 0 : 1;
    }
};
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitchWindowProbe");
    app.setApplicationName("LGInputSwitchDesktop");
    app.setDesktopFileName("lginputswitch");
    QTemporaryDir temporary;
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, temporary.path());
    QSettings preferences;
    preferences.setValue("globalShortcuts", true);
    preferences.setValue("cycleShortcut", "META+CTRL+ALT+SHIFT+F11");
    QJsonArray inputs{
        QJsonObject{{"label", "Synthetic input"}, {"code", "0xd0"}, {"enabled", true}, {"shortcut", ""}}};
    preferences.setValue("inputs", QJsonDocument(inputs).toJson(QJsonDocument::Compact));
    preferences.sync();
    QString warning, error;
    auto workspace = MigrateDesktopConfig(LoadDesktopConfig(warning));
    auto side = workspace.monitors[0];
    side.id = "side";
    side.name = "Side monitor";
    side.method = "standard";
    side.inputs = DefaultMonitorInputs("standard");
    workspace.monitors.push_back(side);
    MonitorProfile group;
    group.id = "group";
    group.name = "Both monitors";
    group.monitors = {workspace.monitors[0].id, side.id};
    group.cycleShortcut = "META+CTRL+ALT+SHIFT+F12";
    group.actions = {
        {"work",
         "Work PC",
         "META+CTRL+ALT+SHIFT+F10",
         true,
         {{workspace.monitors[0].id, workspace.monitors[0].inputs[0].id}, {side.id, side.inputs[0].id}}}};
    workspace.profiles.push_back(group);
    if (!SaveDesktopWorkspace(workspace, error))
    {
        std::cerr << error.toStdString() << "\n";
        return 2;
    }
    for (const auto &key :
         {workspace.profiles[0].cycleShortcut, group.cycleShortcut, group.actions[0].shortcut})
        if (!KGlobalAccel::isGlobalShortcutAvailable(
                QKeySequence::fromString(key, QKeySequence::PortableText), "lginputswitch-window-probe"))
        {
            std::cerr << "Probe key unavailable; no native assignments changed\n";
            return 3;
        }
    QFile fake(temporary.path() + "/ddcutil");
    if (!fake.open(QIODevice::WriteOnly))
        return 2;
    fake.write("#!/bin/sh\nexit 0\n");
    fake.close();
    fake.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    qputenv("PATH", temporary.path().toUtf8());
    DesktopWindow window(false, "lginputswitch-window-probe");
    window.show();
    QEventLoop loop;
    QTimer::singleShot(500, &loop, &QEventLoop::quit);
    loop.exec();
    return DesktopWindowProbe::check(window);
}
