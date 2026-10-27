#include "../core/ddc_packet.h"
#include "../platform/windows/adl_command.h"
#include "../platform/windows/adl_session.h"
#include <algorithm>
#include <array>
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
int wmain(int argc, wchar_t **argv)
{
    Check(argc == 3, "Pass the two isolated fake driver DLLs");
    const auto library = LoadLibraryW(argv[1]);
    Check(library != nullptr, "Could not load fake driver");
    auto status = reinterpret_cast<void (*)(int, int)>(GetProcAddress(library, "FakeSetStatus"));
    auto count = reinterpret_cast<int (*)(int)>(GetProcAddress(library, "FakeCounter"));
    auto packet = reinterpret_cast<const unsigned char *(*)()>(GetProcAddress(library, "FakePacket"));
    auto target = reinterpret_cast<int (*)(int)>(GetProcAddress(library, "FakeTarget"));
    Check(status && count && packet && target, "Fake control exports missing");
    SetAdlTestLibrary(argv[1]);
    status(-5, 0);
    Check(!InitializeAdl() && !Adl().writeBlock, "Failed initialization was retained");
    status(0, 0);
    Check(InitializeAdl() && InitializeAdl() && count(0) == 2, "Initialization retry/idempotence failed");
    int adapters = 0;
    Check(Adl().adapterCount(&adapters) == 0 && adapters == 1, "Adapter enumeration failed");
    AdapterInfo info{};
    info.iSize = sizeof(info);
    Check(Adl().adapters(&info, sizeof(info)) == 0 && info.iAdapterIndex == 7, "Adapter structure incorrect");
    ADLDisplayInfo *display = nullptr;
    int displays = 0;
    Check(Adl().displays(7, &displays, &display, 0) == 0 && displays == 1 &&
              display->displayID.iDisplayLogicalIndex == 3,
          "Display enumeration failed");
    ReleaseAdlMemory(reinterpret_cast<void **>(&display));
    Check(!display, "Driver allocation was not released");
    lg::InputCommand input{0x50, 0xf4, 0xd0};
    Check(SendAdlCommand(7, 3, input) == 0 && count(2) == 1, "Fake driver write failed");
    const std::array<unsigned char, 8> expected{0x6e, 0x50, 0x84, 0x03, 0xf4, 0, 0xd0, 0x9d};
    Check(std::equal(expected.begin(), expected.end(), packet()) && target(0) == 7 && target(1) == 3,
          "Wrong wire packet or selected target");
    status(0, -3);
    Check(SendAdlCommand(7, 3, input) == -3, "Driver error was lost");
    input.sourceAddress = 0x150;
    Check(SendAdlCommand(7, 3, input) == ADL_ERR_INVALID_PARAM && count(2) == 2,
          "Invalid command reached driver");
    ShutdownAdl();
    ShutdownAdl();
    Check(count(1) == 1 && !Adl().create, "Shutdown did not clear session exactly once");
    SetAdlTestLibrary(argv[2]);
    Check(!InitializeAdl() && !Adl().create, "Missing required export accepted");
    SetAdlTestLibrary(argv[1]);
    status(0, 0);
    Check(InitializeAdl(), "Could not recover from incomplete driver");
    ShutdownAdl();
    FreeLibrary(library);
    std::cout << "ADL fake-DLL discovery, initialization, packet, targeting and failure checks passed\n";
}
