#include "../platform/windows/startup.h"
#include <iostream>

int main() {
    // Run on an isolated CI runner; this never starts a logon session.
    if (!SetStartWithWindows(true)) return 1;
    wchar_t actual[260]{};
    DWORD bytes = sizeof(actual), type = 0;
    const auto rc = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LGInputSwitch",
        RRF_RT_REG_SZ, &type, actual, &bytes);
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    bool matches = rc == ERROR_SUCCESS && std::wstring(actual) == L"\"" + std::wstring(path) + L"\"";
    if (!SetStartWithWindows(false)) return 2;
    bytes = sizeof(actual);
    bool removed = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LGInputSwitch",
        RRF_RT_REG_SZ, &type, actual, &bytes) == ERROR_FILE_NOT_FOUND;
    if (!matches || !removed || !SetStartWithWindows(false)) return 3;
    std::cout << "Startup registration, quoted path, removal, and repeated removal passed\n";
}
