#pragma once
#include "profiles.h"

struct MonitorInputRead
{
    QVector<DesktopInput> inputs;
    QString status;
    bool complete = false;
};
// Read-only, selected-target queries. No input commands or fallback write probes.
QJsonObject ReadMonitorCapabilities(const DesktopMonitor &target);
MonitorInputRead InputsFromCapabilities(const SavedMonitor &monitor, const QJsonObject &capabilities);
MonitorInputRead ReadMonitorInputs(const SavedMonitor &monitor, const DesktopMonitor &target);
bool IsPresetInputList(const SavedMonitor &monitor);
bool ApplyMonitorInputs(DesktopWorkspace &workspace, const QString &id, const MonitorInputRead &read);
QString InputNameKey(QString label);
#ifdef Q_OS_WIN
QJsonObject ProbeTargetCapabilities(int adapter, int display, const QString &expectedIdentity);
#endif
