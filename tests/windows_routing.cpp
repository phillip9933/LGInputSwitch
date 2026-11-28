#include "../desktop/windows_monitors.h"
#include "../desktop/input_models.h"
#include "../desktop/monitor_identity.h"
#include "../desktop/diagnostics.h"
#include <iostream>

static void check(bool value, const char *message)
{
    if (!value) { std::cerr << message << '\n'; std::exit(1); }
}
int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    check(NativeWindowsAdapterKind("PCI\\VEN_8086&DEV_1234", "Intel Graphics") == "intel" &&
              NativeWindowsAdapterKind("pci\\ven_8086&dev_1234", "Graphics") == "intel" &&
              NativeWindowsAdapterKind("PCI\\VEN_8086&DEV_1234", "DisplayLink adapter") == "displaylink" &&
              NativeWindowsAdapterKind("PCI\\VEN_80860&DEV_1234", "Intel Graphics") == "other",
          "Intel identification confused another vendor or a DisplayLink route");
    check(SafeMonitorHardware({{"adapter_kind", "intel"}, {"serial", "private"}}) ==
              QJsonObject{{"edid_valid", false}, {"adapter_kind", "intel"}},
          "Intel diagnostic metadata was dropped or leaked identifying fields");
    const QString name = "\\\\.\\DISPLAY6", identity(64, 'a');
    const QJsonObject record{{"windows_display", name}, {"identity", identity},
                             {"label", "LG monitor · Display 6 · Standard DDC/CI"},
                             {"hardware", QJsonObject{{"edid_valid", true}, {"manufacturer", "GSM"},
                                                      {"product_code", 0x1234}, {"adapter_kind", "amd"}}}};
    const auto json = QJsonDocument(QJsonObject{{"monitors", QJsonArray{record}}}).toJson();
    const auto native = ParseNativeWindowsDiscovery(json);
    check(native.monitors.size() == 1 && native.monitors[0].transport == WindowsDdcTransport &&
              native.monitors[0].adapter == -1 && native.monitors[0].display == 6 &&
              native.monitors[0].nativeDisplay == name,
          "DisplayLink/native route was incorrectly interpreted as an AMD target");
    for (const auto &invalid : {QString("DISPLAY6"), QString("\\\\.\\DISPLAY0"),
                               QString("\\\\.\\DISPLAY06"), QString("\\\\.\\DISPLAY6 extra")})
    {
        check(NativeWindowsDisplayNumber(invalid) < 0, "Invalid native display identifier accepted");
        auto changed = record; changed["windows_display"] = invalid;
        check(ParseNativeWindowsDiscovery(QJsonDocument(QJsonObject{{"monitors", QJsonArray{changed}}}).toJson())
                  .monitors.isEmpty(), "Invalid native target entered discovery");
    }
    Discovery amd;
    amd.monitors.push_back({"AMD connected LG", 2, 17, false, identity});
    check(MergeWindowsDiscovery(amd, native).monitors.size() == 1,
          "Uniquely identified AMD display was duplicated or downgraded to native DDC/CI");
    auto displaylink = native;
    displaylink.monitors[0].hardware["adapter_kind"] = "displaylink";
    check(MergeWindowsDiscovery(amd, displaylink).monitors.size() == 2,
          "Matching EDID hid a DisplayLink route behind an AMD-reported route");
    amd.monitors[0].identity = QString(64, 'b');
    const auto mixed = MergeWindowsDiscovery(amd, native);
    check(mixed.monitors.size() == 2 && mixed.monitors[0].adapter == 2 &&
              mixed.monitors[1].adapter == -1,
          "Dock display was hidden merely because an AMD GPU exists");
    check(MergeWindowsDiscovery({}, native).monitors.size() == 1,
          "Native discovery depended on an AMD adapter");
    auto clones = native;
    auto clone = clones.monitors[0]; clone.nativeDisplay = "\\\\.\\DISPLAY7"; clone.display = 7;
    clones.monitors.push_back(clone);
    amd.monitors[0].identity = identity;
    check(MergeWindowsDiscovery(amd, clones).monitors.size() == 3,
          "Ambiguous fingerprints were silently deduplicated");

    DesktopConfig config;
    config.transport = WindowsDdcTransport;
    config.method = "standard";
    config.nativeDisplay = name;
    config.targetIdentity = identity;
    int sends = 0;
    const auto write = [&](const DesktopMonitor &target, unsigned int value) {
        ++sends;
        check(target.nativeDisplay == name && target.identity == identity && value == 0x11,
              "Native write used a different monitor or input");
        return lg::SwitchResult{lg::SwitchStatus::CommandSent, 0, "Synthetic write"};
    };
    check(DispatchNativeWindowsInput(config, "0x11", mixed, write).sent() && sends == 1,
          "Explicit native standard input did not dispatch exactly once");
    config.method = "lg";
    auto result = DispatchNativeWindowsInput(config, "0x90", mixed, write);
    check(!result.sent() && sends == 1 && result.detail.find("custom source") != std::string::npos,
          "LG alternate command silently fell back to Windows standard DDC/CI");
    auto nvidia = native;
    nvidia.monitors[0].hardware["adapter_kind"] = "nvidia";
    int nvidiaSends = 0;
    const auto rawWrite = [&](const DesktopMonitor &target, const lg::InputCommand &command) {
        ++nvidiaSends;
        check(target.identity == identity && command.featureCode == 0xf4 &&
                  command.sourceAddress == 0x50 && command.value == 0x90,
              "NVIDIA writer did not get the exact LG command on the intended monitor");
        return lg::SwitchResult{lg::SwitchStatus::CommandSent, 0, "Synthetic raw write"};
    };
    check(DispatchNativeWindowsInput(config, "0x90", nvidia, write, rawWrite).sent() &&
              nvidiaSends == 1 && sends == 1, "NVIDIA command did not use the raw writer");
    check(!DispatchNativeWindowsInput(config, "0x90", displaylink, write, rawWrite).sent() &&
              nvidiaSends == 1 && sends == 1, "DisplayLink command entered a GPU writer");
    config.method = "standard";
    check(!DispatchNativeWindowsInput(config, "not-an-input", mixed, write).sent() && sends == 1,
          "Invalid code reached the native writer");
    auto replacement = native;
    replacement.monitors[0].identity = QString(64, 'c');
    check(!DispatchNativeWindowsInput(config, "0x11", replacement, write).sent() && sends == 1,
          "A replacement at the same display number received a command");
    auto duplicated = native; duplicated.monitors.push_back(native.monitors[0]);
    check(!DispatchNativeWindowsInput(config, "0x11", duplicated, write).sent() && sends == 1,
          "Ambiguous native selection received a command");
    check(!DispatchNativeWindowsInput(config, "0x11", {}, write).sent() && sends == 1,
          "Disconnected native monitor received a command");
    result = DispatchNativeWindowsInput(config, "0x11", native,
        [](const DesktopMonitor &, unsigned int) {
            return lg::SwitchResult{lg::SwitchStatus::TransportFailure, 31, "Synthetic driver failure"};
        });
    check(!result.sent() && result.nativeCode == 31, "Native driver failure was reported as success");
    auto intel = native;
    intel.monitors[0].hardware["adapter_kind"] = "intel";
    check(MergeWindowsDiscovery(amd, intel).monitors.size() == 2,
          "Intel output disappeared behind an AMD adapter with a matching fingerprint");
    const int beforeIntel = sends;
    check(DispatchNativeWindowsInput(config, "0x11", intel, write, rawWrite).sent() &&
              sends == beforeIntel + 1 && nvidiaSends == 1,
          "Intel standard switching failed to use the Windows monitor writer");
    config.method = "lg";
    result = DispatchNativeWindowsInput(config, "0x90", intel, write, rawWrite);
    check(!result.sent() && result.detail.find("Intel") != std::string::npos &&
              sends == beforeIntel + 1 && nvidiaSends == 1,
          "Intel alternate switching silently sent a different command");
    std::cout << "Native Windows discovery, route isolation and write policy checks passed\n";
}
