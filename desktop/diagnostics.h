#pragma once
#include "platform.h"

struct DiagnosticMonitorSnapshot
{
    DesktopConfig config;
    bool selected = false;
    int monitorIndex = -1;
};

struct DiagnosticSnapshot
{
    DesktopConfig config;
    bool selected = false, unsaved = false;
    QJsonObject lastCommand;
    QVector<DiagnosticMonitorSnapshot> monitors;
    int profileCount = 1;
    QJsonArray actions;
};

// Only explicitly selected, bounded numeric controls are included in the report.
QJsonObject ParseDiagnosticVcp(const QByteArray &output, unsigned int feature);
QJsonObject ParseDiagnosticCapabilities(const QByteArray &response);
QJsonObject CollectDesktopDiagnostics(const DiagnosticSnapshot &snapshot, bool preview);
QString FormatDesktopDiagnostics(const DiagnosticSnapshot &snapshot, const QJsonObject &checks, bool preview);
bool SaveDesktopDiagnostics(const QString &path, const QString &report, QString &error);
#ifdef Q_OS_WIN
// Runs only in an isolated --diagnostic-probe child process, before the GUI/ADL starts.
QJsonObject ProbeWindowsMonitorControls();
#endif
