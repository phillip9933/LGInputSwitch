#include "../core/input_switcher.h"
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

class FakeTransport : public lg::InputTransport {
public:
    std::vector<lg::InputCommand> commands;
    bool fail = false;
    lg::SwitchResult Send(const lg::InputCommand& command) override {
        commands.push_back(command);
        if (fail) return {lg::SwitchStatus::TransportFailure, -7, "Synthetic backend rejection"};
        return {lg::SwitchStatus::CommandSent, 0, "Sent without readback"};
    }
};

int main() {
    FakeTransport transport;
    auto invalid = lg::SwitchInput(transport, "0x150", "0x90");
    Check(invalid.status == lg::SwitchStatus::InvalidArgument && transport.commands.empty(),
        "Invalid command reached backend");
    auto sent = lg::SwitchInput(transport, "0x50", "0xd0");
    Check(sent.sent() && transport.commands.size() == 1, "Valid command not sent");
    Check(transport.commands.back().sourceAddress == 0x50 &&
        transport.commands.back().featureCode == 0xf4 && transport.commands.back().value == 0xd0,
        "Shared command changed protocol values");
    transport.fail = true;
    auto failed = lg::SwitchInput(transport, "80", "144");
    Check(!failed.sent() && failed.nativeCode == -7 && failed.detail == "Synthetic backend rejection",
        "Backend status/detail lost");

    const std::vector<lg::InputDef> enabled{{"DisplayPort", "0xd0"},
        {"HDMI1", "0x92"}, {"USB-C", "0xd1"}};
    const auto ordered = lg::OrderInputs(enabled, {"USB-C", "Disabled", "HDMI1", "USB-C"});
    Check(ordered.size() == 3 && ordered[0].label == "USB-C" && ordered[1].label == "HDMI1" &&
        ordered[2].label == "DisplayPort" && ordered[1].code == "0x92",
        "Ordering lost enablement, custom code or duplicate handling");
    Check(lg::InputPosition(ordered, "HDMI1") == 1 && lg::InputPosition(ordered, "Disabled") == -1,
        "Direct input not mapped into cycle order");
    const auto fallback = lg::OrderInputs(enabled, {});
    Check(fallback.size() == 3 && fallback[0].label == "DisplayPort" &&
        fallback[1].label == "HDMI1" && fallback[2].label == "USB-C", "Legacy fallback ordering changed");
    Check(lg::OrderInputs({}, {"HDMI1"}).empty(), "Disabled input enabled by order");

    int position = lg::InputPosition(ordered, "HDMI1");
    auto cycle = lg::CycleInputs(transport, "0x50", ordered, position);
    Check(!cycle.sent() && position == 1 && transport.commands.back().value == 0xd0,
        "Failed cycle changed position");
    transport.fail = false;
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 2 &&
        transport.commands.back().value == 0xd0, "Retry skipped failed input");
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 0,
        "Direct-then-cycle did not wrap in configured order");
    position = 99;
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 0,
        "Stale position not repaired");
    const auto before = transport.commands.size();
    Check(!lg::CycleInputs(transport, "0x50", {}, position).sent() &&
        transport.commands.size() == before && position == 0, "Empty cycle touched backend/state");
    Check(!lg::ValidCommand({0x100, 0xf4, 0x90}) && !lg::ValidCommand({0x50, 0x100, 0x90}) &&
        !lg::ValidCommand({0x50, 0xf4, 0x10000}), "Transport range guard accepted truncated values");
    std::cout << "Portable core regression checks passed\n";
}
