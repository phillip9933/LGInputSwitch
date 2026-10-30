#include "monitor_inputs.h"
#include "../core/input_value.h"
#include "diagnostics.h"
#include "process.h"
#ifdef Q_OS_WIN
#include "../platform/windows/adl_session.h"
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <setupapi.h>
#include <vector>
#include <windows.h>
#endif

namespace
{
bool Value(const QString &text, unsigned int &value)
{
    return ParseInputValue(text.toStdString(), 0xffff, value);
}
QString Hex(unsigned int value)
{
    return QString("0x%1").arg(value, 2, 16, QChar('0'));
}
const DesktopInput *ByValue(const QVector<DesktopInput> &inputs, unsigned int value)
{
    for (const auto &input : inputs)
    {
        unsigned int current;
        if (Value(input.code, current) && current == value)
            return &input;
    }
    return nullptr;
}
QVector<DesktopInput> StableInputs(const SavedMonitor &monitor, const QVector<DesktopInput> &found)
{
    QVector<DesktopInput> result;
    for (auto input : found)
    {
        unsigned int value;
        if (!Value(input.code, value))
            continue;
        if (const auto *old = ByValue(monitor.inputs, value))
        {
            input.id = old->id;
            // Preserve custom names, but improve generic vendor/numeric labels.
            if (!old->label.startsWith("Input 0x") && !old->label.isEmpty())
                input.label = old->label;
        }
        else
            input.id = NewDesktopId();
        result.push_back(input);
    }
    return result;
}
} // namespace
QString InputNameKey(QString label)
{
    label = label.toLower().remove(' ').remove('-').remove('_');
    if (label == "displayport" || label == "dp" || label == "dp1")
        return "displayport1";
    if (label == "dp2")
        return "displayport2";
    if (label == "hdmi")
        return "hdmi1";
    return label;
}
bool IsPresetInputList(const SavedMonitor &monitor)
{
    if (monitor.inputs.isEmpty())
        return true;
    const auto defaults = DefaultMonitorInputs(monitor.method);
    for (const auto &input : monitor.inputs)
    {
        unsigned int value;
        if (!Value(input.code, value))
            return false;
        const auto *preset = ByValue(defaults, value);
        if (!preset || InputNameKey(preset->label) != InputNameKey(input.label))
            return false;
    }
    return true;
}
MonitorInputRead InputsFromCapabilities(const SavedMonitor &m, const QJsonObject &caps)
{
    MonitorInputRead result;
    if (!caps["read"].toBool() || caps["parse_status"] != "parsed")
    {
        result.status = "Input list unavailable; existing mappings kept.";
        return result;
    }
    const auto lists = caps["advertised_input_values"].toObject();
    const auto feature = m.method == "lg" ? "0xf4" : "0x60";
    auto values = lists[feature].toObject()["values"].toArray();
    bool derived = false;
    if (m.method == "lg" && values.isEmpty())
    {
        // LG often lists the physical ports in 60 but omits F4's values. Only
        // retain alternate codes already in this monitor's known/preset mapping;
        // never apply a single USB-C code to every LG model.
        const auto standard = lists["0x60"].toObject()["values"].toArray();
        if (!standard.isEmpty())
        {
            for (const auto &item : standard)
            {
                unsigned int code, alternate;
                if (!Value(item.toObject()["code"].toString(), code))
                    return result;
                switch (code)
                {
                case 0x0f:
                    alternate = 0xd0;
                    break;
                case 0x10:
                    alternate = 0xd1;
                    break;
                case 0x11:
                    alternate = 0x90;
                    break;
                case 0x12:
                    alternate = 0x91;
                    break;
                default:
                    result.status = "LG input list cannot be fully mapped; existing mappings kept.";
                    return result;
                }
                const auto *known = ByValue(m.inputs, alternate);
                if (!known)
                {
                    result.status = "LG alternate codes are incomplete; existing mappings kept.";
                    return result;
                }
                values.append(QJsonObject{{"code", Hex(alternate)}, {"name", known->label}});
            }
            derived = true;
        }
    }
    if (values.isEmpty())
    {
        result.status = "Monitor did not advertise an input list; existing mappings kept.";
        return result;
    }
    const bool dell = m.route.label.startsWith("DELL", Qt::CaseInsensitive) ||
                      m.name.startsWith("DELL", Qt::CaseInsensitive) || caps["manufacturer"] == "DEL";
    QSet<unsigned int> seen;
    for (const auto &item : values)
    {
        const auto entry = item.toObject();
        unsigned int value;
        if (!Value(entry["code"].toString(), value) || seen.contains(value))
            continue;
        seen.insert(value);
        QString name = derived ? entry["name"].toString() : entry["standard_name"].toString();
        // 1B is a Dell convention, not a universal MCCS USB-C assignment.
        if (m.method == "standard" && dell && value == 0x1b)
            name = "USB-C";
        if (name.isEmpty())
        {
            if (const auto *old = ByValue(m.inputs, value))
                name = old->label;
            else
                name = "Input " + Hex(value);
        }
        result.inputs.push_back({name, Hex(value), {}, true, {}});
    }
    result.complete = !result.inputs.isEmpty() && result.inputs.size() <= 64;
    if (!result.complete)
    {
        result.inputs.clear();
        result.status = "Input list exceeds supported limits; existing mappings kept.";
        return result;
    }
    result.status = derived ? "Inputs read from the monitor; configured LG alternate codes retained."
                            : "Inputs read from this monitor’s advertised capabilities.";
    return result;
}
QJsonObject ReadMonitorCapabilities(const DesktopMonitor &target)
{
#ifdef Q_OS_WIN
    const auto process = RunDesktopProcess(
        QCoreApplication::applicationFilePath(),
        {"--input-probe", QString::number(target.adapter), QString::number(target.display), target.identity},
        15000);
    if (process.status == "finished" && process.exitCode == 0)
    {
        const auto doc = QJsonDocument::fromJson(process.output);
        if (doc.isObject())
            return doc.object();
    }
    return {{"read", false}, {"status", process.status}};
#else
    if (target.display < 0 || (!target.bus && target.display == 0))
        return {{"read", false}};
    const auto process = RunDesktopProcess(
        "ddcutil",
        {target.bus ? "--bus" : "--display", QString::number(target.display), "capabilities", "--terse"},
        8000);
    if (process.status != "finished" || process.exitCode != 0)
        return {{"read", false}, {"status", process.status}};
    auto caps = ParseDiagnosticCapabilities(process.output);
    caps["read"] = true;
    return caps;
#endif
}
MonitorInputRead ReadMonitorInputs(const SavedMonitor &m, const DesktopMonitor &target)
{
    auto selected = m;
    selected.route = target;
    return InputsFromCapabilities(selected, ReadMonitorCapabilities(target));
}
bool ApplyMonitorInputs(DesktopWorkspace &w, const QString &id, const MonitorInputRead &read)
{
    SavedMonitor *m = nullptr;
    for (auto &candidate : w.monitors)
        if (candidate.id == id)
            m = &candidate;
    if (!m)
        return false;
    m->inputStatus = read.status;
    if (!read.complete || !m->autoInputs)
        return false;
    const auto oldInputs = m->inputs;
    const auto next = StableInputs(*m, read.inputs);
    if (next.isEmpty())
        return false;
    bool changed = oldInputs.size() != next.size();
    if (!changed)
        for (int i = 0; i < next.size(); ++i)
            if (oldInputs[i].id != next[i].id || oldInputs[i].label != next[i].label ||
                oldInputs[i].code.toLower() != next[i].code.toLower())
                changed = true;
    m->inputs = next;
    for (auto &profile : w.profiles)
    {
        if (!profile.monitors.contains(id))
            continue;
        for (auto &action : profile.actions)
        {
            if (!FindMonitorInput(*m, action.inputs.value(id)))
            {
                const auto before = action.inputs.value(id);
                const bool wasEnabled = action.enabled;
                const DesktopInput *match = nullptr;
                for (const auto &input : m->inputs)
                    if (InputNameKey(action.name) == InputNameKey(input.label))
                        match = &input;
                if (match)
                    action.inputs[id] = match->id;
                else
                {
                    action.inputs[id] = {};
                    action.enabled = false;
                }
                changed = changed || before != action.inputs.value(id) || wasEnabled != action.enabled;
            }
        }
        if (profile.monitors.size() != 1)
            continue;
        for (const auto &input : m->inputs)
        {
            bool exists = false;
            for (const auto &action : profile.actions)
                if (action.inputs.value(id) == input.id)
                    exists = true;
            if (exists)
                continue;
            // Restore a matching retired action without discarding its key/ID.
            bool restored = false;
            for (auto &action : profile.actions)
                if (!action.enabled && action.inputs.value(id).isEmpty() &&
                    InputNameKey(action.name) == InputNameKey(input.label))
                {
                    action.inputs[id] = input.id;
                    restored = true;
                    changed = true;
                    break;
                }
            if (!restored && profile.actions.size() < 64)
            {
                profile.actions.push_back({NewDesktopId(), input.label, {}, true, {{id, input.id}}});
                changed = true;
            }
        }
        bool enabled = false;
        for (const auto &action : profile.actions)
            if (action.enabled)
                enabled = true;
        if (!enabled)
            for (auto &action : profile.actions)
                if (FindMonitorInput(*m, action.inputs.value(id)))
                {
                    action.enabled = true;
                    break;
                }
    }
    return changed;
}

