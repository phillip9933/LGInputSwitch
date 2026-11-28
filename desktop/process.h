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
    QByteArray output, errors;
    bool exceeded = false;
    QProcess process;
    const auto drain = [&] {
        const auto stdoutBytes = process.readAllStandardOutput();
        const auto stderrBytes = process.readAllStandardError();
        if (exceeded)
            return;
        if (stdoutBytes.size() + stderrBytes.size() > outputLimit - output.size() - errors.size())
        {
            exceeded = true;
            process.kill();
            return;
        }
        output.append(stdoutBytes);
        errors.append(stderrBytes);
    };
    QObject::connect(&process, &QProcess::readyReadStandardOutput, &process, drain);
    QObject::connect(&process, &QProcess::readyReadStandardError, &process, drain);
    process.start(program, arguments);
    if (!process.waitForStarted(3000))
        return {"unavailable", -1, {}, {}};
    const bool finished = process.state() == QProcess::NotRunning || process.waitForFinished(timeoutMs);
    drain();
    if (exceeded || !finished)
    {
        if (process.state() != QProcess::NotRunning)
        {
            process.kill();
            process.waitForFinished(3000);
        }
        return {exceeded ? "output limit exceeded" : "timed out", -1, {}, {}};
    }
    return {process.exitStatus() == QProcess::NormalExit ? "finished" : "crashed",
            process.exitCode(), output, errors};
}
