#include "../core/input_value.h"
#include "../platform/linux/ddcutil_backend.h"
#include "../external/json.hpp"
#include <iostream>
#include <limits>
#include <string>
#include <vector>

static void Usage() {
    std::cout << "LGInputSwitch Linux CLI (experimental)\n"
        "  lginputswitch detect [--dry-run]\n"
        "  lginputswitch switch (--display N | --bus N) --input VALUE [--method lg|standard] [--source VALUE] [--dry-run]\n\n"
        "Display numbers start at 1; bus numbers start at 0. Choose exactly one target.\n"
        "Input/source values are decimal or 0x-prefixed hex. LG source defaults to 0x50; standard uses 0x51.\n"
        "LG sends VCP 0xF4; standard sends VCP 0x60, both without readback verification. Input values vary by model.\n"
        "--dry-run prints the argument vector as JSON and launches no process.\n";
}

static int Error(const std::string& message) {
    std::cerr << message << '\n';
    return 2;
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        Usage();
        return 0;
    }
    if (argc < 2) { Usage(); return 2; }
    const std::string command = argv[1];
    if (command != "detect" && command != "switch") return Error("Expected detect or switch. Use --help.");

    bool dryRun = false, hasInput = false, hasSource = false, hasMethod = false;
    std::string method = "lg";
    std::string selector;
    unsigned int target = 0, input = 0, source = 0x50;
    for (int i = 2; i < argc; ++i) {
        const std::string option = argv[i];
        if (option == "--dry-run") {
            if (dryRun) return Error("Duplicate --dry-run.");
            dryRun = true;
            continue;
        }
        if (command == "detect") return Error("detect accepts only --dry-run.");
        if (option != "--display" && option != "--bus" && option != "--input" && option != "--source" && option != "--method")
            return Error("Unknown option: " + option);
        if (i + 1 >= argc) return Error("Missing value for " + option);
        const std::string text = argv[++i];
        if (option == "--display" || option == "--bus") {
            if (!selector.empty()) return Error("Choose exactly one --display or --bus target.");
            // ddcutil takes decimal selector numbers. Avoid implicit hex/octal routes.
            if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos ||
                !ParseInputValue(text, std::numeric_limits<int>::max(), target) ||
                (option == "--display" && target == 0)) return Error("Invalid target number.");
            selector = option;
        } else if (option == "--method") {
            if (hasMethod || (text != "lg" && text != "standard")) return Error("Invalid or duplicate --method.");
            method = text;
            hasMethod = true;
        } else if (option == "--input") {
            if (hasInput || !ParseInputValue(text, 0xffff, input)) return Error("Invalid or duplicate --input.");
            hasInput = true;
        } else {
            if (hasSource || !ParseInputValue(text, 0xff, source)) return Error("Invalid or duplicate --source.");
            hasSource = true;
        }
    }

    std::vector<std::string> arguments{"ddcutil"};
    DdcutilTransport transport(selector, target);
    if (method == "standard" && hasSource && source != 0x51) return Error("Standard switching uses source 0x51; omit --source.");
    const lg::InputCommand inputCommand{method == "standard" ? 0x51u : source, method == "standard" ? 0x60u : 0xf4u, input};
    if (command == "detect") arguments.push_back("detect");
    else {
        if (selector.empty() || !hasInput) return Error("switch requires a target and --input. Use --help.");
        arguments = transport.Arguments(inputCommand);
        if (arguments.empty()) return Error("Invalid Linux target or LG input command.");
    }
    if (dryRun) {
        std::cout << nlohmann::json(arguments).dump() << '\n';
        return 0;
    }
    lg::SwitchResult result{lg::SwitchStatus::CommandSent, 0, {}};
    if (command == "detect") {
        const auto child = RunDdcutil(arguments);
        result = {child.exitCode == 0 ? lg::SwitchStatus::CommandSent : lg::SwitchStatus::TransportFailure,
            child.exitCode, child.error};
    } else result = transport.Send(inputCommand);
    if (!result.sent()) {
        if (!result.detail.empty()) std::cerr << result.detail << '\n';
        std::cerr << "ddcutil failed (status " << result.nativeCode << "). Check its output, target, cable path and I2C permissions.\n";
    } else if (command == "switch") {
        std::cout << result.detail << '\n';
    }
    return result.nativeCode;
}
