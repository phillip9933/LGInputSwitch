#pragma once
#include <string>
#include <vector>

namespace lg {
struct InputDef {
    std::string label;
    std::string code;
};
struct InputCommand {
    unsigned int sourceAddress = 0x50;
    unsigned int featureCode = 0xf4;
    unsigned int value = 0;
};
enum class SwitchStatus { CommandSent, InvalidArgument, TransportFailure };
struct SwitchResult {
    SwitchStatus status;
    int nativeCode;
    std::string detail;
    bool sent() const { return status == SwitchStatus::CommandSent; }
};

// Each transport is bound to one native target. Core code never interprets OS
// display indices or chooses a GPU on behalf of the frontend.
class InputTransport {
public:
    virtual ~InputTransport() = default;
    virtual SwitchResult Send(const InputCommand& command) = 0;
};
bool ValidCommand(const InputCommand& command);
lg::SwitchResult SwitchInput(InputTransport& transport, const std::string& source,
    const std::string& value);
lg::SwitchResult CycleInputs(InputTransport& transport, const std::string& source,
    const std::vector<InputDef>& ordered, int& position);
std::vector<InputDef> OrderInputs(const std::vector<InputDef>& enabled,
    const std::vector<std::string>& order);
int InputPosition(const std::vector<InputDef>& ordered, const std::string& label);
} // namespace lg
