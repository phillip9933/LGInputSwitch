#pragma once
#include <windows.h>
#include <string>
#pragma comment(lib, "advapi32.lib")

// Per-user startup, no elevation. Quote the executable path for spaces/Unicode.
inline bool SetStartWithWindows(bool enabled) {
    constexpr auto keyPath = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    HKEY key = nullptr;
    LONG rc = RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr, 0,
        KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) return false;
    if (enabled) {
        wchar_t path[MAX_PATH]{};
        DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"" + std::wstring(path, length) + L"\"";
        // Run-key command lines are limited to 260 characters (including quotes).
        if (!length || length >= MAX_PATH || command.size() >= MAX_PATH) {
            RegCloseKey(key);
            return false;
        }
        rc = RegSetValueExW(key, L"LGInputSwitch", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
    } else {
        rc = RegDeleteValueW(key, L"LGInputSwitch");
        if (rc == ERROR_FILE_NOT_FOUND) rc = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}
