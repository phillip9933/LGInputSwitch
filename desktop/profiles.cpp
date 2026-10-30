#include "profiles.h"
#include "../core/input_value.h"
#include "monitor_inputs.h"
#include <QKeySequence>
#ifndef Q_OS_WIN
#include "linux_autostart.h"
#endif
#ifdef Q_OS_WIN
#include "../app/hotkeys.h"
#endif

namespace
{
QJsonArray InputJson(const QVector<DesktopInput> &inputs)
{
    QJsonArray result;
    for (const auto &input : inputs)
        result.append(QJsonObject{{"id", input.id}, {"label", input.label}, {"code", input.code}});
    return result;
}
QJsonObject RouteJson(const DesktopMonitor &route)
{
    return {
        {"adapter", route.adapter}, {"display", route.display}, {"bus", route.bus}, {"label", route.label}};
}
QString Hash(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}
QString LegacyShortcut(QString text)
{
    auto tokens = text.split('+');
    for (auto &token : tokens)
        if (token.trimmed().compare("WIN", Qt::CaseInsensitive) == 0)
            token = "Meta";
    const auto key = QKeySequence::fromString(tokens.join('+'), QKeySequence::PortableText);
    return key.isEmpty() ? text : key.toString(QKeySequence::PortableText).toUpper();
}
QJsonObject ProfileSignature(const DesktopWorkspace &workspace, const MonitorProfile &profile)
{
    QJsonArray monitors, actions;
    for (const auto &id : profile.monitors)
        if (const auto *m = FindMonitor(workspace, id))
        {
            QJsonArray inputCodes;
            for (const auto &input : m->inputs)
                inputCodes.append(QJsonObject{{"id", input.id}, {"code", input.code}});
            monitors.append(QJsonObject{{"id", id},
                                        {"identity", m->identity},
                                        {"route", RouteJson(m->route)},
                                        {"source", m->source},
                                        {"method", m->method},
                                        {"inputs", inputCodes}});
        }
    for (const auto &a : profile.actions)
    {
        QJsonObject mapping;
        for (auto it = a.inputs.begin(); it != a.inputs.end(); ++it)
            mapping[it.key()] = it.value();
        actions.append(QJsonObject{{"id", a.id}, {"enabled", a.enabled}, {"inputs", mapping}});
    }
    return {{"monitors", monitors}, {"actions", actions}};
}
QByteArray ReadWorkspaceBytes(bool &exists, QString &error, const QString &storagePath)
{
    const auto path = storagePath.isEmpty() ? WorkspacePath() : storagePath;
    if (!path.isEmpty())
    {
        QFile file(path);
        exists = file.exists();
        if (!exists)
            return {};
        if (!file.open(QIODevice::ReadOnly))
        {
            error = file.errorString();
            return {};
        }
        if (file.size() > 2097152)
        {
            error = "Profile configuration exceeds 2 MiB.";
            return {};
        }
        return file.readAll();
    }
    QSettings settings;
    exists = settings.contains("profilesV2");
    const auto bytes = settings.value("profilesV2").toByteArray();
    if (bytes.size() > 2097152)
    {
        error = "Profile configuration exceeds 2 MiB.";
        return {};
    }
    return bytes;
}
} // namespace

