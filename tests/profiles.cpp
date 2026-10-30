#include "../desktop/profiles.h"
#include <QApplication>
#include <iostream>

static void check(bool value, const char *message)
{
    if (!value)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}
static DesktopWorkspace fixture()
{
    auto w = MigrateDesktopConfig(DesktopConfig{});
    auto &first = w.monitors[0];
    first.identity = QString(64, 'a');
    first.route = {"Main", 0, 3, true, first.identity};
    const auto firstId = first.id;
    auto second = first;
    second.id = "monitor-2";
    second.name = "Side";
    second.identity = QString(64, 'b');
    second.route = {"Side", 0, 9, true, second.identity};
    second.source = "0x50"; // retained LG source is ignored by standard switching
    second.method = "standard";
    second.inputs[0].code = "0x11";
    w.monitors.push_back(second);
    auto &p = w.profiles[0];
    p.monitors.push_back(second.id);
    for (auto &a : p.actions)
        a.inputs[second.id] = second.inputs[0].id;
    auto side = p;
    side.id = "side";
    side.name = "Side only";
    side.monitors = {second.id};
    side.cycleShortcut.clear();
    side.previousShortcut.clear();
    for (auto &a : side.actions)
    {
        a.inputs.remove(firstId);
        a.shortcut.clear();
    }
    w.profiles.push_back(side);
    return w;
}
int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitchProfileTests");
    app.setApplicationName("Profiles");
    QTemporaryDir temp;
    check(temp.isValid(), "Temporary directory unavailable");
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, temp.path());
    DesktopConfig legacy;
    legacy.adapter = 4;
    legacy.display = 5;
    legacy.source = "0x51";
    legacy.appearance = "dark";
    legacy.previousShortcut = "CTRL+ALT+9";
    legacy.inputs = {{"Custom", "0x1234", "CTRL+ALT+6", true}, {"Disabled", "0xF1", "", false}};
    const auto migrated = MigrateDesktopConfig(legacy);
    check(migrated.profiles.size() == 1 && migrated.monitors.size() == 1 &&
              migrated.monitors[0].route.adapter == 4 && migrated.monitors[0].route.display == 5 &&
              migrated.monitors[0].source == "0x51" && migrated.monitors[0].inputs[0].code == "0x1234" &&
              migrated.profiles[0].actions[0].shortcut == "CTRL+ALT+6" &&
              !migrated.profiles[0].actions[1].enabled &&
              migrated.profiles[0].previousShortcut == "CTRL+ALT+9" &&
              migrated.preferences.appearance == "dark",
          "Legacy configuration was not migrated losslessly");
    check(migrated.profiles[0].id == "default" && migrated.profiles[0].actions[0].id.startsWith("input-"),
          "Legacy Plasma IDs not retained");
    QString error;
    const auto standardDefaults = DefaultMonitorInputs("standard");
    check(standardDefaults.size() == 4 && standardDefaults[0].code == "0x0f" &&
              standardDefaults[2].code == "0x11" && !standardDefaults[0].id.isEmpty(),
          "Standard input presets are not usable");
    auto winLegacy = legacy;
    winLegacy.cycleShortcut = "WIN+ALT+F8";
    check(MigrateDesktopConfig(winLegacy).profiles[0].cycleShortcut.contains("META"),
          "Legacy Windows modifier did not migrate to portable Qt keys");
    auto w = fixture();
    check(ValidateWorkspace(w, error), qPrintable(error));
    DesktopWorkspace parsed;
    check(ParseWorkspace(WorkspaceJson(w), parsed, error) && WorkspaceJson(w) == WorkspaceJson(parsed),
          "Workspace JSON roundtrip failed");
    const auto storage = temp.filePath("profiles.json");
    check(SaveDesktopWorkspace(w, error, storage), "Atomic profile save failed");
    QString warning;
    const auto loaded = LoadDesktopWorkspace(warning, storage);
    check(warning.isEmpty() && WorkspaceJson(w) == WorkspaceJson(loaded), "Saved workspace did not load");
    QFile corrupt(storage);
    check(corrupt.open(QIODevice::WriteOnly | QIODevice::Truncate), "Corrupt fixture failed");
    corrupt.write("{broken");
    corrupt.close();
    LoadDesktopWorkspace(warning, storage);
    check(warning.startsWith("Saved profiles could not be read."), "Corrupt config did not block switching");
    error.clear();
    check(!SaveDesktopWorkspace(w, error, storage), "Corrupt config was overwritten");
    check(corrupt.open(QIODevice::ReadOnly) && corrupt.readAll() == "{broken",
          "Corrupt data was not preserved");
    corrupt.close();
    auto json = WorkspaceJson(w);
    json["version"] = 999;
    check(!ParseWorkspace(json, parsed, error), "Future schema was accepted");
    auto bad = w;
    bad.profiles[1].actions[0].shortcut = bad.profiles[0].actions[0].shortcut;
    check(!ValidateWorkspace(bad, error), "Cross-profile shortcut conflict accepted");
    bad = w;
    bad.profiles[0].actions[0].inputs.remove("monitor-2");
    check(!ValidateWorkspace(bad, error), "Incomplete action mapping accepted");
    bad = w;
    bad.profiles[0].actions[0].inputs["monitor-2"] = "missing";
    check(!ValidateWorkspace(bad, error), "Dangling input ID accepted");
    bad = w;
    bad.profiles[0].monitors.push_back("monitor-1");
    check(!ValidateWorkspace(bad, error), "Duplicate group member accepted");
    bad = w;
    bad.monitors[0].inputs[0].code = "0x10000";
    check(!ValidateWorkspace(bad, error), "Invalid input code accepted");
    bad = w;
    bad.monitors[1].identity = bad.monitors[0].identity;
    check(!ValidateWorkspace(bad, error), "Duplicate physical definition accepted");

    Discovery discovery{{w.monitors[0].route, w.monitors[1].route}, {}};
    discovery.monitors[0].display = 17; // stable identity follows new bus index
    auto resolution = ResolveSavedMonitor(w.monitors[0], discovery);
    check(resolution.available && resolution.target.display == 17,
          "Stable identity did not follow reconnect");
    auto replacement = discovery;
    replacement.monitors[0].identity = QString(64, 'c');
    check(!ResolveSavedMonitor(w.monitors[0], replacement).available,
          "Replacement monitor inherited a saved route");
    auto duplicate = discovery;
    duplicate.monitors.push_back(discovery.monitors[0]);
    duplicate.monitors.back().display = 23;
    check(!ResolveSavedMonitor(w.monitors[0], duplicate).available,
          "Ambiguous identity was silently selected");
    auto uncertain = w.monitors[0];
    uncertain.identity.clear();
    check(!ResolveSavedMonitor(uncertain, discovery).available, "Unidentified monitor was silently rebound");
    QMap<QString, DesktopMonitor> confirmed{{uncertain.id, discovery.monitors[0]}};
    check(ResolveSavedMonitor(uncertain, discovery, confirmed).available, "Explicit confirmation ignored");
    check(!ResolveSavedMonitor(uncertain, replacement, confirmed).available,
          "Confirmation followed a replacement");
    const auto brief =
        ParseDesktopDiscovery("Invalid display\n I2C bus: /dev/i2c-2\n Monitor: LCD:Internal:X\n"
                              "Display 1\n I2C bus: /dev/i2c-7\n Monitor: GSM:LG 1:SERIAL1\n"
                              "Display 2\n I2C bus: /dev/i2c-8\n Monitor: GSM:LG 2:SERIAL2\n");
    check(brief.monitors.size() == 2 && brief.monitors[0].display == 7 &&
              !brief.monitors[0].identity.isEmpty() && !brief.monitors[0].label.contains("SERIAL"),
          "Identity discovery leaked serials or included invalid displays");
    QByteArray edid(128, '\0');
    edid.replace(0, 8, QByteArray::fromHex("00ffffffffffff00"));
    unsigned sum = 0;
    for (int i = 0; i < 127; ++i)
        sum += static_cast<unsigned char>(edid[i]);
    edid[127] = char(-sum);
    check(MonitorIdentity(edid).size() == 64 &&
              MonitorIdentity(edid + QByteArray(128, '\x12')) == MonitorIdentity(edid),
          "EDID base identity incorrect");
    edid[20] = 1;
    check(MonitorIdentity(edid).isEmpty(), "Broken EDID checksum accepted");

    QVector<QPair<int, QString>> sends;
    auto sender = [&](const DesktopConfig &c, const QString &code) {
        sends.push_back({c.display, code});
        check(c.source == (c.display == 9 ? "0x51" : "0x50"), "Monitor-specific source was lost");
        return lg::SwitchResult{lg::SwitchStatus::CommandSent, 0, "Sent"};
    };
    const auto profileId = w.profiles[0].id, firstAction = w.profiles[0].actions[0].id,
               secondAction = w.profiles[0].actions[1].id;
    auto first = SendProfileAction(w, profileId, firstAction, discovery, {}, {}, sender);
    check(first.complete() && sends.size() == 2 && sends[0] == qMakePair(17, QString("0xD0")) &&
              sends[1] == qMakePair(9, QString("0x11")),
          "Grouped action did not dispatch distinct monitor mappings");
    int enumeration = 0;
    QVector<int> liveRoutes;
    const auto changing = SendProfileAction(
        w, profileId, firstAction, {}, {}, {},
        [&](const DesktopConfig &c, const QString &) {
            liveRoutes.push_back(c.display);
            return lg::SwitchResult{lg::SwitchStatus::CommandSent, 0, "Sent"};
        },
        [&] {
            auto current = discovery;
            if (++enumeration > 1)
                current.monitors[1].display = 20;
            return current;
        });
    check(changing.complete() && liveRoutes == QVector<int>{17, 20},
          "Topology change between group commands reused a stale route");
    ProfileSession session;
    session.accept(w, first);
    check(session.previous(profileId).isEmpty() && session.cycle(w.profiles[0]) == secondAction,
          "First action history/cycle incorrect");
    auto failing = [&](const DesktopConfig &c, const QString &code) {
        if (c.display == 9)
            return lg::SwitchResult{lg::SwitchStatus::TransportFailure, 87, "Failed"};
        return sender(c, code);
    };
    auto partial = SendProfileAction(w, profileId, secondAction, discovery, {}, {}, failing);
    check(!partial.complete() && partial.failedMonitors() == QStringList{"monitor-2"},
          "Partial failure was hidden");
    session.accept(w, partial);
    check(session.state(profileId).currentAction == firstAction && session.previous(profileId) == firstAction,
          "Partial group advanced history or lost restore action");
    sends.clear();
    auto retry =
        SendProfileAction(w, profileId, secondAction, discovery, {}, partial.failedMonitors(), sender);
    session.accept(w, retry, true);
    check(sends.size() == 1 && sends[0].first == 9 && session.state(profileId).pending.monitors.isEmpty() &&
              session.previous(profileId) == firstAction &&
              session.state(profileId).currentAction == secondAction,
          "Retry resent successes or did not commit completed group history");
    // Shared monitors invalidate the other profile's old group history.
    auto side = SendProfileAction(w, "side", w.profiles[1].actions[2].id, discovery, {}, {}, sender);
    session.accept(w, side);
    check(session.previous(profileId).isEmpty() && session.state(profileId).currentAction.isEmpty(),
          "Overlapping profile retained stale history");
    // Disjoint profiles retain independent history and cycle positions.
    auto disjoint = w;
    disjoint.profiles[0].monitors = {"monitor-1"};
    for (auto &a : disjoint.profiles[0].actions)
        a.inputs.remove("monitor-2");
    ProfileSession independent;
    independent.accept(disjoint,
                       SendProfileAction(disjoint, profileId, firstAction, discovery, {}, {}, sender));
    independent.accept(disjoint,
                       SendProfileAction(disjoint, profileId, secondAction, discovery, {}, {}, sender));
    independent.accept(disjoint, SendProfileAction(disjoint, "side", firstAction, discovery, {}, {}, sender));
    check(independent.previous(profileId) == firstAction &&
              independent.state("side").currentAction == firstAction,
          "Independent profile histories interfered");
    // Disconnected monitor must not stop later members or receive a command.
    sends.clear();
    auto disconnected = discovery;
    disconnected.monitors.removeFirst();
    const auto missing = SendProfileAction(w, profileId, firstAction, disconnected, {}, {}, sender);
    check(!missing.complete() && sends.size() == 1 && sends[0].first == 9,
          "Disconnected member blocked remaining group or received a write");
    sends.clear();
    check(SendProfileAction(w, "missing", firstAction, discovery, {}, {}, sender).monitors.isEmpty() &&
              sends.isEmpty(),
          "Unknown profile dispatched hardware commands");
    auto disabled = w;
    disabled.profiles[0].actions[0].enabled = false;
    check(SendProfileAction(disabled, profileId, firstAction, discovery, {}, {}, sender).monitors.isEmpty(),
          "Disabled action was dispatched");
    session.accept(w, first);
    session.accept(w, partial);
    auto edited = w;
    edited.monitors[1].source = "0x52";
    session.configurationChanged(w, edited);
    check(session.state(profileId).pending.monitors.isEmpty(), "Config edit retained a stale retry");
    auto diagnostic = QJsonDocument(ProfileCommandDiagnostics(w, partial)).toJson();
    check(!diagnostic.contains("Main") && !diagnostic.contains(firstAction.toUtf8()) &&
              !diagnostic.contains(w.monitors[0].identity.toUtf8()) && diagnostic.contains("0x00d1"),
          "Command diagnostic leaked identities/labels or lost input values");
    std::cout
        << "Profile migration, persistence, resolution, grouped dispatch, retry and history checks passed\n";
}
