#include "platform.h"
#include "../core/input_value.h"
#include "process.h"
#include "profiles.h"
#include "windows_monitors.h"
#ifdef Q_OS_WIN
#include "../platform/windows/adl_session.h"
#include "../platform/windows/adl_transport.h"
#else
#include "../platform/linux/ddcutil_backend.h"
#endif

static Discovery DiscoverPlatformMonitors()
{
    Discovery result;
#ifdef Q_OS_WIN
    if (!InitializeAdl())
    {
        result.message = "AMD display access is unavailable. Check the AMD graphics driver.";
        return result;
    }
    int count = 0;
    if (Adl().adapterCount(&count) != ADL_OK || count <= 0)
    {
        result.message = "No AMD display adapters found.";
        return result;
    }
    std::vector<AdapterInfo> adapters(count);
    for (auto &adapter : adapters)
        adapter.iSize = sizeof(AdapterInfo);
    if (Adl().adapters(adapters.data(), int(adapters.size() * sizeof(AdapterInfo))) != ADL_OK)
    {
        result.message = "Could not enumerate AMD adapters.";
        return result;
    }
    for (const auto &adapter : adapters)
    {
        int displays = 0;
        ADLDisplayInfo *info = nullptr;
        const auto rc = Adl().displays(adapter.iAdapterIndex, &displays, &info, 0);
        if (rc == ADL_OK && info)
            for (int j = 0; j < displays; ++j)
            {
                const int required =
                    ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
                if ((info[j].iDisplayInfoValue & required) != required ||
                    info[j].displayID.iDisplayLogicalAdapterIndex != adapter.iAdapterIndex)
                    continue;
                const int display = info[j].displayID.iDisplayLogicalIndex;
                QString identity, inputProfile;
                QJsonObject hardware;
                if (Adl().edid)
                {
                    ADLDisplayEDIDData data{};
                    data.iSize = sizeof(data);
                    if (Adl().edid(adapter.iAdapterIndex, display, &data) == ADL_OK &&
                        data.iEDIDSize >= 128 && data.iEDIDSize <= ADL_MAX_EDIDDATA_SIZE)
                    {
                        const QByteArray edid(data.cEDIDData, data.iEDIDSize);
                        identity = MonitorIdentity(edid);
                        inputProfile = MonitorInputProfileFromEdid(edid);
                        hardware = MonitorEdidHardware(edid);
                    }
                }
                DesktopMonitor target =
                    {QString::fromLocal8Bit(info[j].strDisplayName) +
                         QString(" · Adapter %1 / Display %2").arg(adapter.iAdapterIndex).arg(display),
                     adapter.iAdapterIndex, display, false, identity, inputProfile};
                target.hardware = hardware;
                result.monitors.push_back(target);
            }
        if (info)
            ReleaseAdlMemory(reinterpret_cast<void **>(&info));
    }
#else
    const auto probe = RunDesktopProcess("ddcutil", {"detect", "--brief"}, 15000);
    if (probe.status == "unavailable")
    {
        result.message = "ddcutil is unavailable. Install it using your distribution's package manager.";
        return result;
    }
    if (probe.status == "timed out")
    {
        result.message = "Monitor discovery timed out. Check device access, then try again.";
        return result;
    }
    const QString output = QString::fromLocal8Bit(probe.output);
    result.monitors = ParseDesktopDiscovery(probe.output).monitors;
    // Prefer the base EDID fingerprint when the kernel exposes a connector's
    // DDC bus. This also works with older ddcutil brief output without serials.
    const QDir drm("/sys/class/drm");
    for (auto &monitor : result.monitors)
        if (monitor.bus)
            for (const auto &connector : drm.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            {
                const auto path = drm.filePath(connector);
                if (QFileInfo(path + "/ddc").canonicalFilePath().section('/', -1) !=
                    QString("i2c-%1").arg(monitor.display))
                    continue;
                QFile edid(path + "/edid");
                if (edid.open(QIODevice::ReadOnly))
                {
                    const auto bytes = edid.read(128);
                    const auto identity = MonitorIdentity(bytes);
                    if (!identity.isEmpty())
                    {
                        monitor.identity = identity;
                        monitor.hardware = MonitorEdidHardware(bytes);
                        const auto profile = MonitorInputProfileFromEdid(bytes);
                        if (!profile.isEmpty())
                            monitor.inputProfile = profile;
                    }
                }
            }
    if (probe.status != "finished" || probe.exitCode != 0)
    {
        result.monitors.clear();
        result.message = (output + "\n" + QString::fromLocal8Bit(probe.errorOutput)).trimmed().left(1200);
    }
#endif
    if (result.message.isEmpty())
        result.message =
            result.monitors.isEmpty()
                ? "No controllable monitors found. Check the cable, graphics driver and DDC/I2C access."
                : QString("%1 monitor(s) available. Use Connect on a saved monitor if needed.")
                      .arg(result.monitors.size());
    return result;
}

Discovery DiscoverDesktopMonitors()
{
    auto result = DiscoverPlatformMonitors();
#ifdef Q_OS_WIN
    // The native path does not depend on AMD initialization and is isolated in
    // case a dock/display driver hangs while exposing its monitor information.
    const auto probe = RunDesktopProcess(QCoreApplication::applicationFilePath(),
                                         {"--windows-discovery"}, 15000, 262144);
    if (probe.status == "finished" && probe.exitCode == 0)
        result = MergeWindowsDiscovery(result, ParseNativeWindowsDiscovery(probe.output));
    else
        result.message += " Windows dock discovery " + probe.status + ".";
#endif
    return result;
}

lg::SwitchResult SendDesktopInput(const DesktopConfig &c, const QString &code)
{
    if (c.method != "lg" && c.method != "standard")
        return {lg::SwitchStatus::InvalidArgument, 2, "Choose a supported input switching method."};
    unsigned int source = 0x51, value;
    if ((c.method == "lg" && !ParseInputValue(c.source.toStdString(), 0xff, source)) ||
        !ParseInputValue(code.toStdString(), 0xffff, value) || c.display < 0)
        return {lg::SwitchStatus::InvalidArgument, 2, "Invalid monitor or input code."};
    const lg::InputCommand command{source, c.method == "standard" ? 0x60u : 0xf4u, value};
#ifdef Q_OS_WIN
    if (c.transport == WindowsDdcTransport)
    {
        QStringList args{"--windows-switch", c.nativeDisplay, c.targetIdentity, code};
        if (c.method == "lg")
            args.append(c.source);
        const auto probe = RunDesktopProcess(QCoreApplication::applicationFilePath(),
                                             args, 10000);
        if (probe.status != "finished" || probe.exitCode != 0)
            return {lg::SwitchStatus::TransportFailure, probe.exitCode,
                    ("Windows input command " + probe.status + "; physical switch unverified.").toStdString()};
        const auto result = QJsonDocument::fromJson(probe.output).object();
        if (!result.contains("sent") || !result.contains("native_code"))
            return {lg::SwitchStatus::TransportFailure, -1, "Invalid Windows command result; physical switch unverified."};
        return {result["sent"].toBool() ? lg::SwitchStatus::CommandSent : lg::SwitchStatus::TransportFailure,
                result["native_code"].toInt(-1), result["detail"].toString().toStdString()};
    }
    if (!c.transport.isEmpty())
        return {lg::SwitchStatus::InvalidArgument, 87, "Unknown monitor transport."};
    AdlTransport transport(c.adapter, c.display);
    return transport.Send(command);
#else
    if (!c.transport.isEmpty())
        return {lg::SwitchStatus::InvalidArgument, 87, "This monitor connection is only supported on Windows."};
    DdcutilTransport transport(c.bus ? "--bus" : "--display", static_cast<unsigned int>(c.display));
    const auto arguments = transport.Arguments(command);
    if (arguments.empty())
        return {lg::SwitchStatus::InvalidArgument, 2, "Invalid monitor or input command."};
    QStringList args;
    for (std::size_t i = 1; i < arguments.size(); ++i)
        args.push_back(QString::fromStdString(arguments[i]));
    const auto result = RunDesktopProcess("ddcutil", args, 10000);
    if (result.status == "finished" && result.exitCode == 0)
        return {lg::SwitchStatus::CommandSent, 0, "Command sent; physical switch unverified."};
    return {
        lg::SwitchStatus::TransportFailure, result.exitCode,
        (QString("ddcutil %1 (code %2). Check cable and I2C access.").arg(result.status).arg(result.exitCode))
            .toStdString()};
#endif
}

MonitorBrightness AccessMonitorBrightness(const DesktopMonitor &target, int percent)
{
    if (percent < -1 || percent > 100)
        return {-1, "Invalid brightness."};
#ifdef Q_OS_WIN
    if (target.identity.isEmpty())
        return {-1, "Brightness needs an identifiable monitor connection."};
    const auto result = RunDesktopProcess(QCoreApplication::applicationFilePath(),
        {"--windows-brightness", target.nativeDisplay, target.identity, QString::number(percent)}, 10000);
    if (result.status == "finished" && result.exitCode == 0)
    {
        const auto value = QJsonDocument::fromJson(result.output).object();
        return {value["percent"].toInt(-1), value["status"].toString("Brightness unavailable.")};
    }
    return {-1, "Windows brightness request " + result.status + "."};
#else
    if (!target.transport.isEmpty() || target.display < 0 || (!target.bus && target.display == 0))
        return {-1, "Brightness needs a connected monitor."};
    const QStringList selector{target.bus ? "--bus" : "--display", QString::number(target.display)};
    const auto read = [&]() -> QPair<int, int> {
        const auto result = RunDesktopProcess("ddcutil", selector + QStringList{"getvcp", "10", "--terse"}, 5000);
        if (result.status != "finished" || result.exitCode != 0)
            return {-1, 0};
        const auto match = QRegularExpression(R"(^VCP\s+10\s+C\s+(\d{1,5})\s+(\d{1,5})$)")
                               .match(QString::fromLocal8Bit(result.output).trimmed());
        const int value = match.captured(1).toInt(), maximum = match.captured(2).toInt();
        return maximum > 0 && maximum <= 65535 && value <= maximum
            ? qMakePair(value, maximum) : qMakePair(-1, 0);
    };
    auto [value, maximum] = read();
    if (!maximum)
        return {-1, "This monitor did not report brightness."};
    if (percent >= 0)
    {
        const int requested = (maximum * percent + 50) / 100;
        if (requested != value)
        {
            const auto set = RunDesktopProcess("ddcutil",
                selector + QStringList{"setvcp", "10", QString::number(requested)}, 7000);
            const auto confirmed = read();
            if (!confirmed.second)
                return {-1, "Could not confirm brightness after the request."};
            value = confirmed.first;
            maximum = confirmed.second;
            if (set.status != "finished" || set.exitCode != 0 || value != requested)
                return {(value * 100 + maximum / 2) / maximum, "The monitor did not confirm the requested brightness."};
        }
    }
    return {(value * 100 + maximum / 2) / maximum, {}};
#endif
}
