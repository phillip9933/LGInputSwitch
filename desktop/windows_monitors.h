#pragma once
#include "platform.h"
#include <functional>

inline constexpr auto WindowsDdcTransport = "windows-ddc";
inline bool SupportsLgAlternate(const DesktopMonitor &target)
{
    return target.transport.isEmpty() || (target.transport == WindowsDdcTransport &&
        target.hardware["adapter_kind"] == "nvidia" && target.hardware["nvidia_ddc"].toBool());
}
QString NativeWindowsAdapterKind(const QString &deviceId, const QString &description);
int NativeWindowsDisplayNumber(const QString &name);
Discovery ParseNativeWindowsDiscovery(const QByteArray &json);
Discovery MergeWindowsDiscovery(Discovery amd, const Discovery &native);
using NativeMonitorWriter = std::function<lg::SwitchResult(const DesktopMonitor &, unsigned int)>;
using NvidiaMonitorWriter = std::function<lg::SwitchResult(const DesktopMonitor &, const lg::InputCommand &)>;
lg::SwitchResult DispatchNativeWindowsInput(const DesktopConfig &config, const QString &code,
                                          const Discovery &live, const NativeMonitorWriter &write,
                                          const NvidiaMonitorWriter &writeNvidia = {});
#ifdef Q_OS_WIN
// Internal child-process operations: no AMD initialization or fallback writes.
QJsonObject ProbeNativeWindowsDiscovery();
QJsonObject ProbeNativeWindowsCapabilities(const QString &display, const QString &identity);
QJsonObject ProbeNativeWindowsSwitch(const QString &display, const QString &identity, const QString &code,
                                     const QString &source = {});
QJsonObject ProbeNativeWindowsBrightness(const QString &display, const QString &identity, int percent);
QJsonObject NativeWindowsHardware(const QString &display);
#endif
