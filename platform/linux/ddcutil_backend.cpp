#include "ddcutil_backend.h"
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <limits>
#include <sstream>

extern char** environ;

static std::string Hex(unsigned int value) {
    std::ostringstream text;
    text << "0x" << std::hex << value;
    return text.str();
}

std::vector<std::string> DdcutilTransport::Arguments(const lg::InputCommand& command) const {
    if (!lg::ValidCommand(command) || (command.featureCode != 0xf4 && !(command.featureCode == 0x60 && command.sourceAddress == 0x51)) ||
        (selector_ != "--display" && selector_ != "--bus") ||
        number_ > static_cast<unsigned int>(std::numeric_limits<int>::max()) ||
        (selector_ == "--display" && number_ == 0)) return {};
    std::vector<std::string> arguments{"ddcutil", selector_, std::to_string(number_), "setvcp", Hex(command.featureCode), Hex(command.value)};
    if (command.featureCode == 0xf4) arguments.push_back("--i2c-source-addr=" + Hex(command.sourceAddress));
    arguments.push_back("--noverify");
    return arguments;
}

lg::SwitchResult DdcutilTransport::Send(const lg::InputCommand& command) {
    auto arguments = Arguments(command);
    if (arguments.empty()) return {lg::SwitchStatus::InvalidArgument, 2, "Invalid Linux target or LG input command."};
    const auto result = RunDdcutil(arguments);
    if (result.exitCode == 0)
        return {lg::SwitchStatus::CommandSent, 0, "Input command sent; monitor switching was not verified."};
    return {lg::SwitchStatus::TransportFailure, result.exitCode, result.error};
}

ProcessResult RunDdcutil(const std::vector<std::string>& arguments) {
    std::vector<char*> argv;
    for (const auto& argument : arguments)
        argv.push_back(const_cast<char*>(argument.c_str()));
    argv.push_back(nullptr);
    pid_t child = 0;
    int rc = posix_spawnp(&child, "ddcutil", nullptr, nullptr, argv.data(), environ);
    if (rc != 0) {
        return {rc == ENOENT ? 127 : 126,
            rc == ENOENT ? "ddcutil was not found. Install your distribution's ddcutil package."
                         : std::string("Could not start ddcutil: ") + std::strerror(rc)};
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno == EINTR) continue;
        return {126, std::string("Could not wait for ddcutil: ") + std::strerror(errno)};
    }
    if (WIFEXITED(status)) return {WEXITSTATUS(status), {}};
    if (WIFSIGNALED(status)) return {128 + WTERMSIG(status), "ddcutil was terminated by a signal."};
    return {126, "ddcutil ended without an exit status."};
}
