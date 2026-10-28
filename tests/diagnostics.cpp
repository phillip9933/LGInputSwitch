#include "../desktop/diagnostics.h"
#include "../desktop/process.h"
#include <iostream>

static void Check(bool condition, const char *message)
{
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationVersion("diagnostic-test");
    if (app.arguments().contains("--hang")) { QThread::sleep(10); return 0; }
    if (app.arguments().contains("--fixture")) { std::cout << "fixture"; return 0; }
    Check(ParseDiagnosticVcp("VCP 10 C 55 100\nserial=PRIVATE", 0x10)["value"] == "55",
          "Brightness result was not parsed");
    Check(ParseDiagnosticVcp("VCP 60 SNC x0f\n", 0x60)["value"] == "x0f",
          "Input result was not parsed");
    Check(!ParseDiagnosticVcp("VCP 10 C 55 100", 0x60)["read"].toBool(), "Wrong feature accepted");
    Check(!ParseDiagnosticVcp("VCP 60 ERR NULL", 0x60)["read"].toBool(), "Error treated as a read");
    DiagnosticSnapshot snapshot;
    snapshot.selected = true; snapshot.unsaved = true; snapshot.config.display = 31;
    snapshot.config.source = "PRIVATE /home/example";
    snapshot.config.inputs = {{"PRIVATE serial label", "not PRIVATE", "PRIVATE shortcut", true}};
    const auto text = FormatDesktopDiagnostics(snapshot, CollectDesktopDiagnostics(snapshot, true), true);
    Check(!text.contains("PRIVATE") && !text.contains("/home/example"), "Report exported private config text");
    const auto report = QJsonDocument::fromJson(text.toUtf8()).object();
    Check(report["source_address"] == "invalid" && report["settings_unsaved"].toBool(),
          "Invalid source or unsaved state lost");
    Check(report["last_command"].isString(), "No-command state missing");
    QTemporaryDir temporary;
    QString error;
    Check(SaveDesktopDiagnostics(temporary.filePath("report.json"), text, error), "Report save failed");
    QFile saved(temporary.filePath("report.json"));
    Check(saved.open(QIODevice::ReadOnly) && saved.readAll() == text.toUtf8(), "Saved report differs from preview");
    Check(!SaveDesktopDiagnostics(temporary.filePath("missing/report.json"), text, error),
          "Unwritable report target succeeded");
    const auto process = RunDesktopProcess(app.applicationFilePath(), {"--fixture"}, 3000);
    Check(process.status == "finished" && process.exitCode == 0 && process.output == "fixture",
          "Bounded process output failed");
    QElapsedTimer timer; timer.start();
    Check(RunDesktopProcess(app.applicationFilePath(), {"--hang"}, 100).status == "timed out",
          "Stalled diagnostic process was not stopped");
    Check(timer.elapsed() < 5000, "Diagnostic timeout was not bounded");
    std::cout << "Diagnostic privacy, parsing, export and timeout checks passed\n";
}
