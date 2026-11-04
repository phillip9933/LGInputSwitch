#pragma once
#include <string>
#include <windows.h>
#include <shlobj.h> // SHGetFolderPathA

inline std::wstring ToW(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(len ? len : 0, L'\0');
    if (len) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), len);
    if (len) w.pop_back(); // conversion includes the terminator
    return w;
}

inline std::string AppDataDir() {
    char path[MAX_PATH] = {};
    if (SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, path) == S_OK) {
        return std::string(path) + "\\LGInputSwitch";
    }
    return ".";
}

// List-box labels may come from a user-edited configuration file.
inline std::wstring ListBoxText(HWND list, int index) {
    const auto length = SendMessageW(list, LB_GETTEXTLEN, index, 0);
    if (length == LB_ERR) return {};
    std::wstring text(static_cast<size_t>(length) + 1, L'\0');
    const auto copied = SendMessageW(list, LB_GETTEXT, index, reinterpret_cast<LPARAM>(text.data()));
    if (copied == LB_ERR) return {};
    text.resize(static_cast<size_t>(copied));
    return text;
}
