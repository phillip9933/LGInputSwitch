#include "app_toggle.h"
#include <windows.h>
#include <cstdlib>
#include "amdddc_core.h"

static unsigned int ParseHex(const std::string& s) {
    return strtoul(s.c_str(), nullptr, 0);
}

bool SendInputCode(const Target& t, const char* i2cAddrHex, const char* codeHex) {
    // For this AMD+LG path, the CLI used a fixed side-channel code (0xF4) and put the input
    // in the "value" field. We mirror that here and pass i2c subaddress (0x50) from config.
    const unsigned short DUMMY_VCP = 0xF4; // ignored in our core; kept for clarity
    unsigned int value = ParseHex(codeHex);     // e.g., 0xD0 / 0xD1 / 0x90 / 0x91
    unsigned int i2c = ParseHex(i2cAddrHex);  // e.g., 0x50

    int rc = SetVcpFeatureWithI2cAddr(
        t.adapterIndex,
        t.displayIndex,
        DUMMY_VCP,
        value,
        i2c
    );
    return rc == 0;
}

bool ToggleCycle(const Target& t, const char* i2cAddrHex,
    const std::vector<InputDef>& orderedInputs, int& idx) {
    if (orderedInputs.empty()) return false;
    idx = (idx + 1) % (int)orderedInputs.size();
    return SendInputCode(t, i2cAddrHex, orderedInputs[idx].code.c_str());
}
