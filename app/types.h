#pragma once
#include <string>

struct Target {
    int adapterIndex;
    int displayIndex;
};

struct InputDef {
    std::string label; // e.g., "DisplayPort"
    std::string code;  // e.g., "0xD0"
};
