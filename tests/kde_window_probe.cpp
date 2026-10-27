#define LG_DESKTOP_TEST
#include "../desktop/main.cpp"
#include <iostream>
struct DesktopWindowProbe
{
    static int check(DesktopWindow &window)
    {
        const QString component = "lginputswitch-window-probe";
        const auto keys = KGlobalAccel::self()->globalShortcut(component, "cycle");
        std::cout << "KDE available=" << window.kdeAvailable_ << " enabled=" << window.config_.globalShortcuts
                  << " recording=" << window.recordingShortcut_ << " focus="
                  << (window.focusWidget() ? window.focusWidget()->metaObject()->className() : "none")
                  << " daemon active=" << KGlobalAccel::isComponentActive(component)
                  << " cycle keys=" << keys.size() << "\n";
        const bool passed = window.kdeAvailable_ && !keys.isEmpty();
        window.kdeShortcuts_.clear(true);
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
