#pragma once
#include "platform.h"
#include <functional>

inline constexpr auto WindowsDdcTransport = "windows-ddc";
int NativeWindowsDisplayNumber(const QString &name);
Discovery ParseNativeWindowsDiscovery(const QByteArray &json);
Discovery MergeWindowsDiscovery(Discovery amd, const Discovery &native);
using NativeMonitorWriter = std::function<lg::SwitchResult(const DesktopMonitor &, unsigned int)>;
lg::SwitchResult DispatchNativeWindowsInput(const DesktopConfig &config, const QString &code,
                                          const Discovery &live, const NativeMonitorWriter &write);
#ifdef Q_OS_WIN
// Internal child-process operations: no AMD initialization or fallback writes.
QJsonObject ProbeNativeWindowsDiscovery();
QJsonObject ProbeNativeWindowsCapabilities(const QString &display, const QString &identity);
QJsonObject ProbeNativeWindowsSwitch(const QString &display, const QString &identity, const QString &code);
QJsonObject NativeWindowsHardware(const QString &display);
#endif
