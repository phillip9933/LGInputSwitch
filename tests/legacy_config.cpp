#include "../desktop/legacy_config.h"
#include <iostream>

static void check(bool value, const char *message) {
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
static const DesktopInput *input(const DesktopConfig &config, const QString &label) {
    for (const auto &row : config.inputs) if (row.label == label) return &row;
    return nullptr;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    app.setOrganizationName("LGInputSwitchTests");
    app.setApplicationName("LegacyImport");
    QTemporaryDir temporary;
    check(temporary.isValid(), "Temporary directory unavailable");
    const QString unicode = QString::fromUtf8("Work – 作業");
    const QJsonObject json{
        {"targets", QJsonArray{QJsonArray{7, 3}, QJsonArray{9, 4}}},
        {"inputs", QJsonArray{QJsonObject{{"label", unicode}, {"code", "0x1234"}},
                               QJsonObject{{"label", "HDMI1"}, {"code", "0x92"}}}},
        {"cycleOrder", QJsonArray{"HDMI1", "not enabled", unicode, "HDMI1"}},
        {"i2cSourceAddr", "0x51"},
        {"hotkeys", QJsonObject{{"cycle", "WIN+ALT+F8"},
                                 {"direct", QJsonObject{{unicode, "CTRL+ALT+F10"}, {"HDMI1", "CTRL+ALT+9"}}}}},
        {"debounceMs", 321}, {"showNotifications", false}, {"startWithWindows", true}};
    const auto bytes = QJsonDocument(json).toJson();
    const QJsonArray stored{
        QJsonObject{{"label", "Disabled custom"}, {"code", "0xF1"}, {"shortcut", "CTRL+ALT+F11"}},
        QJsonObject{{"label", "HDMI1"}, {"code", "0xdead"}, {"shortcut", "CTRL+ALT+1"}}};
    QString warning;
    const auto imported = ImportLegacyWindowsConfig(bytes, stored, warning);
    check(warning.isEmpty() && imported.adapter == 7 && imported.display == 3 && imported.source == "0x51" &&
              imported.debounce == 321 && !imported.notifications && imported.startup &&
              imported.cycleShortcut == "WIN+ALT+F8", "Legacy target/preferences changed during import");
    check(imported.inputs[0].label == "HDMI1" && imported.inputs[0].code == "0x92" &&
              imported.inputs[0].shortcut == "CTRL+ALT+9" && imported.inputs[1].label == unicode &&
              imported.inputs[1].code == "0x1234" && imported.inputs[1].shortcut == "CTRL+ALT+F10",
          "Input order, Unicode labels, custom codes or shortcuts changed during import");
    check(input(imported, "Disabled custom") && !input(imported, "Disabled custom")->enabled &&
              input(imported, "Disabled custom")->shortcut == "CTRL+ALT+F11" &&
              input(imported, "DisplayPort") && !input(imported, "DisplayPort")->enabled,
          "Disabled/custom inputs were not retained for review");
    const auto path = temporary.filePath("config.json");
    QFile file(path);
    check(file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(), "Import fixture unavailable");
    file.close();
    const auto loaded = LoadLegacyWindowsConfig(path, stored, warning);
    check(loaded.inputs[1].label == unicode && warning.isEmpty(), "Read-only file import failed");
    check(file.open(QIODevice::ReadOnly) && file.readAll() == bytes, "Import modified the original config.json");
    file.close();
    auto wrongTypes = json;
    wrongTypes["targets"] = QJsonArray{QJsonArray{2.5, 3}, QJsonArray{1e30, 0}, QJsonArray{false, 0}, QJsonArray{4, 6}};
    wrongTypes["debounceMs"] = "42";
    wrongTypes["showNotifications"] = "false";
    wrongTypes["hotkeys"] = QJsonObject{{"direct", QJsonObject{}}};
    const auto strict = ImportLegacyWindowsConfig(QJsonDocument(wrongTypes).toJson(), {}, warning);
    check(strict.adapter == 4 && strict.display == 6 && strict.debounce == 750 && strict.notifications &&
              input(strict, "HDMI1")->shortcut.isEmpty(), "Wrong JSON types changed import/default behavior");
    for (const QByteArray invalid : {QByteArray("{broken"), QByteArray("[]"), QByteArray(2 * 1024 * 1024 + 1, ' ')}) {
        const auto fallback = ImportLegacyWindowsConfig(invalid, {}, warning);
        check(!warning.isEmpty() && fallback.inputs.size() == 4 && fallback.inputs[0].code == "0xD0",
              "Unreadable config did not produce defaults and a setup warning");
    }
    check(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "Broken-file fixture unavailable");
    file.write("{broken"); file.close();
    LoadLegacyWindowsConfig(path, {}, warning);
    check(!warning.isEmpty() && file.open(QIODevice::ReadOnly) && file.readAll() == "{broken",
          "Import repaired or overwrote an unreadable legacy file");
    file.close();
    LoadLegacyWindowsConfig(temporary.filePath("missing.json"), {}, warning);
    check(!warning.isEmpty() && !QFile::exists(temporary.filePath("missing.json")), "Import created a missing legacy file");
#ifndef Q_OS_WIN
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temporary.path());
    qputenv("XDG_CONFIG_HOME", temporary.path().toUtf8());
    QSettings settings;
    settings.setValue("appearance", "dark"); settings.setValue("previousShortcut", "CTRL+ALT+F7");
    settings.setValue("target", 12); settings.setValue("bus", true);
    settings.setValue("inputs", QJsonDocument(QJsonArray{QJsonObject{
        {"label", unicode}, {"code", "0x1234"}, {"shortcut", "CTRL+ALT+F10"}, {"enabled", false}}}).toJson());
    settings.sync();
    const auto linuxConfig = LoadLegacyDesktopConfig(warning);
    check(warning.isEmpty() && linuxConfig.appearance == "dark" && linuxConfig.previousShortcut == "CTRL+ALT+F7" &&
              linuxConfig.display == 12 && linuxConfig.bus && linuxConfig.inputs.size() == 1 && !linuxConfig.inputs[0].enabled &&
              linuxConfig.inputs[0].label == unicode, "Pre-profile Linux settings changed during import");
#endif
}
