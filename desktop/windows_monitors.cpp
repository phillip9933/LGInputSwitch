#include "windows_monitors.h"
#include "monitor_identity.h"
#include "diagnostics.h"
#include "../core/input_value.h"

int NativeWindowsDisplayNumber(const QString &name)
{
    const QString prefix = "\\\\.\\DISPLAY";
    if (!name.startsWith(prefix))
        return -1;
    const auto suffix = name.mid(prefix.size());
    bool ok = false;
    const int number = suffix.toInt(&ok);
    return ok && number > 0 && number <= 9999 && QString::number(number) == suffix ? number : -1;
}
Discovery ParseNativeWindowsDiscovery(const QByteArray &json)
{
    Discovery result;
    if (json.size() > 262144)
        return result;
    const auto doc = QJsonDocument::fromJson(json);
    const auto monitors = doc.object()["monitors"].toArray();
    if (!doc.isObject() || monitors.size() > 32)
        return result;
    for (const auto &item : monitors)
    {
        const auto record = item.toObject();
        const auto name = record["windows_display"].toString();
        const auto identity = record["identity"].toString();
        const auto label = record["label"].toString();
        if (NativeWindowsDisplayNumber(name) < 0 || label.isEmpty() || label.size() > 256 ||
            !QRegularExpression("^[0-9a-f]{64}$").match(identity).hasMatch())
            continue;
        DesktopMonitor monitor{label, -1, NativeWindowsDisplayNumber(name), false, identity};
        monitor.transport = WindowsDdcTransport;
        monitor.nativeDisplay = name;
        monitor.hardware = record["hardware"].toObject();
        monitor.inputProfile = monitor.hardware["input_profile"].toString();
        result.monitors.push_back(monitor);
    }
    return result;
}
Discovery MergeWindowsDiscovery(Discovery amd, const Discovery &native)
{
    for (const auto &monitor : native.monitors)
    {
        int amdMatches = 0, nativeMatches = 0;
        for (const auto &candidate : amd.monitors)
            if (candidate.transport.isEmpty() && !monitor.identity.isEmpty() &&
                candidate.identity == monitor.identity)
                ++amdMatches;
        for (const auto &candidate : native.monitors)
            if (candidate.identity == monitor.identity)
                ++nativeMatches;
        // Keep AMD raw access for uniquely matched AMD monitors. Unmatched
        // DisplayLink/Intel/NVIDIA routes use Windows standard DDC/CI instead.
        if (amdMatches == 1 && nativeMatches == 1 && monitor.hardware["adapter_kind"] == "amd")
            continue;
        amd.monitors.push_back(monitor);
    }
    if (!amd.monitors.isEmpty())
        amd.message = QString("%1 monitor connection(s) available. Dock connections use standard DDC/CI.")
                          .arg(amd.monitors.size());
    return amd;
}
lg::SwitchResult DispatchNativeWindowsInput(const DesktopConfig &config, const QString &code,
                                          const Discovery &live, const NativeMonitorWriter &write)
{
    if (config.method == "lg")
        return {lg::SwitchStatus::InvalidArgument, 50,
                "This connection uses Windows DDC/CI, which cannot send LG alternate commands with a "
                "custom source address. Use a direct AMD connection for LG alternate switching. "
                "Standard DDC/CI can be selected and tested if this monitor supports it."};
    unsigned int value;
    if (config.transport != WindowsDdcTransport || config.method != "standard" ||
        NativeWindowsDisplayNumber(config.nativeDisplay) < 0 ||
        !QRegularExpression("^[0-9a-f]{64}$").match(config.targetIdentity).hasMatch() ||
        !ParseInputValue(code.toStdString(), 0xffff, value) || !write)
        return {lg::SwitchStatus::InvalidArgument, 87, "Invalid Windows monitor selection or input."};
    QVector<DesktopMonitor> matches;
    for (const auto &monitor : live.monitors)
        if (monitor.transport == WindowsDdcTransport && monitor.nativeDisplay == config.nativeDisplay &&
            monitor.identity == config.targetIdentity)
            matches.push_back(monitor);
    if (matches.size() != 1)
        return {lg::SwitchStatus::TransportFailure, 1167,
                "Windows monitor disconnected, changed or ambiguous. Refresh and reconnect it."};
    return write(matches.front(), value);
}

#ifdef Q_OS_WIN
#include <windows.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <setupapi.h>
#include <vector>

