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
    if (app.arguments().contains("--large")) { std::cout << std::string(65537, 'x'); return 0; }
    const auto caps = ParseDiagnosticCapabilities(
        "(prot(monitor)model(PRIVATE serial)cmds(01 02 03)"
        "vcp(10 12 14(05 08 0B) 60(11 0F 12 11 99) F4(0090 00D0))mccs_ver(2.2))");
    Check(caps["parse_status"] == "parsed" && caps["mccs_version"] == "2.2", "Capabilities were not parsed");
    Check(caps["advertised_vcp_codes"].toArray() == QJsonArray({"0x10", "0x12", "0x14", "0x60", "0xf4"}),
          "Advertised feature codes differ");
    const auto advertised = caps["advertised_input_values"].toObject();
    const auto standard = advertised["0x60"].toObject()["values"].toArray();
    Check(standard.size() == 4 && standard[0].toObject()["standard_name"] == "DisplayPort 1" &&
          standard[1].toObject()["standard_name"] == "HDMI 1" &&
          !standard[3].toObject().contains("standard_name"), "Input values/names or duplicate handling differ");
    const auto vendor = advertised["0xf4"].toObject()["values"].toArray();
    Check(vendor.size() == 2 && vendor[0].toObject()["code"] == "0x90" &&
          !vendor[0].toObject().contains("standard_name"), "Vendor values were guessed or lost");
    Check(!QJsonDocument(caps).toJson().contains("PRIVATE"), "Raw capability identity leaked");
    const auto nested = ParseDiagnosticCapabilities("(model(PRIVATE vcp(60(11)))vcp(10 F4))");
    const auto nestedInputs = nested["advertised_input_values"].toObject();
    Check(nestedInputs["0x60"].toObject()["control_advertised"] == false &&
          nestedInputs["0xf4"].toObject()["control_advertised"] == true &&
          nestedInputs["0xf4"].toObject()["values_listed"] == false,
          "Nested identity field injected controls or missing values were fabricated");
    Check(ParseDiagnosticCapabilities("Unparsed capabilities string: (vcp(60(0F 11)))")["parse_status"] == "parsed",
          "ddcutil terse prefix unsupported");
    Check(ParseDiagnosticCapabilities("vcp(60())")["advertised_input_values"].toObject()["0x60"].toObject()["values_listed"] == true,
          "Empty advertised values confused with no list");
    for (const QByteArray malformed : {QByteArray("(vcp(60(11)"), QByteArray("(vcp(10) vcp(60(11)))"),
          QByteArray("(vcp(600(11)))"), QByteArray("(vcp(60(zz)))"), QByteArray("(vcp(60(11(12))))"),
          QByteArray("(vcp(60(10000)))"), QByteArray("(vcp(10)) trailing PRIVATE"),
          QByteArray(17, '(') + QByteArray(17, ')'), QByteArray(65537, 'x')})
    {
        const auto result = ParseDiagnosticCapabilities(malformed);
        Check(result["parse_status"] != "parsed" && !result.contains("advertised_vcp_codes"),
              "Malformed/oversized capabilities accepted or partially exported");
    }
    Check(ParseDiagnosticCapabilities("(model(PRIVATE))")["parse_status"] == "vcp section not advertised",
          "Missing capability list not identified");
    QByteArray tooMany("(vcp(60(");
    for (unsigned int i = 0; i <= 256; ++i) tooMany += QByteArray::number(i, 16) + ' ';
    tooMany += ")))";
    Check(ParseDiagnosticCapabilities(tooMany)["parse_status"] == "input value list too large",
          "Input value list was not bounded");
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
    Check(RunDesktopProcess(app.applicationFilePath(), {"--large"}, 3000).status == "output limit exceeded",
          "Oversized process output silently truncated");
    QElapsedTimer timer; timer.start();
    Check(RunDesktopProcess(app.applicationFilePath(), {"--hang"}, 100).status == "timed out",
          "Stalled diagnostic process was not stopped");
    Check(timer.elapsed() < 5000, "Diagnostic timeout was not bounded");
    std::cout << "Diagnostic privacy, parsing, export and timeout checks passed\n";
}
