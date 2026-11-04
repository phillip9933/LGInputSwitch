#include "../platform/windows/adl_transport.h"
#include <cstdlib>
#include <iostream>

static int calls = 0, status = 0;
int SendAdlCommand(int adapter, int display, const lg::InputCommand &command) {
    if (adapter != 5 || display != 2 || command.value != 0x1234) std::abort();
    ++calls;
    return status;
}
static void Check(bool condition, const char *message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}
int main() {
    AdlTransport transport(5, 2);
    for (const auto &invalid : {lg::InputCommand{0x150, 0xf4, 0x1234}, {0x50, 0x60, 0x1234},
                                {0x50, 0xf4, 0x10000}, {0x50, 0xff, 0x1234}})
        Check(transport.Send(invalid).status == lg::SwitchStatus::InvalidArgument && calls == 0,
              "Invalid command reached the AMD driver");
    Check(AdlTransport(-1, 2).Send({0x50, 0xf4, 0x1234}).status == lg::SwitchStatus::InvalidArgument && calls == 0,
          "Invalid target reached the AMD driver");
    Check(transport.Send({0x50, 0xf4, 0x1234}).sent() && calls == 1, "LG command failed");
    Check(transport.Send({0x51, 0x60, 0x1234}).sent() && calls == 2, "Standard command failed");
    status = -3;
    const auto failed = transport.Send({0x50, 0xf4, 0x1234});
    Check(!failed.sent() && failed.nativeCode == -3 && failed.detail.find("adapter 5") != std::string::npos,
          "Driver failure details were lost");
}
