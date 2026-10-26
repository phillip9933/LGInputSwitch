#include "app_toggle.h"
#include "../core/input_value.h"
#include "amdddc_core.h"

static std::string g_lastError;
const std::string& LastInputError() { return g_lastError; }

bool SendInputCode(const Target& t, const char* i2cAddrHex, const char* codeHex) {
    // For this AMD+LG path, the CLI used a fixed side-channel code (0xF4) and put the input
    // in the "value" field. We mirror that here and pass i2c subaddress (0x50) from config.
    const unsigned short DUMMY_VCP = 0xF4; // ignored in our core; kept for clarity
    unsigned int value = 0, i2c = 0;
    if (!codeHex || !i2cAddrHex || t.adapterIndex < 0 || t.displayIndex < 0 ||
        !ParseInputValue(codeHex, 0xffff, value) ||
        !ParseInputValue(i2cAddrHex, 0xff, i2c)) {
        g_lastError = "Invalid target, I2C source address, or input code. Check config.json.";
        return false;
    }

    int rc = SetVcpFeatureWithI2cAddr(
        t.adapterIndex,
        t.displayIndex,
        DUMMY_VCP,
        value,
        i2c
    );
    g_lastError.clear();
    if (rc != 0) {
        g_lastError = "Switch failed: ADL status " + std::to_string(rc) +
            ", adapter " + std::to_string(t.adapterIndex) + ", display " +
            std::to_string(t.displayIndex) + ", source " + i2cAddrHex + ", input " + codeHex;
    }
    return rc == 0;
}

bool ToggleCycle(const Target& t, const char* i2cAddrHex,
    const std::vector<InputDef>& orderedInputs, int& idx) {
    if (orderedInputs.empty()) return false;
    const int next = (idx < -1 || idx >= (int)orderedInputs.size())
        ? 0 : (idx + 1) % (int)orderedInputs.size();
    if (!SendInputCode(t, i2cAddrHex, orderedInputs[next].code.c_str())) return false;
    idx = next;
    return true;
}
