#include "legacy_config.h"
#ifndef Q_OS_WIN
#include "linux_autostart.h"
#endif

namespace {
constexpr qsizetype MaximumLegacyConfigBytes = 2 * 1024 * 1024;
const QString setupWarning = "Choose your monitor and save to finish setup. Existing config.json is kept unchanged.";
}

DesktopConfig ImportLegacyWindowsConfig(const QByteArray &bytes, const QJsonArray &storedRows, QString &warning)
{
    warning.clear();
    QJsonObject json;
    QJsonParseError parse;
    if (bytes.size() <= MaximumLegacyConfigBytes)
    {
        const auto document = QJsonDocument::fromJson(bytes, &parse);
        if (parse.error == QJsonParseError::NoError && document.isObject())
            json = document.object();
        else
            warning = setupWarning;
    }
    else
        warning = setupWarning + " Import exceeds 2 MiB.";

    DesktopConfig config;
    // Match the original reader's in-memory defaults; a saved connection still
    // needs explicit identity confirmation before the desktop app can switch it.
    config.adapter = 5;
    config.display = 0;
    for (const auto &item : json["targets"].toArray())
    {
        const auto target = item.toArray();
        // toInt rejects fractional/out-of-range values; compare to avoid silently
        // turning invalid JSON numbers into a different adapter/display index.
        if (target.size() == 2 && target[0].isDouble() && target[1].isDouble() &&
            target[0].toDouble() == target[0].toInt() && target[1].toDouble() == target[1].toInt())
        {
            config.adapter = target[0].toInt();
            config.display = target[1].toInt();
            break;
        }
    }
    std::vector<lg::InputDef> inputs;
    for (const auto &item : json["inputs"].toArray())
    {
        const auto row = item.toObject();
        if (row["label"].isString() && row["code"].isString())
            inputs.push_back({row["label"].toString().toStdString(), row["code"].toString().toStdString()});
    }
    if (inputs.empty())
        for (const auto &row : config.inputs)
            inputs.push_back({row.label.toStdString(), row.code.toStdString()});
    std::vector<std::string> order;
    for (const auto &label : json["cycleOrder"].toArray())
        if (label.isString()) order.push_back(label.toString().toStdString());
    if (order.empty())
        for (const auto &row : config.inputs) order.push_back(row.label.toStdString());

    const auto hotkeys = json["hotkeys"].toObject();
    QMap<QString, QString> direct;
    if (hotkeys["direct"].isObject())
    {
        const auto stored = hotkeys["direct"].toObject();
        for (auto it = stored.begin(); it != stored.end(); ++it)
            if (it.value().isString()) direct[it.key()] = it.value().toString();
    }
    else
        for (const auto &row : config.inputs) direct[row.label] = row.shortcut;

    config.inputs.clear();
    for (const auto &input : lg::OrderInputs(inputs, order))
    {
        const auto label = QString::fromStdString(input.label);
        config.inputs.push_back({label, QString::fromStdString(input.code), direct.value(label), true});
    }
    auto appendDisabled = [&](DesktopInput input) {
        for (const auto &existing : config.inputs)
            if (existing.label == input.label) return;
        input.enabled = false;
        config.inputs.push_back(input);
    };
    for (const auto &item : storedRows)
    {
        const auto row = item.toObject();
        appendDisabled({row["label"].toString(), row["code"].toString(), row["shortcut"].toString(), false});
    }
    for (const auto &input : DesktopConfig{}.inputs) appendDisabled(input);
    config.source = json["i2cSourceAddr"].toString(config.source);
    config.cycleShortcut = hotkeys["cycle"].toString(config.cycleShortcut);
    config.debounce = json["debounceMs"].toInt(config.debounce);
    config.notifications = json["showNotifications"].toBool(config.notifications);
    config.startup = json["startWithWindows"].toBool(config.startup);
    return config;
}

DesktopConfig LoadLegacyWindowsConfig(const QString &path, const QJsonArray &storedRows, QString &warning)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return ImportLegacyWindowsConfig({}, storedRows, warning);
    if (file.size() > MaximumLegacyConfigBytes)
    {
        auto defaults = ImportLegacyWindowsConfig({}, storedRows, warning);
        warning = setupWarning + " Import exceeds 2 MiB.";
        return defaults;
    }
    return ImportLegacyWindowsConfig(file.read(MaximumLegacyConfigBytes + 1), storedRows, warning);
}

DesktopConfig LoadLegacyDesktopConfig(QString &warning)
{
    warning.clear();
    DesktopConfig c;
    QSettings prefs;
#ifdef Q_OS_WIN
    const auto storedRows = QJsonDocument::fromJson(prefs.value("inputs").toByteArray()).array();
    c = LoadLegacyWindowsConfig(QCoreApplication::applicationDirPath() + "/config.json", storedRows, warning);
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
    c.appearance = prefs.value("appearance", "system").toString();
    c.previousShortcut = prefs.value("previousShortcut").toString();
    return c;
}
