#pragma once
#include "../core/input_switcher.h"
#include <QtCore>
struct DesktopInput
{
    QString label, code, shortcut;
    bool enabled = true;
};
struct DesktopConfig
{
    QVector<DesktopInput> inputs{{"DisplayPort", "0xD0", "CTRL+ALT+2", true},
                                 {"USB-C", "0xD1", "CTRL+ALT+3", true},
                                 {"HDMI1", "0x90", "CTRL+ALT+4", true},
                                 {"HDMI2", "0x91", "CTRL+ALT+5", true}};
    QString source = "0x50", cycleShortcut = "CTRL+ALT+1", appearance = "system";
    int adapter = -1, display = -1, debounce = 750;
    bool bus = false, notifications = true, startup = false, globalShortcuts = false;
};
struct DesktopMonitor
{
    QString label;
    int adapter = -1, display = -1;
    bool bus = false;
};
struct Discovery
{
    QVector<DesktopMonitor> monitors;
    QString message;
};
DesktopConfig LoadDesktopConfig(QString &warning);
bool SaveDesktopConfig(const DesktopConfig &config, QString &error);
Discovery DiscoverDesktopMonitors();
lg::SwitchResult SendDesktopInput(const DesktopConfig &config, const QString &code);
