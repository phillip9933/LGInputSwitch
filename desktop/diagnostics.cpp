#include "diagnostics.h"
#include "../core/input_value.h"
#include "process.h"
#ifndef LG_BUILD_REVISION
#define LG_BUILD_REVISION "unavailable"
#endif
#ifdef Q_OS_WIN
#include <lowlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <vector>
#include <windows.h>
#endif

QJsonObject ParseDiagnosticVcp(const QByteArray &output, unsigned int feature)
{
    // ddcutil terse output: VCP 10 C <current> <maximum>, or VCP 60 SNC xNN.
    // Do not copy raw output: it can include serial numbers, paths or unrelated displays.
    const QString code = QString::number(feature, 16).rightJustified(2, '0');
    const QRegularExpression pattern(
        "^VCP\\s+" + code + "\\s+(C|SNC)\\s+(0x[0-9a-f]+|x[0-9a-f]+|[0-9]+)(?:\\s+([0-9]+))?\\s*$",
        QRegularExpression::CaseInsensitiveOption);
    for (const auto &line : QString::fromUtf8(output).split('\n'))
    {
        const auto match = pattern.match(line.trimmed());
        if (!match.hasMatch())
            continue;
        QJsonObject value{{"read", true}, {"value", match.captured(2)}};
        if (match.captured(1).compare("C", Qt::CaseInsensitive) == 0 && !match.captured(3).isEmpty())
            value["maximum"] = match.captured(3);
        return value;
    }
    return {{"read", false}};
}

QJsonObject CollectDesktopDiagnostics(const DiagnosticSnapshot &snapshot, bool preview)
{
    if (preview)
        return {{"status", "Design preview; no settings or hardware accessed."},
                {"capabilities", ParseDiagnosticCapabilities("(vcp(10 12 60(0f 11 12) f4)mccs_ver(2.2))")}};
#ifdef Q_OS_WIN
    Q_UNUSED(snapshot);
    // A hung display driver cannot hold the UI or application shutdown indefinitely.
    const auto result =
        RunDesktopProcess(QCoreApplication::applicationFilePath(), {"--diagnostic-probe"}, 20000, 262144);
    QJsonObject checks{{"probe_status", result.status}, {"probe_exit_code", result.exitCode}};
    if (result.status == "finished" && result.exitCode == 0)
    {
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(result.output, &error);
        if (error.error == QJsonParseError::NoError && document.isObject())
            checks["windows"] = document.object();
        else
            checks["probe_status"] = "unreadable response";
    }
    return checks;
#else
    if (!snapshot.monitors.isEmpty())
    {
        QJsonArray monitors;
        for (const auto &monitor : snapshot.monitors)
        {
            DiagnosticSnapshot single{monitor.config, monitor.selected, snapshot.unsaved, {}};
            monitors.append(CollectDesktopDiagnostics(single, false));
        }
        return {{"monitors", monitors}};
    }
    QJsonObject checks{{"backend", "ddcutil"}};
    const auto version = RunDesktopProcess("ddcutil", {"--version"}, 3000);
    checks["backend_status"] = version.status;
    if (version.status == "unavailable")
        return checks;
    const auto match = QRegularExpression("ddcutil\\s+([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)")
                           .match(QString::fromUtf8(version.output));
    if (match.hasMatch())
        checks["backend_version"] = match.captured(1);
    if (!snapshot.selected || snapshot.config.display < 0 ||
        (!snapshot.config.bus && snapshot.config.display == 0))
    {
        checks["monitor_reads"] = "Skipped: select a monitor first.";
        return checks;
    }
    QJsonObject controls;
    for (unsigned int feature : {0x10u, 0x60u})
    {
        const QString code = QString::number(feature, 16);
        const auto result =
            RunDesktopProcess("ddcutil",
                              {snapshot.config.bus ? "--bus" : "--display",
                               QString::number(snapshot.config.display), "getvcp", code, "--terse"},
                              5000);
        auto value = ParseDiagnosticVcp(result.output, feature);
        if (result.status != "finished" || result.exitCode != 0)
            value = {{"read", false}};
        value["process_status"] = result.status;
        value["exit_code"] = result.exitCode;
        if (result.output.contains("Permission denied"))
            value["hint"] = "Check I2C device access permissions.";
        controls["0x" + code] = value;
    }
    checks["standard_vcp_reads"] = controls;
    const auto capabilities =
        RunDesktopProcess("ddcutil",
                          {snapshot.config.bus ? "--bus" : "--display",
                           QString::number(snapshot.config.display), "capabilities", "--terse"},
                          10000);
    QJsonObject advertised{{"read", false}};
    if (capabilities.status == "finished" && capabilities.exitCode == 0)
    {
        advertised = ParseDiagnosticCapabilities(capabilities.output);
        advertised["read"] = true;
    }
    advertised["process_status"] = capabilities.status;
    advertised["exit_code"] = capabilities.exitCode;
    checks["capabilities"] = advertised;
    return checks;
#endif
}

