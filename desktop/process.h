#pragma once
#include <QtCore>

struct DesktopProcessResult
{
    QString status;
    int exitCode = -1;
    QByteArray output;
};

// No shell or elevation. Used for discovery and explicit read-only diagnostics.
inline DesktopProcessResult RunDesktopProcess(const QString &program, const QStringList &arguments,
                                               int timeoutMs)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, arguments);
    if (!process.waitForStarted(3000))
        return {"unavailable", -1, {}};
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(3000);
        return {"timed out", -1, {}};
    }
    return {process.exitStatus() == QProcess::NormalExit ? "finished" : "crashed",
            process.exitCode(), process.readAll().left(65536)};
}
