#include "diagnostics.h"
#include "process.h"
#include "../core/input_value.h"
#ifdef Q_OS_WIN
#include <windows.h>
#include <physicalmonitorenumerationapi.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <vector>
#endif

QJsonObject ParseDiagnosticVcp(const QByteArray &output, unsigned int feature)
{
    // ddcutil terse output: VCP 10 C <current> <maximum>, or VCP 60 SNC xNN.
    // Do not copy raw output: it can include serial numbers, paths or unrelated displays.
    const QString code = QString::number(feature, 16).rightJustified(2, '0');
    const QRegularExpression pattern("^VCP\\s+" + code +
        "\\s+(C|SNC)\\s+(0x[0-9a-f]+|x[0-9a-f]+|[0-9]+)(?:\\s+([0-9]+))?\\s*$",
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
        return {{"status", "Design preview; no settings or hardware accessed."}};
#ifdef Q_OS_WIN
    Q_UNUSED(snapshot);
    // A hung display driver cannot hold the UI or application shutdown indefinitely.
    const auto result = RunDesktopProcess(QCoreApplication::applicationFilePath(),
                                          {"--diagnostic-probe"}, 12000);
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
    QJsonObject checks{{"backend", "ddcutil"}};
    const auto version = RunDesktopProcess("ddcutil", {"--version"}, 3000);
    checks["backend_status"] = version.status;
    if (version.status == "unavailable") return checks;
    const auto match = QRegularExpression("ddcutil\\s+([0-9]+\\.[0-9]+(?:\\.[0-9]+)?)")
                           .match(QString::fromUtf8(version.output));
    if (match.hasMatch()) checks["backend_version"] = match.captured(1);
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
        const auto result = RunDesktopProcess("ddcutil",
            {snapshot.config.bus ? "--bus" : "--display", QString::number(snapshot.config.display),
             "getvcp", code, "--terse"}, 5000);
        auto value = ParseDiagnosticVcp(result.output, feature);
        if (result.status != "finished" || result.exitCode != 0) value = {{"read", false}};
        value["process_status"] = result.status;
        value["exit_code"] = result.exitCode;
        if (result.output.contains("Permission denied"))
            value["hint"] = "Check I2C device access permissions.";
        controls["0x" + code] = value;
    }
    checks["standard_vcp_reads"] = controls;
    return checks;
#endif
}

QString FormatDesktopDiagnostics(const DiagnosticSnapshot &snapshot, const QJsonObject &checks, bool preview)
{
    QJsonObject report{{"report_format", 1}, {"application", "LG Input Switch"},
        {"application_version", QCoreApplication::applicationVersion()},
        {"build_revision", LG_BUILD_REVISION},
        {"qt_version", QString::fromLatin1(qVersion())},
        {"os", QSysInfo::prettyProductName()}, {"kernel_version", QSysInfo::kernelVersion()},
        {"architecture", QSysInfo::currentCpuArchitecture()}, {"preview", preview},
        {"settings_unsaved", snapshot.unsaved},
        {"privacy", "No raw EDID, serial numbers, usernames, hostnames, paths or shortcuts collected. "
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
    // Values are parsed and normalized; user labels and arbitrary strings are never exported.
    unsigned int source;
    report["source_address"] = ParseInputValue(snapshot.config.source.toStdString(), 0xff, source)
        ? QJsonValue(QString("0x%1").arg(source, 2, 16, QChar('0'))) : QJsonValue("invalid");
    report["feature"] = "0xf4";
    QJsonArray inputs;
    for (const auto &input : snapshot.config.inputs)
    {
        unsigned int code;
        inputs.append(QJsonObject{{"enabled", input.enabled}, {"code",
            ParseInputValue(input.code.toStdString(), 0xffff, code)
                ? QJsonValue(QString("0x%1").arg(code, 4, 16, QChar('0'))) : QJsonValue("invalid")}});
    }
    report["configured_inputs"] = inputs;
    report["last_command"] = snapshot.lastCommand.isEmpty()
        ? QJsonValue("No input command attempted in this session.") : QJsonValue(snapshot.lastCommand);
    return QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
}

bool SaveDesktopDiagnostics(const QString &path, const QString &report, QString &error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) { error = file.errorString(); return false; }
    const auto bytes = report.toUtf8();
    if (file.write(bytes) != bytes.size() || !file.commit())
    { error = file.errorString(); return false; }
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
BOOL CALLBACK ProbeDisplay(HMONITOR monitor, HDC, LPRECT, LPARAM context)
{
    auto &displays = *reinterpret_cast<QJsonArray *>(context);
    if (displays.size() >= 8) return FALSE;
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
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
                controls.append(QJsonObject{{"0x10", ReadWindowsVcp(item.hPhysicalMonitor, 0x10)},
                                             {"0x60", ReadWindowsVcp(item.hPhysicalMonitor, 0x60)}});
            DestroyPhysicalMonitors(count, physical.data());
            display["physical_monitors"] = controls;
        }
        else display["enumeration_error"] = static_cast<qint64>(GetLastError());
    }
    else display["physical_monitor_count"] = static_cast<qint64>(count);
    displays.append(display);
    return TRUE;
}
}
QJsonObject ProbeWindowsMonitorControls()
{
    QJsonArray adapters;
    for (DWORD i = 0; i < 32; ++i)
    {
        DISPLAY_DEVICEW device{}; device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(nullptr, i, &device, 0)) break;
        if (!(device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP)) continue;
        QJsonObject adapter{{"model", QString::fromWCharArray(device.DeviceString)}};
        const QString prefix = "\\Registry\\Machine\\";
        const auto key = QString::fromWCharArray(device.DeviceKey);
        if (key.startsWith(prefix, Qt::CaseInsensitive))
        {
            HKEY registry = nullptr;
            if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                reinterpret_cast<const wchar_t *>(key.mid(prefix.size()).utf16()), 0, KEY_QUERY_VALUE, &registry) == ERROR_SUCCESS)
            {
                wchar_t version[256]{}; DWORD type = 0, bytes = sizeof(version) - sizeof(wchar_t);
                if (RegQueryValueExW(registry, L"DriverVersion", nullptr, &type,
                                     reinterpret_cast<BYTE *>(version), &bytes) == ERROR_SUCCESS && type == REG_SZ)
                    adapter["driver_version"] = QString::fromWCharArray(version);
                RegCloseKey(registry);
            }
        }
        adapters.append(adapter);
    }
    QJsonArray displays;
    const BOOL enumerated = EnumDisplayMonitors(nullptr, nullptr, ProbeDisplay, reinterpret_cast<LPARAM>(&displays));
    return {{"adapters", adapters}, {"displays", displays}, {"enumeration_complete", bool(enumerated)},
        {"scope", "Standard Windows DDC/CI reads on up to 8 desktop displays; Windows display identifiers "
                  "are not ADL adapter/display indices. No AMD raw write API is called."}};
}
#endif