QString NewDesktopId()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces);
}
QVector<DesktopInput> DefaultMonitorInputs(const QString &method)
{
    QVector<DesktopInput> inputs = method == "standard"
                                       ? QVector<DesktopInput>{{"DisplayPort 1", "0x0f", {}, true},
                                                               {"DisplayPort 2", "0x10", {}, true},
                                                               {"HDMI 1", "0x11", {}, true},
                                                               {"HDMI 2", "0x12", {}, true}}
                                       : DesktopConfig{}.inputs;
    for (auto &input : inputs)
    {
        input.id = NewDesktopId();
        input.shortcut.clear();
    }
    return inputs;
}
const SavedMonitor *FindMonitor(const DesktopWorkspace &w, const QString &id)
{
    for (const auto &m : w.monitors)
        if (m.id == id)
            return &m;
    return nullptr;
}
const MonitorProfile *FindProfile(const DesktopWorkspace &w, const QString &id)
{
    for (const auto &p : w.profiles)
        if (p.id == id)
            return &p;
    return nullptr;
}
const DesktopInput *FindMonitorInput(const SavedMonitor &m, const QString &id)
{
    for (const auto &input : m.inputs)
        if (input.id == id)
            return &input;
    return nullptr;
}
DesktopWorkspace MigrateDesktopConfig(const DesktopConfig &legacy)
{
    DesktopWorkspace w;
    w.preferences = legacy;
    SavedMonitor m;
    m.id = "monitor-1";
    m.name = "Main monitor";
    m.route = {"", legacy.adapter, legacy.display, legacy.bus, {}};
    m.source = legacy.source;
    m.inputs = legacy.inputs;
    MonitorProfile p;
    p.id = "default";
    p.name = "Main monitor";
    p.monitors = {m.id};
    p.cycleShortcut = LegacyShortcut(legacy.cycleShortcut);
    p.previousShortcut = LegacyShortcut(legacy.previousShortcut);
    for (auto &input : m.inputs)
    {
        // Retain the 1.2 Plasma catalogue IDs (including renamed custom inputs).
        input.id = "input-" + Hash(input.label.toUtf8());
        p.actions.push_back(
            {input.id, input.label, LegacyShortcut(input.shortcut), input.enabled, {{m.id, input.id}}});
        input.shortcut.clear();
        input.enabled = true;
    }
    m.autoInputs = IsPresetInputList(m);
    w.monitors = {m};
    w.profiles = {p};
    w.activeProfile = p.id;
    return w;
}
QJsonObject WorkspaceJson(const DesktopWorkspace &w)
{
    QJsonArray monitors, profiles;
    for (const auto &m : w.monitors)
        monitors.append(QJsonObject{{"id", m.id},
                                    {"name", m.name},
                                    {"identity", m.identity},
                                    {"route", RouteJson(m.route)},
                                    {"source", m.source},
                                    {"method", m.method},
                                    {"autoInputs", m.autoInputs},
                                    {"inputs", InputJson(m.inputs)}});
    for (const auto &p : w.profiles)
    {
        QJsonArray members, actions;
        for (const auto &id : p.monitors)
            members.append(id);
        for (const auto &a : p.actions)
        {
            QJsonObject mapping;
            for (auto it = a.inputs.begin(); it != a.inputs.end(); ++it)
                mapping[it.key()] = it.value();
            actions.append(QJsonObject{{"id", a.id},
                                       {"name", a.name},
                                       {"shortcut", a.shortcut},
                                       {"enabled", a.enabled},
                                       {"inputs", mapping}});
        }
        profiles.append(QJsonObject{{"id", p.id},
                                    {"name", p.name},
                                    {"monitors", members},
                                    {"actions", actions},
                                    {"cycleShortcut", p.cycleShortcut},
                                    {"previousShortcut", p.previousShortcut}});
    }
    const auto &c = w.preferences;
    return {{"version", 3},
            {"activeProfile", w.activeProfile},
            {"monitors", monitors},
            {"profiles", profiles},
            {"preferences", QJsonObject{{"appearance", c.appearance},
                                        {"notifications", c.notifications},
                                        {"startup", c.startup},
                                        {"globalShortcuts", c.globalShortcuts},
                                        {"debounce", c.debounce}}}};
}
bool ValidateWorkspace(const DesktopWorkspace &w, QString &error)
{
    error.clear();
    auto fail = [&](const QString &text) {
        error = text;
        return false;
    };
    if (w.monitors.isEmpty() || w.monitors.size() > 16 || w.profiles.isEmpty() || w.profiles.size() > 32)
        return fail("Keep between 1 and 16 saved monitors and between 1 and 32 profiles.");
    if (!FindProfile(w, w.activeProfile))
        return fail("Select an existing profile.");
    if (w.preferences.debounce < 0 || w.preferences.debounce > 10000 ||
        !QStringList{"system", "light", "dark"}.contains(w.preferences.appearance))
        return fail("Invalid application preferences.");
    QSet<QString> monitorIds, names, profileIds, profileNames, shortcuts, identities, routes;
    auto validId = [](const QString &id) {
        return QRegularExpression("^[A-Za-z0-9_-]{1,100}$").match(id).hasMatch();
    };
    auto validName = [](const QString &name) {
        return !name.trimmed().isEmpty() && name.size() <= 100 && !name.contains(QChar::Null);
    };
    for (const auto &m : w.monitors)
    {
        unsigned int n;
        if (!validId(m.id) || monitorIds.contains(m.id) || !validName(m.name) || names.contains(m.name))
            return fail("Saved monitors need unique names and IDs.");
        monitorIds.insert(m.id);
        names.insert(m.name);
        if (!m.identity.isEmpty() && !QRegularExpression("^[0-9a-f]{64}$").match(m.identity).hasMatch())
            return fail("Invalid monitor fingerprint.");
        // A physical display has one saved definition, shared by all profiles.
        const auto route = MonitorRouteKey(m.route);
        if ((!m.identity.isEmpty() && identities.contains(m.identity)) ||
            (m.identity.isEmpty() && m.route.display >= 0 && routes.contains(route)))
            return fail("This monitor is already saved. Reuse its definition in another profile.");
        if (!m.identity.isEmpty())
            identities.insert(m.identity);
        if (m.identity.isEmpty() && m.route.display >= 0)
            routes.insert(route);
        if (!QStringList{"lg", "standard"}.contains(m.method))
            return fail(m.name + ": choose a supported switching method.");
        if ((m.method == "lg" && !ParseInputValue(m.source.toStdString(), 0xff, n)) || m.inputs.isEmpty() ||
            m.inputs.size() > 64)
            return fail(m.name + ": enter a valid source address and at least one input.");
        QSet<QString> ids, labels;
        for (const auto &input : m.inputs)
        {
            if (!validId(input.id) || ids.contains(input.id) || !validName(input.label) ||
                labels.contains(input.label) || !ParseInputValue(input.code.toStdString(), 0xffff, n))
                return fail(m.name + ": each input needs a unique name and ID and a valid input code.");
            ids.insert(input.id);
            labels.insert(input.label);
        }
    }
    auto shortcut = [&](const QString &text) {
        if (text.isEmpty())
            return true;
        const auto key = QKeySequence::fromString(text, QKeySequence::PortableText);
        const auto canonical = key.toString(QKeySequence::PortableText).toUpper();
        if (key.count() != 1 || canonical.isEmpty() || key[0].key() == Qt::Key_unknown ||
            shortcuts.contains(canonical))
            return false;
#ifdef Q_OS_WIN
        HotkeySpec spec{};
        if (!ParseHotkey(text.toStdString(), spec))
            return false;
#endif
        shortcuts.insert(canonical);
        return true;
    };
    for (const auto &p : w.profiles)
    {
        if (!validId(p.id) || profileIds.contains(p.id) || !validName(p.name) ||
            profileNames.contains(p.name))
            return fail("Profiles need unique names and IDs.");
        profileIds.insert(p.id);
        profileNames.insert(p.name);
        if (p.monitors.isEmpty() || p.actions.isEmpty() || p.actions.size() > 64)
            return fail(p.name + ": assign at least one monitor and one action (up to 64 actions).");
        QSet<QString> members;
        for (const auto &id : p.monitors)
        {
            if (!monitorIds.contains(id) || members.contains(id))
                return fail(p.name + ": invalid or duplicate monitor assignment.");
            members.insert(id);
        }
        if (!shortcut(p.cycleShortcut) || !shortcut(p.previousShortcut))
            return fail("Shortcuts must be valid and unique across every profile.");
        QSet<QString> ids, labels;
        for (const auto &a : p.actions)
        {
            if (!validId(a.id) || a.id == "cycle" || a.id == "previous" || ids.contains(a.id) ||
                !validName(a.name) || labels.contains(a.name))
                return fail(p.name + ": actions need unique names and IDs.");
            ids.insert(a.id);
            labels.insert(a.name);
            if (a.enabled && !shortcut(a.shortcut))
                return fail("Shortcuts must be valid and unique across every profile.");
            if (a.inputs.size() != members.size())
                return fail(p.name + ": select an input for every assigned monitor in every action.");
            for (auto it = a.inputs.begin(); it != a.inputs.end(); ++it)
            {
                const auto *m = FindMonitor(w, it.key());
                if (!members.contains(it.key()) || !m ||
                    (!FindMonitorInput(*m, it.value()) && (a.enabled || !it.value().isEmpty())))
                    return fail(p.name + ": select an existing input for each monitor.");
            }
        }
    }
    return true;
}
bool ParseWorkspace(const QJsonObject &json, DesktopWorkspace &out, QString &error)
{
    error.clear();
    if (json["version"].toInt(-1) != 2 && json["version"].toInt(-1) != 3)
    {
        error = "Unsupported profile configuration version.";
        return false;
    }
    DesktopWorkspace w;
    const auto c = json["preferences"].toObject();
    w.preferences.appearance = c["appearance"].toString("system");
    w.preferences.notifications = c["notifications"].toBool(true);
    w.preferences.startup = c["startup"].toBool();
    w.preferences.globalShortcuts = c["globalShortcuts"].toBool();
    w.preferences.debounce = c["debounce"].toInt(750);
    w.activeProfile = json["activeProfile"].toString();
    for (auto item : json["monitors"].toArray())
    {
        const auto j = item.toObject(), r = j["route"].toObject();
        SavedMonitor m;
        m.id = j["id"].toString();
        m.name = j["name"].toString();
        m.identity = j["identity"].toString();
        m.route = {r["label"].toString(), r["adapter"].toInt(-1), r["display"].toInt(-1), r["bus"].toBool(),
                   m.identity};
        m.source = j["source"].toString();
        m.method = j["method"].toString("lg");
        for (auto input : j["inputs"].toArray())
        {
            const auto i = input.toObject();
            m.inputs.push_back({i["label"].toString(), i["code"].toString(), {}, true, i["id"].toString()});
        }
        m.autoInputs = j.contains("autoInputs") ? j["autoInputs"].toBool() : IsPresetInputList(m);
        w.monitors.push_back(m);
    }
    for (auto item : json["profiles"].toArray())
    {
        const auto j = item.toObject();
        MonitorProfile p;
        p.id = j["id"].toString();
        p.name = j["name"].toString();
        p.cycleShortcut = j["cycleShortcut"].toString();
        p.previousShortcut = j["previousShortcut"].toString();
        for (auto id : j["monitors"].toArray())
            p.monitors.push_back(id.toString());
        for (auto action : j["actions"].toArray())
        {
            const auto a = action.toObject();
            ProfileAction row{a["id"].toString(),
                              a["name"].toString(),
                              a["shortcut"].toString(),
                              a["enabled"].toBool(),
                              {}};
            const auto mapping = a["inputs"].toObject();
            for (auto it = mapping.begin(); it != mapping.end(); ++it)
                row.inputs[it.key()] = it.value().toString();
            p.actions.push_back(row);
        }
        w.profiles.push_back(p);
    }
    if (!ValidateWorkspace(w, error))
        return false;
    out = w;
    return true;
}
QString WorkspacePath()
{
#ifdef Q_OS_WIN
    return QCoreApplication::applicationDirPath() + "/desktop-profiles.json";
#else
    return {};
#endif
}
DesktopWorkspace LoadDesktopWorkspace(QString &warning, const QString &storagePath)
{
    warning.clear();
    bool exists = false;
    QString error;
    const auto bytes = ReadWorkspaceBytes(exists, error, storagePath);
    if (exists)
    {
        DesktopWorkspace w;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(bytes, &parse);
        if (error.isEmpty() && parse.error == QJsonParseError::NoError && doc.isObject() &&
            ParseWorkspace(doc.object(), w, error))
        {
#ifndef Q_OS_WIN
            if (storagePath.isEmpty())
                w.preferences.startup = DesktopAutostartEnabled();
#endif
            return w;
        }
        warning = "Saved profiles could not be read. Switching and saving are blocked to preserve the file. "
                  "Back up and repair/remove the profile configuration, then restart. " +
                  error;
        return MigrateDesktopConfig(DesktopConfig{});
    }
    return MigrateDesktopConfig(LoadDesktopConfig(warning));
}
bool SaveDesktopWorkspace(const DesktopWorkspace &w, QString &error, const QString &storagePath)
{
    error.clear();
    if (!ValidateWorkspace(w, error))
        return false;
    // Never overwrite a corrupted/newer file with an in-memory fallback.
    bool exists = false;
    const auto original = ReadWorkspaceBytes(exists, error, storagePath);
    if (exists)
    {
        DesktopWorkspace existing;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(original, &parse);
        if (!error.isEmpty() || parse.error != QJsonParseError::NoError || !doc.isObject() ||
            !ParseWorkspace(doc.object(), existing, error))
        {
            error = "Existing profiles cannot be read; preserved without changes. " + error;
            return false;
        }
    }
    const auto bytes = QJsonDocument(WorkspaceJson(w)).toJson(QJsonDocument::Indented);
    const auto path = storagePath.isEmpty() ? WorkspacePath() : storagePath;
    if (!path.isEmpty())
    {
        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
        {
            error = "Could not save desktop profiles. " + file.errorString();
            return false;
        }
    }
    else
    {
        QSettings prefs;
        prefs.setValue("profilesV2", bytes);
        prefs.sync();
        if (prefs.status() != QSettings::NoError)
        {
            error = "Could not save desktop profiles.";
            return false;
        }
    }
    return true;
}
QString MonitorRouteKey(const DesktopMonitor &m)
{
    return QString("%1:%2:%3").arg(m.adapter).arg(m.display).arg(m.bus);
}
QString MonitorIdentity(const QByteArray &edid)
{
    if (edid.size() < 128 || edid.left(8) != QByteArray::fromHex("00ffffffffffff00"))
        return {};
    unsigned int sum = 0;
    for (int i = 0; i < 128; ++i)
        sum += static_cast<unsigned char>(edid[i]);
    if ((sum & 0xff) != 0)
        return {};
    // Base block contains model/serial. Ignore mutable extension blocks.
    return Hash(edid.left(128));
}
Discovery ParseDesktopDiscovery(const QByteArray &output)
{
    Discovery result;
    DesktopMonitor current;
    auto append = [&] {
        if (current.display >= 0)
            result.monitors.push_back(current);
        current = {};
    };
    for (const auto &line : QString::fromUtf8(output).split('\n'))
    {
        auto display = QRegularExpression("^Display\\s+(\\d+)").match(line);
        if (display.hasMatch())
        {
            append();
            current.display = display.captured(1).toInt();
            current.label = "Display " + display.captured(1);
        }
        // Explicitly terminate invalid displays as well as starting valid ones.
        if (line.startsWith("Invalid display"))
            append();
        auto bus = QRegularExpression("I2C bus:\\s+/dev/i2c-(\\d+)").match(line);
        if (bus.hasMatch() && current.display >= 0)
        {
            current.display = bus.captured(1).toInt();
            current.bus = true;
        }
        auto model = QRegularExpression("Model:\\s+(.+)").match(line);
        if (model.hasMatch() && current.display >= 0)
            current.label = model.captured(1).trimmed();
        auto monitor = QRegularExpression("Monitor:\\s+([^:]+):([^:]+):(.*)").match(line);
        if (monitor.hasMatch() && current.display >= 0)
        {
            current.label = monitor.captured(2).trimmed();
            const auto serial = monitor.captured(3).trimmed();
            if (!serial.isEmpty() && serial != "0" && serial != "Unspecified")
                current.identity = Hash(monitor.captured(1).trimmed().toUtf8() + ':' +
                                        current.label.toUtf8() + ':' + serial.toUtf8());
        }
    }
    append();
    for (auto &m : result.monitors)
        m.label += m.bus ? QString(" · I²C %1").arg(m.display) : QString(" · Display %1").arg(m.display);
    return result;
}
MonitorResolution ResolveSavedMonitor(const SavedMonitor &m, const Discovery &d,
                                      const QMap<QString, DesktopMonitor> &confirmed)
{
    if (!m.identity.isEmpty())
    {
        QVector<DesktopMonitor> matches;
        for (const auto &candidate : d.monitors)
            if (candidate.identity == m.identity)
                matches.push_back(candidate);
        if (matches.size() == 1)
            return {true, matches.front(), {}};
        if (matches.size() > 1)
            return {false, {}, "Ambiguous identity. Select the connection again."};
        return {false, {}, "Disconnected or identity changed. Select the connection again."};
    }
    if (confirmed.contains(m.id))
    {
        const auto expected = confirmed.value(m.id);
        QVector<DesktopMonitor> matches;
        for (const auto &candidate : d.monitors)
            if (MonitorRouteKey(candidate) == MonitorRouteKey(expected) &&
                candidate.label == expected.label && candidate.identity == expected.identity)
                matches.push_back(candidate);
        if (matches.size() == 1)
            return {true, matches.front(), {}};
    }
    return {false, {}, "Select the connection to confirm this monitor."};
}
bool ProfileSwitchResult::complete() const
{
    if (monitors.isEmpty())
        return false;
    for (const auto &m : monitors)
        if (!m.result.sent())
            return false;
    return true;
}
QStringList ProfileSwitchResult::failedMonitors() const
{
    QStringList result;
    for (const auto &m : monitors)
        if (!m.result.sent())
            result.push_back(m.monitorId);
    return result;
}
ProfileSwitchResult SendProfileAction(const DesktopWorkspace &w, const QString &profileId,
                                      const QString &actionId, const Discovery &discovery,
                                      const QMap<QString, DesktopMonitor> &confirmed, const QStringList &only,
                                      const DesktopSender &sender,
                                      const std::function<Discovery()> &rediscover)
{
    ProfileSwitchResult result{profileId, actionId, {}, discovery};
    const auto *p = FindProfile(w, profileId);
    if (!p)
        return result;
    const ProfileAction *action = nullptr;
    for (const auto &a : p->actions)
        if (a.id == actionId && a.enabled)
            action = &a;
    if (!action)
        return result;
    for (const auto &id : p->monitors)
    {
        if (!only.isEmpty() && !only.contains(id))
            continue;
        MonitorCommandResult entry;
        entry.monitorId = id;
        const auto *m = FindMonitor(w, id);
        if (!m)
        {
            entry.status = "Missing saved monitor.";
            result.monitors.push_back(entry);
            continue;
        }
        entry.target.method = m->method;
        entry.target.source = m->method == "standard" ? "0x51" : m->source;
        const auto current = rediscover ? rediscover() : discovery;
        result.discovery = current;
        const auto resolution = ResolveSavedMonitor(*m, current, confirmed);
        entry.inputId = action->inputs.value(id);
        const auto *input = FindMonitorInput(*m, entry.inputId);
        if (!input)
        {
            entry.status = "Choose an input in this action.";
            result.monitors.push_back(entry);
            continue;
        }
        entry.code = input->code;
        if (!resolution.available)
        {
            entry.status = resolution.reason;
            result.monitors.push_back(entry);
            continue;
        }
        entry.target.adapter = resolution.target.adapter;
        entry.target.display = resolution.target.display;
        entry.target.bus = resolution.target.bus;
        entry.attempted = true;
        entry.result = sender(entry.target, input->code);
        entry.status = entry.result.sent()
                           ? "Command sent · physical switch unverified"
                           : (entry.result.detail.empty()
                                  ? QString("Command failed (code %1).").arg(entry.result.nativeCode)
                                  : QString::fromStdString(entry.result.detail));
        result.monitors.push_back(entry);
    }
    return result;
}
QString ProfileSession::previous(const QString &id) const
{
    const auto s = state(id);
    return !s.pending.monitors.isEmpty() && !s.pending.complete()
               ? s.currentAction
               : QString::fromStdString(s.history.previous());
}
QString ProfileSession::cycle(const MonitorProfile &p) const
{
    int position = -1;
    const auto current = state(p.id).currentAction;
    for (int i = 0; i < p.actions.size(); ++i)
        if (p.actions[i].id == current)
            position = i;
    for (int n = 1; n <= p.actions.size(); ++n)
        if (p.actions[(position + n) % p.actions.size()].enabled)
            return p.actions[(position + n) % p.actions.size()].id;
    return {};
}
void ProfileSession::accept(const DesktopWorkspace &w, const ProfileSwitchResult &result, bool retry)
{
    auto combined = result;
    auto &runtime = states_[result.profileId];
    if (retry && runtime.pending.actionId == result.actionId)
    {
        combined = runtime.pending;
        for (const auto &entry : result.monitors)
            for (auto &old : combined.monitors)
                if (old.monitorId == entry.monitorId)
                    old = entry;
    }
    // A different profile may have changed the same physical monitor. Its old
    // group history/retry is no longer a reliable description of accepted sends.
    for (auto it = states_.begin(); it != states_.end(); ++it)
    {
        if (it.key() == result.profileId)
            continue;
        const auto *other = FindProfile(w, it.key());
        bool overlap = false;
        if (other)
            for (const auto &entry : result.monitors)
                if (entry.result.sent() && other->monitors.contains(entry.monitorId))
                    overlap = true;
        if (overlap)
            it.value() = {};
    }
    runtime.pending = combined.complete() ? ProfileSwitchResult{} : combined;
    if (combined.complete())
    {
        runtime.currentAction = result.actionId;
        runtime.history.sent(result.actionId.toStdString());
    }
}
void ProfileSession::configurationChanged(const DesktopWorkspace &before, const DesktopWorkspace &after)
{
    for (auto it = states_.begin(); it != states_.end();)
    {
        const auto *old = FindProfile(before, it.key()), *next = FindProfile(after, it.key());
        if (!old || !next || ProfileSignature(before, *old) != ProfileSignature(after, *next))
            it = states_.erase(it);
        else
            ++it;
    }
}
QJsonObject ProfileCommandDiagnostics(const DesktopWorkspace &w, const ProfileSwitchResult &r)
{
    QJsonArray entries;
    for (const auto &entry : r.monitors)
    {
        int index = -1;
        for (int i = 0; i < w.monitors.size(); ++i)
            if (w.monitors[i].id == entry.monitorId)
                index = i;
        unsigned int input = 0, source = 0;
        const bool validInput = ParseInputValue(entry.code.toStdString(), 0xffff, input);
        const bool validSource = ParseInputValue(entry.target.source.toStdString(), 0xff, source);
        entries.append(
            QJsonObject{{"monitor_index", index},
                        {"adapter", entry.target.adapter},
                        {entry.target.bus ? "i2c_bus" : "display", entry.target.display},
                        {"source", validSource ? QString("0x%1").arg(source, 2, 16, QChar('0')) : "invalid"},
                        {"feature", entry.target.method == "standard" ? "0x60" : "0xf4"},
                        {"input", validInput ? QString("0x%1").arg(input, 4, 16, QChar('0')) : "invalid"},
                        {"status", entry.result.sent() ? "command sent; physical switch unverified"
                                                       : "failed or unavailable"},
                        {"native_code", entry.result.nativeCode},
                        {"attempted", entry.attempted}});
    }
    return {{"complete", r.complete()}, {"monitors", entries}};
}
