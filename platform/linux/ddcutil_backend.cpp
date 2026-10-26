#include "ddcutil_backend.h"
#include <cerrno>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>

extern char** environ;

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