QString FormatDesktopDiagnostics(const DiagnosticSnapshot &snapshot, const QJsonObject &checks, bool preview)
{
    QJsonObject report{
        {"report_format", 1},
        {"application", "LG Input Switch"},
        {"application_version", QCoreApplication::applicationVersion()},
        {"build_revision", LG_BUILD_REVISION},
        {"qt_version", QString::fromLatin1(qVersion())},
        {"os", QSysInfo::prettyProductName()},
        {"kernel_version", QSysInfo::kernelVersion()},
        {"architecture", QSysInfo::currentCpuArchitecture()},
        {"preview", preview},
        {"settings_unsaved", snapshot.unsaved},
        {"privacy", "No raw EDID/capability strings, serial numbers, usernames, hostnames, personal file "
                    "paths or shortcuts exported. "
                    "Nothing uploaded. Review before sharing."},
        {"limits", "Read-only standard brightness (0x10) and input (0x60) queries do not prove support "
                   "for LG 0xF4/source-0x50 switching. Command sent does not confirm a physical switch."},
        {"checks", checks}};
    QJsonObject target{{"selected_in_app", snapshot.selected}};
    if (snapshot.selected)
    {
        target["adapter"] = snapshot.config.adapter;
        target[snapshot.config.bus ? "i2c_bus" : "display"] = snapshot.config.display;
    }
    report["target"] = target;
    if (!snapshot.monitors.isEmpty())
    {
        report["report_format"] = 2;
        report["profile_count"] = snapshot.profileCount;
        report["profile_actions"] = snapshot.actions;
        QJsonArray monitors;
        for (const auto &monitor : snapshot.monitors)
        {
            DiagnosticSnapshot single{monitor.config, monitor.selected, snapshot.unsaved, {}};
            const auto normalized =
                QJsonDocument::fromJson(FormatDesktopDiagnostics(single, {}, preview).toUtf8()).object();
            monitors.append(QJsonObject{{"target", normalized["target"]},
                                        {"source_address", normalized["source_address"]},
                                        {"feature", monitor.config.method == "standard" ? "0x60" : "0xf4"},
                                        {"monitor_index", monitor.monitorIndex},
                                        {"configured_inputs", normalized["configured_inputs"]}});
        }
        report["profile_monitors"] = monitors;
    }
    // Values are parsed and normalized; user labels and arbitrary strings are never exported.
    unsigned int source;
    report["source_address"] =
        ParseInputValue(
            (snapshot.config.method == "standard" ? QString("0x51") : snapshot.config.source).toStdString(),
            0xff, source)
            ? QJsonValue(QString("0x%1").arg(source, 2, 16, QChar('0')))
            : QJsonValue("invalid");
    report["feature"] = snapshot.config.method == "standard" ? "0x60" : "0xf4";
    QJsonArray inputs;
    for (const auto &input : snapshot.config.inputs)
    {
        unsigned int code;
        inputs.append(QJsonObject{{"enabled", input.enabled},
                                  {"code", ParseInputValue(input.code.toStdString(), 0xffff, code)
                                               ? QJsonValue(QString("0x%1").arg(code, 4, 16, QChar('0')))
                                               : QJsonValue("invalid")}});
    }
    report["configured_inputs"] = inputs;
    report["last_command"] = snapshot.lastCommand.isEmpty()
                                 ? QJsonValue("No input command attempted in this session.")
                                 : QJsonValue(snapshot.lastCommand);
    return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
}

bool SaveDesktopDiagnostics(const QString &path, const QString &report, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
    {
        error = file.errorString();
        return false;
    }
    const auto bytes = report.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
    {
        error = file.errorString();
        return false;
    }
    return true;
}

