#include "platform.h"
#include "../core/input_value.h"
#include "process.h"
#include "profiles.h"
#ifdef Q_OS_WIN
#include "../app/app_config.h"
#include "../platform/windows/adl_session.h"
#include "../platform/windows/adl_transport.h"
#else
#include "../platform/linux/ddcutil_backend.h"
#include "linux_autostart.h"
#endif

DesktopConfig LoadDesktopConfig(QString &warning)
{
    DesktopConfig c;
    QSettings prefs;
    c.appearance = prefs.value("appearance", "system").toString();
    c.previousShortcut = prefs.value("previousShortcut").toString();
    const auto storedRows = QJsonDocument::fromJson(prefs.value("inputs").toByteArray()).array();
#ifdef Q_OS_WIN
    AppConfig legacy;
    if (!LoadConfig(legacy))
        warning = "Choose your monitor and save to finish setup. Existing unreadable configuration is not "
                  "saved automatically.";
    if (!legacy.targets.empty())
    {
        c.adapter = legacy.targets[0].first;
        c.display = legacy.targets[0].second;
    }
    c.inputs.clear();
    for (const auto &input : lg::OrderInputs(legacy.inputs, legacy.cycleOrder))
    {
        auto hk = legacy.hotkeys.direct.find(input.label);
        c.inputs.push_back(
            {QString::fromStdString(input.label), QString::fromStdString(input.code),
             hk == legacy.hotkeys.direct.end() ? QString{} : QString::fromStdString(hk->second), true});
    }
    // Keep disabled/custom mappings available to re-enable. The legacy enabled
    // values take precedence if config.json was edited outside this frontend.
    auto appendDisabled = [&](DesktopInput row) {
        for (const auto &existing : c.inputs)
            if (existing.label == row.label)
                return;
        row.enabled = false;
        c.inputs.push_back(row);
    };
    for (const auto &item : storedRows)
    {
        auto row = item.toObject();
        appendDisabled({row["label"].toString(), row["code"].toString(), row["shortcut"].toString(), false});
    }
    const DesktopConfig defaults;
    for (const auto &row : defaults.inputs)
        appendDisabled(row);
    c.source = QString::fromStdString(legacy.i2cSourceAddr);
    c.cycleShortcut = QString::fromStdString(legacy.hotkeys.cycle);
    c.debounce = legacy.debounceMs;
    c.notifications = legacy.showNotifications;
    c.startup = legacy.startWithWindows;
#else
    c.startup = DesktopAutostartEnabled();
    c.globalShortcuts = prefs.value("globalShortcuts", false).toBool();
    c.cycleShortcut = prefs.value("cycleShortcut", c.cycleShortcut).toString();
    c.display = prefs.value("target", -1).toInt();
    c.bus = prefs.value("bus", true).toBool();
    c.source = prefs.value("source", c.source).toString();
    c.debounce = prefs.value("debounce", c.debounce).toInt();
    c.notifications = prefs.value("notifications", true).toBool();
    const auto stored = prefs.value("inputs").toByteArray();
    if (!stored.isEmpty())
    {
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(stored, &error);
        if (error.error == QJsonParseError::NoError && doc.isArray() && !doc.array().isEmpty())
        {
            c.inputs.clear();
            for (auto item : doc.array())
            {
                auto row = item.toObject();
                c.inputs.push_back({row["label"].toString(), row["code"].toString(),
                                    row["shortcut"].toString(), row["enabled"].toBool()});
            }
        }
        else
            warning = "Saved inputs could not be read. Review settings before saving.";
    }
#endif
    return c;
}

