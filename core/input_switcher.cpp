#include "input_switcher.h"
#include "input_value.h"
#include <map>
#include <set>

namespace lg {
bool ValidCommand(const InputCommand& command) {
    return command.sourceAddress <= 0xff && command.featureCode <= 0xff && command.value <= 0xffff;
}

SwitchResult SwitchInput(InputTransport& transport, const std::string& source,
    const std::string& value) {
    InputCommand command;
    if (!ParseInputValue(source, 0xff, command.sourceAddress) ||
        !ParseInputValue(value, 0xffff, command.value))
        return {SwitchStatus::InvalidArgument, 2, "Invalid I2C source address or input code. Check configuration."};
    return transport.Send(command);
}

SwitchResult CycleInputs(InputTransport& transport, const std::string& source,
    const std::vector<InputDef>& ordered, int& position) {
    if (ordered.empty()) return {SwitchStatus::InvalidArgument, 2, "No inputs are enabled."};
    const int next = position < -1 || position >= (int)ordered.size()
        ? 0 : (position + 1) % (int)ordered.size();
    auto result = SwitchInput(transport, source, ordered[next].code);
    if (result.sent()) position = next;
    return result;
}

std::vector<InputDef> OrderInputs(const std::vector<InputDef>& enabled,
    const std::vector<std::string>& order) {
    // Preserve Windows ordering: requested labels first, then other enabled
    // labels alphabetically. Repeated order entries do not duplicate inputs.
    std::map<std::string, const InputDef*> byLabel;
    for (const auto& input : enabled) byLabel[input.label] = &input;
    std::set<std::string> used;
    std::vector<InputDef> ordered;
    for (const auto& label : order) {
        auto found = byLabel.find(label);
        if (found != byLabel.end() && used.insert(label).second)
            ordered.push_back(*found->second);
    }
    for (const auto& item : byLabel)
        if (used.insert(item.first).second) ordered.push_back(*item.second);
    return ordered;
}

int InputPosition(const std::vector<InputDef>& ordered, const std::string& label) {
    for (size_t i = 0; i < ordered.size(); ++i)
        if (ordered[i].label == label) return (int)i;
    return -1;
}
} // namespace lg
