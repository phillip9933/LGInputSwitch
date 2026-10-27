#include "../core/ddc_packet.h"
#include "../core/input_history.h"
#include "../core/input_switcher.h"
#include <cstdlib>
#include <iostream>

static void Check(bool condition, const char *message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

class FakeTransport : public lg::InputTransport
{
  public:
    std::vector<lg::InputCommand> commands;
    bool fail = false;
    lg::SwitchResult Send(const lg::InputCommand &command) override
    {
        commands.push_back(command);
        if (fail)
            return {lg::SwitchStatus::TransportFailure, -7, "Synthetic backend rejection"};
        return {lg::SwitchStatus::CommandSent, 0, "Sent without readback"};
    }
};

int main()
{
    const auto packet = lg::EncodeDdcWrite({0x50, 0xf4, 0xd0});
    Check(packet && *packet == std::array<unsigned char, 8>{0x6e, 0x50, 0x84, 3, 0xf4, 0, 0xd0, 0x9d},
          "DisplayPort wire packet incorrect");
    const auto high = lg::EncodeDdcWrite({0x50, 0xf4, 0x1234});
    Check(high && (*high)[5] == 0x12 && (*high)[6] == 0x34, "Wire value byte order incorrect");
    for (const auto &bad : {lg::InputCommand{0x150, 0xf4, 0xd0}, lg::InputCommand{0x50, 0x60, 0xd0},
                            lg::InputCommand{0x50, 0xf4, 0x10000}})
        Check(!lg::EncodeDdcWrite(bad), "Invalid wire packet accepted");
    FakeTransport transport;
    auto invalid = lg::SwitchInput(transport, "0x150", "0x90");
    Check(invalid.status == lg::SwitchStatus::InvalidArgument && transport.commands.empty(),
          "Invalid command reached backend");
    auto sent = lg::SwitchInput(transport, "0x50", "0xd0");
    Check(sent.sent() && transport.commands.size() == 1, "Valid command not sent");
    Check(transport.commands.back().sourceAddress == 0x50 && transport.commands.back().featureCode == 0xf4 &&
              transport.commands.back().value == 0xd0,
          "Shared command changed protocol values");
    transport.fail = true;
    auto failed = lg::SwitchInput(transport, "80", "144");
    Check(!failed.sent() && failed.nativeCode == -7 && failed.detail == "Synthetic backend rejection",
          "Backend status/detail lost");

    const std::vector<lg::InputDef> enabled{{"DisplayPort", "0xd0"}, {"HDMI1", "0x92"}, {"USB-C", "0xd1"}};
    const auto ordered = lg::OrderInputs(enabled, {"USB-C", "Disabled", "HDMI1", "USB-C"});
    Check(ordered.size() == 3 && ordered[0].label == "USB-C" && ordered[1].label == "HDMI1" &&
              ordered[2].label == "DisplayPort" && ordered[1].code == "0x92",
          "Ordering lost enablement, custom code or duplicate handling");
    Check(lg::InputPosition(ordered, "HDMI1") == 1 && lg::InputPosition(ordered, "Disabled") == -1,
          "Direct input not mapped into cycle order");
    const auto fallback = lg::OrderInputs(enabled, {});
    Check(fallback.size() == 3 && fallback[0].label == "DisplayPort" && fallback[1].label == "HDMI1" &&
              fallback[2].label == "USB-C",
          "Legacy fallback ordering changed");
    Check(lg::OrderInputs({}, {"HDMI1"}).empty(), "Disabled input enabled by order");

    int position = lg::InputPosition(ordered, "HDMI1");
    auto cycle = lg::CycleInputs(transport, "0x50", ordered, position);
    Check(!cycle.sent() && position == 1 && transport.commands.back().value == 0xd0,
          "Failed cycle changed position");
    transport.fail = false;
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 2 &&
              transport.commands.back().value == 0xd0,
          "Retry skipped failed input");
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 0,
          "Direct-then-cycle did not wrap in configured order");
    position = 99;
    Check(lg::CycleInputs(transport, "0x50", ordered, position).sent() && position == 0,
          "Stale position not repaired");
    const auto before = transport.commands.size();
    Check(!lg::CycleInputs(transport, "0x50", {}, position).sent() && transport.commands.size() == before &&
              position == 0,
          "Empty cycle touched backend/state");
    Check(!lg::ValidCommand({0x100, 0xf4, 0x90}) && !lg::ValidCommand({0x50, 0x100, 0x90}) &&
              !lg::ValidCommand({0x50, 0xf4, 0x10000}),
          "Transport range guard accepted truncated values");
    lg::InputHistory history;
    Check(history.previous().empty(), "Previous input invented at startup");
    history.sent("DisplayPort");
    Check(history.previous().empty(), "First accepted command invented history");
    history.sent("USB-C");
    history.sent("USB-C");
    Check(history.previous() == "DisplayPort", "Repeated selection destroyed previous input");
    const auto previous = history.previous();
    history.sent(previous);
    Check(history.previous() == "USB-C", "Previous-input toggle did not retain the other input");
    history.reset();
    Check(history.previous().empty(), "Target change retained stale history");
    std::cout << "Portable core regression checks passed\n";
}
