#pragma once
#include <QtCore>

// Desktop Entry values have two escaping layers: string values and Exec quoting.
inline QString DesktopExecArgument(const QString &value)
{
    QString quoted;
    for (const auto ch : value)
    {
        if (ch == '\\' || ch == '"' || ch == '$' || ch == '`')
            quoted += '\\';
        quoted += ch == '%' ? QStringLiteral("%%") : QString(ch);
    }
    quoted.replace("\\", "\\\\");
    return '"' + quoted + '"';
}
inline QString DesktopAutostartPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) +
           "/autostart/lginputswitch.desktop";
}
inline bool DesktopAutostartEnabled(const QString &path = DesktopAutostartPath())
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    bool entry = false, enabled = false;
    for (const auto &line : file.readAll().split('\n'))
    {
        const auto text = line.trimmed();
        if (text.startsWith('['))
            entry = text == "[Desktop Entry]";
        else if (entry && text == "Type=Application")
            enabled = true;
        else if (entry && text == "Hidden=true")
            return false;
    }
    return enabled;
}
inline bool SetDesktopAutostart(bool enabled, QString &error, const QString &path = DesktopAutostartPath(),
                                const QString &executable = QCoreApplication::applicationFilePath())
{
    if (!enabled && !QFile::exists(path))
        return true;
    if (executable.contains('\n') || executable.contains('\r') || executable.contains('=') ||
        !QFileInfo(executable).isAbsolute())
    {
        error = "The application path cannot be used for desktop startup.";
        return false;
    }
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        error = "Could not create the desktop startup folder.";
        return false;
    }
    // Hidden=true also overrides a future system-wide autostart entry.
    const auto contents = QString("[Desktop Entry]\nType=Application\nName=LG Input Switch\n"
                                  "Comment=Switch LG monitor inputs\nExec=%1 --background\n"
                                  "Icon=lginputswitch\nTerminal=false\nHidden=%2\n")
                              .arg(DesktopExecArgument(executable), enabled ? "false" : "true")
                              .toUtf8();
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(contents) != contents.size() || !file.commit())
    {
        error = "Could not update desktop startup: " + file.errorString();
        return false;
    }
    return true;
}
