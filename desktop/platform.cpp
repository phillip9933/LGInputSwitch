#include "platform.h"
#ifdef Q_OS_WIN
#include "../amdddc/adl.h"
#include "../app/app_config.h"
#include "../platform/windows/adl_transport.h"
#else
#include "../platform/linux/ddcutil_backend.h"
#endif

DesktopConfig LoadDesktopConfig(QString &warning)
{
    DesktopConfig c;
    QSettings prefs;
    c.appearance = prefs.value("appearance", "system").toString();
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
    if (!InitADL())
    {
        result.message = "AMD display access is unavailable. Check the AMD graphics driver.";
        return result;
    }
    int count = 0;
    if (adlprocs.ADL_Adapter_NumberOfAdapters_Get(&count) != ADL_OK || count <= 0)
    {
        result.message = "No AMD display adapters found.";
        return result;
    }
    std::vector<AdapterInfo> adapters(count);
    for (auto &adapter : adapters)
        adapter.iSize = sizeof(AdapterInfo);
    if (adlprocs.ADL_Adapter_AdapterInfo_Get(adapters.data(), int(adapters.size() * sizeof(AdapterInfo))) !=
        ADL_OK)
    {
        result.message = "Could not enumerate AMD adapters.";
        return result;
    }
    for (const auto &adapter : adapters)
    {
        int displays = 0;
        ADLDisplayInfo *info = nullptr;
        const auto rc = adlprocs.ADL_Display_DisplayInfo_Get(adapter.iAdapterIndex, &displays, &info, 0);
        if (rc == ADL_OK && info)
            for (int j = 0; j < displays; ++j)
            {
                const int required =
                    ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
                if ((info[j].iDisplayInfoValue & required) != required ||
                    info[j].displayID.iDisplayLogicalAdapterIndex != adapter.iAdapterIndex)
                    continue;
                const int display = info[j].displayID.iDisplayLogicalIndex;
                result.monitors.push_back(
                    {QString::fromLocal8Bit(info[j].strDisplayName) +
                         QString(" · Adapter %1 / Display %2").arg(adapter.iAdapterIndex).arg(display),
                     adapter.iAdapterIndex, display, false});
            }
        if (info)
            ADL_Main_Memory_Free(reinterpret_cast<void **>(&info));
    }
#else
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start("ddcutil", {"detect", "--brief"});
    if (!process.waitForStarted(3000))
    {
        result.message = "ddcutil is unavailable. Install it using your distribution's package manager.";
        return result;
    }
    if (!process.waitForFinished(15000))
    {
        process.kill();
        process.waitForFinished();
        result.message = "Monitor discovery timed out. Check device access, then try again.";
        return result;
    }
    const QString output = QString::fromLocal8Bit(process.readAll());
    DesktopMonitor current;
    auto append = [&] {
        if (current.display >= 0)
            result.monitors.push_back(current);
        current = {};
    };
    for (const auto &line : output.split('\n'))
    {
        auto display = QRegularExpression("^Display\\s+(\\d+)").match(line);
        if (display.hasMatch())
        {
            append();
            current.display = display.captured(1).toInt();
            current.label = "Display " + display.captured(1);
        }
        auto bus = QRegularExpression("I2C bus:\\s+/dev/i2c-(\\d+)").match(line);
        if (bus.hasMatch() && current.display >= 0)
        {
            current.display = bus.captured(1).toInt();
            current.bus = true;
        }
        auto model = QRegularExpression("Model:\\s+(.+)").match(line);
        if (model.hasMatch() && current.display >= 0)
            current.label = model.captured(1).trimmed();
        // ddcutil 3 brief output uses manufacturer:model:serial instead of a
        // separate Model field. Show the model, without the serial number.
        auto monitor = QRegularExpression("Monitor:\\s+([^:]+):([^:]+)").match(line);
        if (monitor.hasMatch() && current.display >= 0)
            current.label = monitor.captured(2).trimmed();
    }
    append();
    for (auto &monitor : result.monitors)
        monitor.label += monitor.bus ? QString(" · I²C %1").arg(monitor.display)
                                     : QString(" · Display %1").arg(monitor.display);
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
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
#ifdef Q_OS_WIN
    AdlTransport transport(c.adapter, c.display);
#else
    DdcutilTransport transport(c.bus ? "--bus" : "--display", static_cast<unsigned int>(c.display));
#endif
    return lg::SwitchInput(transport, c.source.toStdString(), code.toStdString());
}
