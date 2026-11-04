#pragma once
#include <QtCore>

struct DesktopProcessResult
{
    QString status;
    int exitCode = -1;
    QByteArray output;
    QByteArray errorOutput;
};

// No shell or elevation. Used for discovery, diagnostics and explicit switch commands.
inline DesktopProcessResult RunDesktopProcess(const QString &program, const QStringList &arguments,
                                               int timeoutMs, int outputLimit = 65536)
{
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForStarted(3000))
        return {"unavailable", -1, {}};
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(3000);
        return {"timed out", -1, {}};
    }
    const auto output = process.readAllStandardOutput();
    const auto errors = process.readAllStandardError();
    if (output.size() + errors.size() > outputLimit)
        return {"output limit exceeded", process.exitCode(), {}};
    return {process.exitStatus() == QProcess::NormalExit ? "finished" : "crashed",
            process.exitCode(), output, errors};
}
