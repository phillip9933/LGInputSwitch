#include "app_toggle.h"
#include "../platform/windows/adl_transport.h"

static std::string g_lastError;
const std::string& LastInputError() { return g_lastError; }

bool SendInputCode(const Target& target, const char* source, const char* code) {
    if (!source || !code) {
        g_lastError = "Missing I2C source address or input code.";
        return false;
    }
    AdlTransport transport(target.adapterIndex, target.displayIndex);
    const auto result = lg::SwitchInput(transport, source, code);
    g_lastError = result.sent() ? std::string{} : result.detail;
    return result.sent();
}

bool ToggleCycle(const Target& target, const char* source,
    const std::vector<InputDef>& ordered, int& index) {
    if (!source) {
        g_lastError = "Missing I2C source address.";
        return false;
    }
    AdlTransport transport(target.adapterIndex, target.displayIndex);
    const auto result = lg::CycleInputs(transport, source, ordered, index);
    g_lastError = result.sent() ? std::string{} : result.detail;
    return result.sent();
}
