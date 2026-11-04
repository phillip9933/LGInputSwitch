#include "monitor_inputs.h"
#include "../core/input_value.h"
#include "diagnostics.h"
#include "input_models.h"
#include "windows_monitors.h"
#include "process.h"
#ifdef Q_OS_WIN
#include "../platform/windows/adl_session.h"
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
    const auto defaults = DefaultMonitorInputs(monitor.method, monitor.route.inputProfile);
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
static MonitorInputRead AdvertisedMonitorInputs(const SavedMonitor &m, const QJsonObject &caps)
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
MonitorInputRead InputsFromCapabilities(const SavedMonitor &m, const QJsonObject &caps)
{
    auto result = AdvertisedMonitorInputs(m, caps);
    const bool thunderbolt = m.method == "lg" &&
        (m.route.inputProfile == Lg40U990AInputProfile ||
         (caps["read"].toBool() && caps["parse_status"] == "parsed" &&
          caps["input_profile"] == Lg40U990AInputProfile));
    if (!thunderbolt)
        return result;
    // This model's tested 0xD2 port may be omitted from capabilities altogether.
    // An unavailable/incomplete list must not discard any existing inputs.
    const bool advertised = result.complete;
    if (!advertised)
        result.inputs = m.inputs;
    if (!ByValue(result.inputs, 0xd2))
    {
        if (result.inputs.size() >= 64)
        {
            result.complete = false;
            result.status = "Thunderbolt detected; input limit reached. Existing mappings kept.";
            return result;
        }
        result.inputs.push_back({"Thunderbolt", "0xd2", {}, true, {}});
    }
    for (auto &input : result.inputs)
    {
        unsigned int value;
        if (Value(input.code, value) && value == 0xd2 &&
            (input.label.isEmpty() || input.label.startsWith("Input 0x")))
            input.label = "Thunderbolt";
    }
    result.complete = true;
    result.status = advertised
        ? "Inputs read; Thunderbolt (0xD2) added from the LG 40U990A-W model profile."
        : "LG 40U990A-W model profile: Thunderbolt (0xD2) available; existing inputs kept.";
    return result;
}
QJsonObject ReadMonitorCapabilities(const DesktopMonitor &target)
{
#ifdef Q_OS_WIN
    const QStringList arguments = target.transport == WindowsDdcTransport
        ? QStringList{"--windows-input-probe", target.nativeDisplay, target.identity}
        : QStringList{"--input-probe", QString::number(target.adapter), QString::number(target.display), target.identity};
    const auto process = RunDesktopProcess(QCoreApplication::applicationFilePath(), arguments, 15000);
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
            if (FindMonitorInput(*m, action.inputs.value(id)))
                continue;
            const auto before = action.inputs.value(id);
            QString missing = action.missingInputs.value(id);
            if (missing.isEmpty())
                for (const auto &old : oldInputs)
                    if (old.id == before)
                        missing = old.code;
            unsigned int code;
            const auto *match = Value(missing, code) ? ByValue(m->inputs, code) : nullptr;
            if (match)
            {
                action.inputs[id] = match->id;
                action.missingInputs.remove(id);
            }
            else
            {
                if (!missing.isEmpty())
                    action.missingInputs[id] = missing;
                action.inputs[id].clear();
                if (action.enabled)
                    action.autoDisabled = true;
                action.enabled = false;
            }
            changed = changed || before != action.inputs.value(id);
        }
        if (profile.monitors.size() != 1)
            continue;
        for (const auto &input : m->inputs)
        {
            bool exists = false;
            for (const auto &action : profile.actions)
                if (action.inputs.value(id) == input.id)
                    exists = true;
            if (!exists && profile.actions.size() < 64)
            {
                QString name = input.label;
                auto used = [&](const QString &label) {
                    for (const auto &action : profile.actions)
                        if (action.name == label) return true;
                    return false;
                };
                for (int suffix = 2; used(name); ++suffix)
                    name = input.label + QString(" (%1)").arg(suffix);
                profile.actions.push_back({NewDesktopId(), name, {}, true, {{id, input.id}}});
                changed = true;
            }
        }
    }
    // Restore only actions disabled by discovery, once every group member has
    // its mapping back. Never reactivate an intentionally disabled action or
    // steal a shortcut assigned elsewhere while this input was absent.
    for (auto &profile : w.profiles)
        for (auto &action : profile.actions)
            if (action.autoDisabled && action.missingInputs.isEmpty())
            {
                bool complete = true;
                for (const auto &member : profile.monitors)
                {
                    const auto *monitor = FindMonitor(w, member);
                    complete = complete && monitor && FindMonitorInput(*monitor, action.inputs.value(member));
                }
                if (!complete) continue;
                action.enabled = true;
                action.autoDisabled = false;
                QString error;
                if (!ValidateWorkspace(w, error))
                {
                    action.enabled = false;
                    action.autoDisabled = true;
                    m->inputStatus += " An action needs review in Edit actions: " + error;
                }
                else
                    changed = true;
            }
    return changed;
}

#ifdef Q_OS_WIN
QJsonObject ProbeTargetCapabilities(int adapter, int display, const QString &expectedIdentity)
{
    if (adapter < 0 || display < 0 || !InitializeAdl() || !Adl().edid)
        return {{"read", false}};
    ADLDisplayEDIDData data{};
    data.iSize = sizeof(data);
    if (Adl().edid(adapter, display, &data) != ADL_OK || data.iEDIDSize < 128 ||
        data.iEDIDSize > ADL_MAX_EDIDDATA_SIZE)
        return {{"read", false}};
    const QByteArray edid(data.cEDIDData, data.iEDIDSize);
    const auto identity = MonitorIdentity(edid);
    if (identity.isEmpty() || (!expectedIdentity.isEmpty() && identity != expectedIdentity))
        return {{"read", false}};
    auto caps = ProbeNativeWindowsCapabilities({}, identity);
    const auto hardware = MonitorEdidHardware(edid);
    caps["hardware"] = hardware;
    caps["manufacturer"] = hardware["manufacturer"];
    if (!hardware["input_profile"].toString().isEmpty())
        caps["input_profile"] = hardware["input_profile"];
    return caps;
}
#endif
