#pragma once
#include <string>
#include <vector>

struct ProcessResult {
    int exitCode;
    std::string error;
};

// Child inherits terminal I/O. No shell, elevation, permission changes or writes
// other than the explicitly requested ddcutil operation.
ProcessResult RunDdcutil(const std::vector<std::string>& arguments);
