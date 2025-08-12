#pragma once
#include <string>
#include <vector>
#include "types.h"

bool SendInputCode(const Target& t, const char* i2cAddrHex, const char* codeHex);
bool ToggleCycle(const Target& t, const char* i2cAddrHex,
    const std::vector<InputDef>& orderedInputs,
    int& inOutIndex);
