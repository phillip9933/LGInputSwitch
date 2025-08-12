#pragma once
#include <string>
#include <windows.h>
#include <shlobj.h> // SHGetFolderPathA

inline std::wstring ToW(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len ? len - 1 : 0, L'\0');
    if (len) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    return w;
}

inline std::string AppDataDir() {
    char path[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
        return std::string(path) + "\\LGInputSwitch";
    }
    return ".";
}