namespace
{
QByteArray InterfaceEdid(const wchar_t *path)
{
    const auto set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE)
        return {};
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    QByteArray edid;
    if (SetupDiOpenDeviceInterfaceW(set, path, 0, &interfaceData))
    {
        DWORD bytes = 0;
        SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, nullptr, 0, &bytes, nullptr);
        if (bytes >= sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) && bytes <= 65536)
        {
            std::vector<unsigned char> buffer(bytes);
            auto *detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
            detail->cbSize = sizeof(*detail);
            if (SetupDiGetDeviceInterfaceDetailW(set, &interfaceData, detail, bytes, nullptr, &device))
            {
                const auto key = SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
                if (key != INVALID_HANDLE_VALUE)
                {
                    DWORD length = 0, type = 0;
                    if (RegQueryValueExW(key, L"EDID", nullptr, &type, nullptr, &length) == ERROR_SUCCESS &&
                        type == REG_BINARY && length >= 128 && length <= 16384)
                    {
                        edid.resize(int(length));
                        if (RegQueryValueExW(key, L"EDID", nullptr, &type,
                                            reinterpret_cast<BYTE *>(edid.data()), &length) != ERROR_SUCCESS ||
                            type != REG_BINARY)
                            edid.clear();
                        else
                            edid.resize(int(length));
                    }
                    RegCloseKey(key);
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return edid;
}
struct NativeRecord
{
    DesktopMonitor target;
    HMONITOR handle = nullptr;
};
struct Inventory
{
    QVector<NativeRecord> records;
    int visited = 0;
};
QVector<DISPLAY_DEVICEW> ActiveDevices(const wchar_t *name)
{
    QVector<DISPLAY_DEVICEW> devices;
    for (DWORD index = 0; index < 16; ++index)
    {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(name, index, &device, EDD_GET_DEVICE_INTERFACE_NAME))
            break;
        if (device.StateFlags & DISPLAY_DEVICE_ACTIVE)
            devices.push_back(device);
    }
    return devices;
}
QString AdapterKind(const QString &display)
{
    for (DWORD index = 0; index < 32; ++index)
    {
        DISPLAY_DEVICEW adapter{};
        adapter.cb = sizeof(adapter);
        if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0))
            break;
        if (QString::fromWCharArray(adapter.DeviceName) != display)
            continue;
        const auto id = QString::fromWCharArray(adapter.DeviceID).toUpper();
        const auto name = QString::fromWCharArray(adapter.DeviceString);
        if (name.contains("DisplayLink", Qt::CaseInsensitive) || id.contains("VID_17E9"))
            return "displaylink";
        return id.startsWith("PCI\\VEN_1002&") ? "amd" : "other";
    }
    return "other";
}
BOOL CALLBACK ReadDisplay(HMONITOR handle, HDC, LPRECT, LPARAM raw)
{
    auto &inventory = *reinterpret_cast<Inventory *>(raw);
    if (++inventory.visited > 32)
        return FALSE;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(handle, reinterpret_cast<MONITORINFO *>(&info)))
        return TRUE;
    const auto devices = ActiveDevices(info.szDevice);
    if (devices.size() != 1 || devices.front().DeviceID[0] == L'\0')
        return TRUE; // No physical identity, or a clone group that cannot be targeted safely.
    const auto &device = devices.front();
    const auto edid = InterfaceEdid(device.DeviceID);
    auto identity = MonitorIdentity(edid);
    if (identity.isEmpty())
        identity = QString::fromLatin1(QCryptographicHash::hash(
            QString::fromWCharArray(device.DeviceID).toUtf8(), QCryptographicHash::Sha256).toHex());
    auto label = MonitorNameFromEdid(edid);
    if (label.isEmpty())
        label = QString::fromWCharArray(device.DeviceString).left(100);
    if (label.isEmpty())
        label = "Monitor";
    const auto name = QString::fromWCharArray(info.szDevice);
    const int number = NativeWindowsDisplayNumber(name);
    if (number < 0)
        return TRUE;
    DesktopMonitor target{label + QString(" · Display %1 · Standard DDC/CI").arg(number),
                          -1, number, false, identity};
    target.transport = WindowsDdcTransport;
    target.nativeDisplay = name;
    target.hardware = MonitorEdidHardware(edid);
    target.hardware["adapter_kind"] = AdapterKind(name);
    target.inputProfile = target.hardware["input_profile"].toString();
    inventory.records.push_back({target, handle});
    return TRUE;
}
Inventory ReadInventory()
{
    Inventory result;
    if (!EnumDisplayMonitors(nullptr, nullptr, ReadDisplay, reinterpret_cast<LPARAM>(&result)))
        result.records.clear();
    return result;
}
struct PhysicalMonitor
{
    PHYSICAL_MONITOR value{};
    bool acquired = false;
    ~PhysicalMonitor()
    {
        if (acquired)
            DestroyPhysicalMonitors(1, &value);
    }
    bool acquire(HMONITOR handle)
    {
        DWORD count = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(handle, &count) || count != 1)
            return false;
        acquired = bool(GetPhysicalMonitorsFromHMONITOR(handle, 1, &value));
        return acquired;
    }
};
const NativeRecord *Selected(const Inventory &inventory, const QString &display, const QString &identity)
{
    const NativeRecord *selected = nullptr;
    for (const auto &record : inventory.records)
        if ((display.isEmpty() || record.target.nativeDisplay == display) && record.target.identity == identity)
        {
            if (selected)
                return nullptr;
            selected = &record;
        }
    return selected;
}
} // namespace
QJsonObject ProbeNativeWindowsDiscovery()
{
    QJsonArray monitors;
    for (const auto &record : ReadInventory().records)
    {
        const auto &target = record.target;
        monitors.append(QJsonObject{{"label", target.label}, {"identity", target.identity},
                                    {"windows_display", target.nativeDisplay}, {"hardware", target.hardware}});
    }
    return {{"monitors", monitors}};
}
QJsonObject NativeWindowsHardware(const QString &display)
{
    const auto devices = ActiveDevices(reinterpret_cast<const wchar_t *>(display.utf16()));
    auto hardware = devices.size() == 1 ? MonitorEdidHardware(InterfaceEdid(devices[0].DeviceID))
                                        : QJsonObject{{"edid_valid", false}};
    hardware["adapter_kind"] = AdapterKind(display);
    return SafeMonitorHardware(hardware);
}
QJsonObject ProbeNativeWindowsCapabilities(const QString &display, const QString &identity)
{
    const auto inventory = ReadInventory();
    const auto *record = Selected(inventory, display, identity);
    if (!record)
        return {{"read", false}, {"status", "ambiguous or unavailable native mapping"}};
    const auto hardware = record->target.hardware;
    QJsonObject result{{"read", false}, {"hardware", hardware}};
    PhysicalMonitor physical;
    if (physical.acquire(record->handle))
    {
        DWORD length = 0;
        if (GetCapabilitiesStringLength(physical.value.hPhysicalMonitor, &length) && length > 0 && length <= 65536)
        {
            QByteArray text(int(length), '\0');
            if (CapabilitiesRequestAndCapabilitiesReply(physical.value.hPhysicalMonitor, text.data(), length))
            {
                const int end = text.indexOf('\0');
                if (end >= 0)
                {
                    result = ParseDiagnosticCapabilities(text.left(end));
                    result["read"] = true;
                    result["hardware"] = hardware;
                }
            }
        }
    }
    result["manufacturer"] = hardware["manufacturer"];
    if (!hardware["input_profile"].toString().isEmpty())
        result["input_profile"] = hardware["input_profile"];
    return result;
}
QJsonObject ProbeNativeWindowsSwitch(const QString &display, const QString &identity, const QString &code)
{
    DesktopConfig config;
    config.transport = WindowsDdcTransport;
    config.method = "standard";
    config.nativeDisplay = display;
    config.targetIdentity = identity;
    const auto inventory = ReadInventory();
    Discovery live;
    for (const auto &record : inventory.records)
        live.monitors.push_back(record.target);
    const auto result = DispatchNativeWindowsInput(config, code, live,
        [&](const DesktopMonitor &target, unsigned int value) -> lg::SwitchResult {
            const auto *record = Selected(inventory, target.nativeDisplay, target.identity);
            PhysicalMonitor physical;
            if (!record || !physical.acquire(record->handle))
                return {lg::SwitchStatus::TransportFailure, 50, "Windows physical monitor is unavailable or ambiguous."};
            if (!SetVCPFeature(physical.value.hPhysicalMonitor, 0x60, value))
                return {lg::SwitchStatus::TransportFailure, int(GetLastError()), "Windows DDC/CI input command failed."};
            return {lg::SwitchStatus::CommandSent, 0, "Windows DDC/CI command sent; physical switch unverified."};
        });
    return {{"sent", result.sent()}, {"native_code", result.nativeCode}, {"detail", QString::fromStdString(result.detail)}};
}
#endif
