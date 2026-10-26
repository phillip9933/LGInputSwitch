#pragma once
#include <string>
#include <vector>
#include <utility>
#include "../../core/input_switcher.h"

struct ProcessResult {
    int exitCode;
    std::string error;
};

// Child inherits terminal I/O. No shell, elevation, permission changes or writes
// other than the explicitly requested ddcutil operation.
ProcessResult RunDdcutil(const std::vector<std::string>& arguments);

class DdcutilTransport final : public lg::InputTransport {
public:
    DdcutilTransport(std::string selector, unsigned int number)
        : selector_(std::move(selector)), number_(number) {}
    // An empty vector means invalid target/command. Preview never spawns a child.
    std::vector<std::string> Arguments(const lg::InputCommand& command) const;
    lg::SwitchResult Send(const lg::InputCommand& command) override;
private:
    std::string selector_;
    unsigned int number_;
};
