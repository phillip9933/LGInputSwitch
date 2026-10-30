#pragma once
#include "../core/input_history.h"
#include "platform.h"
#include <functional>

// Persistent IDs belong to the app. Identity is a local fingerprint, never a
// native display number, and is deliberately excluded from support reports.
struct SavedMonitor
{
    QString id, name, identity;
    DesktopMonitor route;
    QString source = "0x50", method = "lg";
    QVector<DesktopInput> inputs;
};
struct ProfileAction
{
    QString id, name, shortcut;
    bool enabled = true;
    QMap<QString, QString> inputs; // saved monitor ID -> physical input ID
};
struct MonitorProfile
{
    QString id, name;
    QStringList monitors;
    QVector<ProfileAction> actions;
    QString cycleShortcut, previousShortcut;
};
struct DesktopWorkspace
{
    DesktopConfig preferences; // app-wide appearance, notifications, startup, debounce
    QVector<SavedMonitor> monitors;
    QVector<MonitorProfile> profiles;
    QString activeProfile;
};
QString NewDesktopId();
QVector<DesktopInput> DefaultMonitorInputs(const QString &method);
DesktopWorkspace MigrateDesktopConfig(const DesktopConfig &legacy);
QJsonObject WorkspaceJson(const DesktopWorkspace &workspace);
bool ParseWorkspace(const QJsonObject &json, DesktopWorkspace &workspace, QString &error);
bool ValidateWorkspace(const DesktopWorkspace &workspace, QString &error);
DesktopWorkspace LoadDesktopWorkspace(QString &warning, const QString &storagePath = {});
bool SaveDesktopWorkspace(const DesktopWorkspace &workspace, QString &error, const QString &storagePath = {});
QString WorkspacePath(); // Windows portable file; empty on Linux (QSettings)

const SavedMonitor *FindMonitor(const DesktopWorkspace &workspace, const QString &id);
const MonitorProfile *FindProfile(const DesktopWorkspace &workspace, const QString &id);
const DesktopInput *FindMonitorInput(const SavedMonitor &monitor, const QString &id);
QString MonitorRouteKey(const DesktopMonitor &monitor);
QString MonitorIdentity(const QByteArray &edid);
Discovery ParseDesktopDiscovery(const QByteArray &output);
// Unique fingerprints may follow changed native indices. Missing/duplicate
// fingerprints require explicit selection; session confirmations never override
// a changed fingerprint or ambiguous route.
struct MonitorResolution
{
    bool available = false;
    DesktopMonitor target;
    QString reason;
};
MonitorResolution ResolveSavedMonitor(const SavedMonitor &monitor, const Discovery &discovery,
                                      const QMap<QString, DesktopMonitor> &confirmed = {});

struct MonitorCommandResult
{
    QString monitorId, inputId;
    DesktopConfig target;
    QString code, status;
    bool attempted = false;
    lg::SwitchResult result{lg::SwitchStatus::TransportFailure, -1, "Monitor unavailable."};
};
struct ProfileSwitchResult
{
    QString profileId, actionId;
    QVector<MonitorCommandResult> monitors;
    Discovery discovery;
    bool complete() const;
    QStringList failedMonitors() const;
};
using DesktopSender = std::function<lg::SwitchResult(const DesktopConfig &, const QString &)>;
ProfileSwitchResult SendProfileAction(const DesktopWorkspace &workspace, const QString &profileId,
                                      const QString &actionId, const Discovery &discovery,
                                      const QMap<QString, DesktopMonitor> &confirmed = {},
                                      const QStringList &only = {},
                                      const DesktopSender &sender = SendDesktopInput,
                                      const std::function<Discovery()> &rediscover = {});

struct ProfileRuntime
{
    lg::InputHistory history;
    QString currentAction;
    ProfileSwitchResult pending;
};
// Shared by GUI, tray and native shortcuts. No GUI tab index participates in
// dispatch. Partial attempts do not advance history; retries retain successes.
class ProfileSession
{
    QMap<QString, ProfileRuntime> states_;

  public:
    const ProfileRuntime state(const QString &profileId) const
    {
        return states_.value(profileId);
    }
    QString previous(const QString &profileId) const;
    QString cycle(const MonitorProfile &profile) const;
    void accept(const DesktopWorkspace &workspace, const ProfileSwitchResult &result, bool retry = false);
    void configurationChanged(const DesktopWorkspace &before, const DesktopWorkspace &after);
};
QJsonObject ProfileCommandDiagnostics(const DesktopWorkspace &workspace, const ProfileSwitchResult &result);