#ifdef Q_OS_WIN
namespace
{
QString InterfaceIdentity(const wchar_t *path)
{
    const auto set = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (set == INVALID_HANDLE_VALUE)
        return {};
    SP_DEVICE_INTERFACE_DATA interfaceData{};
    interfaceData.cbSize = sizeof(interfaceData);
    SP_DEVINFO_DATA device{};
    device.cbSize = sizeof(device);
    QString identity;
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
                const auto key =
                    SetupDiOpenDevRegKey(set, &device, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_QUERY_VALUE);
                if (key != INVALID_HANDLE_VALUE)
                {
                    DWORD length = 0, type = 0;
                    if (RegQueryValueExW(key, L"EDID", nullptr, &type, nullptr, &length) == ERROR_SUCCESS &&
                        type == REG_BINARY && length >= 128 && length <= 16384)
                    {
                        QByteArray edid(int(length), '\0');
                        if (RegQueryValueExW(key, L"EDID", nullptr, &type,
                                             reinterpret_cast<BYTE *>(edid.data()),
                                             &length) == ERROR_SUCCESS &&
                            type == REG_BINARY)
                            identity = MonitorIdentity(edid.left(int(length)));
                    }
                    RegCloseKey(key);
                }
            }
        }
    }
    SetupDiDestroyDeviceInfoList(set);
    return identity;
}
struct TargetProbe
{
    QString identity;
    int matches = 0, displays = 0;
    QJsonObject caps;
};
BOOL CALLBACK TargetDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM raw)
{
    auto &probe = *reinterpret_cast<TargetProbe *>(raw);
    if (++probe.displays > 32)
        return FALSE;
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)))
        return TRUE;
    QStringList identities;
    for (DWORD index = 0; index < 16; ++index)
    {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(info.szDevice, index, &device, EDD_GET_DEVICE_INTERFACE_NAME))
            break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ACTIVE))
            continue;
        identities.push_back(InterfaceIdentity(device.DeviceID));
    }
    // Neither physical-handle ordering nor an AMD index is a Windows index.
    if (identities.size() != 1 || identities.front() != probe.identity)
        return TRUE;
    ++probe.matches;
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count) || count != 1)
        return TRUE;
    PHYSICAL_MONITOR physical{};
    if (!GetPhysicalMonitorsFromHMONITOR(monitor, 1, &physical))
        return TRUE;
    DWORD length = 0;
    if (GetCapabilitiesStringLength(physical.hPhysicalMonitor, &length) && length > 0 && length <= 65536)
    {
        QByteArray text(int(length), '\0');
        if (CapabilitiesRequestAndCapabilitiesReply(physical.hPhysicalMonitor, text.data(), length))
        {
            const auto end = text.indexOf('\0');
            if (end >= 0)
            {
                probe.caps = ParseDiagnosticCapabilities(text.left(end));
                probe.caps["read"] = true;
            }
        }
    }
    DestroyPhysicalMonitors(1, &physical);
    return TRUE;
}
} // namespace
QJsonObject ProbeTargetCapabilities(int adapter, int display, const QString &expectedIdentity)
{
    if (adapter < 0 || display < 0 || !InitializeAdl() || !Adl().edid)
        return {{"read", false}};
    ADLDisplayEDIDData data{};
    data.iSize = sizeof(data);
    if (Adl().edid(adapter, display, &data) != ADL_OK || data.iEDIDSize < 128 ||
        data.iEDIDSize > ADL_MAX_EDIDDATA_SIZE)
        return {{"read", false}};
    const auto identity = MonitorIdentity(QByteArray(data.cEDIDData, data.iEDIDSize));
    if (identity.isEmpty() || (!expectedIdentity.isEmpty() && identity != expectedIdentity))
        return {{"read", false}};
    TargetProbe probe;
    probe.identity = identity;
    EnumDisplayMonitors(nullptr, nullptr, TargetDisplay, reinterpret_cast<LPARAM>(&probe));
    if (probe.matches != 1 || probe.caps.isEmpty())
        return {{"read", false}, {"status", "ambiguous or unavailable native mapping"}};
    const unsigned int manufacturer =
        (static_cast<unsigned char>(data.cEDIDData[8]) << 8) | static_cast<unsigned char>(data.cEDIDData[9]);
    if (manufacturer == 0x10ac)
        probe.caps["manufacturer"] = "DEL";
    return probe.caps;
}
#endif