bool SaveDesktopConfig(const DesktopConfig &c, QString &error)
{
#ifdef Q_OS_WIN
    AppConfig legacy;
    LoadConfig(legacy);
    if (legacy.targets.empty())
        legacy.targets.push_back({c.adapter, c.display});
    else
        legacy.targets[0] = {c.adapter, c.display};
    legacy.inputs.clear();
    legacy.cycleOrder.clear();
    legacy.hotkeys.direct.clear();
    for (const auto &row : c.inputs)
    {
        const auto label = row.label.toStdString();
        if (row.enabled)
        {
            legacy.inputs.push_back({label, row.code.toStdString()});
            legacy.cycleOrder.push_back(label);
        }
        if (!row.shortcut.isEmpty())
            legacy.hotkeys.direct[label] = row.shortcut.toStdString();
    }
    legacy.i2cSourceAddr = c.source.toStdString();
    legacy.hotkeys.cycle = c.cycleShortcut.toStdString();
    legacy.debounceMs = c.debounce;
    legacy.showNotifications = c.notifications;
    legacy.startWithWindows = c.startup;
    if (!SaveConfig(legacy))
    {
        error = "Could not save config.json. Check that the app folder is writable.";
        return false;
    }
#endif
    QJsonArray inputs;
    for (const auto &row : c.inputs)
        inputs.append(QJsonObject{
            {"label", row.label}, {"code", row.code}, {"shortcut", row.shortcut}, {"enabled", row.enabled}});
    QSettings prefs;
    prefs.setValue("inputs", QJsonDocument(inputs).toJson(QJsonDocument::Compact));
#ifndef Q_OS_WIN
    prefs.setValue("globalShortcuts", c.globalShortcuts);
    prefs.setValue("cycleShortcut", c.cycleShortcut);
    prefs.setValue("target", c.display);
    prefs.setValue("bus", c.bus);
    prefs.setValue("source", c.source);
    prefs.setValue("debounce", c.debounce);
    prefs.setValue("notifications", c.notifications);
#endif
    prefs.setValue("previousShortcut", c.previousShortcut);
    prefs.setValue("appearance", c.appearance);
    prefs.sync();
    if (prefs.status() != QSettings::NoError)
    {
        error = "Could not save desktop preferences.";
        return false;
    }
    return true;
}

Discovery DiscoverDesktopMonitors()
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
                QString identity;
                if (Adl().edid)
                {
                    ADLDisplayEDIDData data{};
                    data.iSize = sizeof(data);
                    if (Adl().edid(adapter.iAdapterIndex, display, &data) == ADL_OK &&
                        data.iEDIDSize >= 128 && data.iEDIDSize <= ADL_MAX_EDIDDATA_SIZE)
                        identity = MonitorIdentity(QByteArray(data.cEDIDData, data.iEDIDSize));
                }
                result.monitors.push_back(
                    {QString::fromLocal8Bit(info[j].strDisplayName) +
                         QString(" · Adapter %1 / Display %2").arg(adapter.iAdapterIndex).arg(display),
                     adapter.iAdapterIndex, display, false, identity});
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
                    const auto identity = MonitorIdentity(edid.read(128));
                    if (!identity.isEmpty())
                        monitor.identity = identity;
                }
            }
    if (probe.status != "finished" || probe.exitCode != 0)
    {
        result.monitors.clear();
        result.message = output.trimmed().left(1200);
    }
#endif
    if (result.message.isEmpty())
        result.message =
            result.monitors.isEmpty()
                ? "No controllable monitors found. Check the cable, graphics driver and DDC/I2C access."
                : QString("%1 monitor(s) available. Select the display you want to control.")
                      .arg(result.monitors.size());
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
    AdlTransport transport(c.adapter, c.display);
    return transport.Send(command);
#else
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

bool SaveDesktopShortcutPreferences(const DesktopConfig &c, QString &error)
{
    QSettings prefs;
    QJsonArray rows;
    for (const auto &row : c.inputs)
        rows.append(QJsonObject{
            {"label", row.label}, {"code", row.code}, {"shortcut", row.shortcut}, {"enabled", row.enabled}});
    prefs.setValue("inputs", QJsonDocument(rows).toJson(QJsonDocument::Compact));
    prefs.setValue("cycleShortcut", c.cycleShortcut);
    prefs.setValue("previousShortcut", c.previousShortcut);
    prefs.sync();
    if (prefs.status() != QSettings::NoError)
    {
        error = "Could not persist the shortcut change from Plasma.";
        return false;
    }
    return true;
}
