#include "../core/input_value.h"
#include "../platform/linux/ddcutil_backend.h"
#include "../external/json.hpp"
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

static void Usage() {
    std::cout << "LGInputSwitch Linux CLI (experimental)\n"
        "  lginputswitch detect [--dry-run]\n"
        "  lginputswitch switch (--display N | --bus N) --input VALUE [--source VALUE] [--dry-run]\n\n"
        "Display numbers start at 1; bus numbers start at 0. Choose exactly one target.\n"
        "Input/source values are decimal or 0x-prefixed hex. Source defaults to 0x50.\n"
        "This LG profile sends VCP 0xF4 without readback verification. Input values vary by model.\n"
        "--dry-run prints the argument vector as JSON and launches no process.\n";
}

static int Error(const std::string& message) {
    std::cerr << message << '\n';
    return 2;
}

static std::string Hex(unsigned int value) {
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

int main(int argc, char** argv) {
    if (argc == 2 && (std::string(argv[1]) == "--help" || std::string(argv[1]) == "-h")) {
        Usage();
        return 0;
    }
    if (argc < 2) { Usage(); return 2; }
    const std::string command = argv[1];
    if (command != "detect" && command != "switch") return Error("Expected detect or switch. Use --help.");

    bool dryRun = false, hasInput = false, hasSource = false;
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
        if (option != "--display" && option != "--bus" && option != "--input" && option != "--source")
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
        } else if (option == "--input") {
            if (hasInput || !ParseInputValue(text, 0xffff, input)) return Error("Invalid or duplicate --input.");
            hasInput = true;
        } else {
            if (hasSource || !ParseInputValue(text, 0xff, source)) return Error("Invalid or duplicate --source.");
            hasSource = true;
        }
    }

    std::vector<std::string> arguments{"ddcutil"};
    if (command == "detect") arguments.push_back("detect");
    else {
        if (selector.empty() || !hasInput) return Error("switch requires a target and --input. Use --help.");
        // Prefix all protocol values explicitly; ddcutil's source option is hexadecimal.
        arguments.insert(arguments.end(), {selector, std::to_string(target), "setvcp", "0xf4",
            Hex(input), "--i2c-source-addr=" + Hex(source), "--noverify"});
    }
    if (dryRun) {
        std::cout << nlohmann::json(arguments).dump() << '\n';
        return 0;
    }
    const auto result = RunDdcutil(arguments);
    if (!result.error.empty()) std::cerr << result.error << '\n';
    if (result.exitCode != 0) {
        std::cerr << "ddcutil failed (status " << result.exitCode << "). Check its output, target, cable path and I2C permissions.\n";
    } else if (command == "switch") {
        std::cout << "Input command sent; monitor switching was not verified.\n";
    }
    return result.exitCode;
}
