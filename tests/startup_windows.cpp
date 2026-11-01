#include "../platform/windows/startup.h"
#include <iostream>

int main() {
    // Run on an isolated CI runner; this never starts a logon session.
    wchar_t path[MAX_PATH]{};
    const auto length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return 1;
    const auto quoted = L"\"" + std::wstring(path, length) + L"\"";
    auto registeredAs = [&](const std::wstring& expected) {
        wchar_t actual[MAX_PATH]{};
        DWORD bytes = sizeof(actual), type = 0;
        return RegGetValueW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LGInputSwitch",
            RRF_RT_REG_SZ, &type, actual, &bytes) == ERROR_SUCCESS &&
            std::wstring(actual) == expected;
    };
    // Legacy callers keep their existing command; modern sign-in starts in the tray.
    bool matches = SetStartWithWindows(true) && registeredAs(quoted);
    matches = SetStartWithWindows(true, true) && registeredAs(quoted + L" --background") && matches;
    matches = SetStartWithWindows(true, true) && registeredAs(quoted + L" --background") && matches;
    matches = SetStartWithWindows(true) && registeredAs(quoted) && matches;
    if (!SetStartWithWindows(false, true)) return 2;
    wchar_t actual[260]{};
    DWORD bytes = sizeof(actual), type = 0;
    bool removed = RegGetValueW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", L"LGInputSwitch",
        RRF_RT_REG_SZ, &type, actual, &bytes) == ERROR_FILE_NOT_FOUND;
    if (!matches || !removed || !SetStartWithWindows(false)) return 3;
    std::cout << "Startup registration, quoted path, background argument updates, removal, and repeated removal passed\n";
}