#ifdef Q_OS_WIN
namespace
{
QJsonObject ReadWindowsVcp(HANDLE monitor, BYTE feature)
{
    DWORD current = 0;
    if (GetVCPFeatureAndVCPFeatureReply(monitor, feature, nullptr, &current, nullptr))
        return {{"read", true}, {"value", static_cast<qint64>(current)}};
    return {{"read", false}, {"windows_error", static_cast<qint64>(GetLastError())}};
}
QJsonObject ReadWindowsCapabilities(HANDLE monitor)
{
    DWORD length = 0;
    if (!GetCapabilitiesStringLength(monitor, &length))
        return {{"read", false}, {"windows_error", static_cast<qint64>(GetLastError())}};
    if (length == 0 || length > 65536)
        return {{"read", false}, {"error", "Invalid capabilities response length"}};
    QByteArray buffer(static_cast<int>(length), '\0');
    if (!CapabilitiesRequestAndCapabilitiesReply(monitor, buffer.data(), length))
        return {{"read", false}, {"windows_error", static_cast<qint64>(GetLastError())}};
    const auto end = buffer.indexOf('\0');
    if (end < 0)
        return {{"read", true}, {"parse_status", "unterminated response"}};
    auto result = ParseDiagnosticCapabilities(buffer.left(end));
    result["read"] = true;
    return result;
}
struct WindowsProbeContext
{
    QJsonArray displays;
    int physicalReads = 0;
    bool limited = false;
};
BOOL CALLBACK ProbeDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    auto &probe = *reinterpret_cast<WindowsProbeContext *>(context);
    auto &displays = probe.displays;
    if (displays.size() >= 8 || probe.physicalReads >= 8)
    {
        probe.limited = true;
        return FALSE;
    }
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    QJsonObject display;
    if (GetMonitorInfoW(monitor, reinterpret_cast<MONITORINFO *>(&info)))
        display["windows_display"] = QString::fromWCharArray(info.szDevice);
    DWORD count = 0;
    if (!GetNumberOfPhysicalMonitorsFromHMONITOR(monitor, &count))
        display["enumeration_error"] = static_cast<qint64>(GetLastError());
    else if (count > 0 && count <= 16)
    {
        std::vector<PHYSICAL_MONITOR> physical(count);
        if (GetPhysicalMonitorsFromHMONITOR(monitor, count, physical.data()))
        {
            QJsonArray controls;
            for (const auto &item : physical)
            {
                if (probe.physicalReads >= 8)
                {
                    probe.limited = true;
                    break;
                }
                ++probe.physicalReads;
                controls.append(
                    QJsonObject{{"0x10", ReadWindowsVcp(item.hPhysicalMonitor, 0x10)},
                                {"0x60", ReadWindowsVcp(item.hPhysicalMonitor, 0x60)},
                                {"capabilities", ReadWindowsCapabilities(item.hPhysicalMonitor)}});
            }
            DestroyPhysicalMonitors(count, physical.data());
            display["physical_monitors"] = controls;
        }
        else
            display["enumeration_error"] = static_cast<qint64>(GetLastError());
    }
    else
        display["physical_monitor_count"] = static_cast<qint64>(count);
    displays.append(display);
    return TRUE;
}
} // namespace
QJsonObject ProbeWindowsMonitorControls()
{
    QJsonArray adapters;
    for (DWORD i = 0; i < 32; ++i)
    {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, i, &device, 0))
            break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP))
            continue;
        QJsonObject adapter{{"model", QString::fromWCharArray(device.DeviceString)}};
        const QString prefix = "\\Registry\\Machine\\";
        const auto key = QString::fromWCharArray(device.DeviceKey);
        if (key.startsWith(prefix, Qt::CaseInsensitive))
        {
            HKEY registry = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                              reinterpret_cast<const wchar_t *>(key.mid(prefix.size()).utf16()), 0,
                              KEY_QUERY_VALUE, &registry) == ERROR_SUCCESS)
            {
                wchar_t version[256]{};
                DWORD type = 0, bytes = sizeof(version) - sizeof(wchar_t);
                if (RegQueryValueExW(registry, L"DriverVersion", nullptr, &type,
                                     reinterpret_cast<BYTE *>(version), &bytes) == ERROR_SUCCESS &&
                    type == REG_SZ)
                    adapter["driver_version"] = QString::fromWCharArray(version);
                RegCloseKey(registry);
            }
        }
        adapters.append(adapter);
    }
    WindowsProbeContext probe;
    const BOOL enumerated =
        EnumDisplayMonitors(nullptr, nullptr, ProbeDisplay, reinterpret_cast<LPARAM>(&probe));
    return {{"adapters", adapters},
            {"displays", probe.displays},
            {"enumeration_complete", bool(enumerated)},
            {"monitor_limit_reached", probe.limited},
            {"scope", "Standard Windows DDC/CI reads on up to 8 desktop displays and 8 physical monitors; "
                      "Windows display identifiers "
                      "are not ADL adapter/display indices. No AMD raw write API is called."}};
}
#endif
