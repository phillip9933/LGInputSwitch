#include "app_tray.h"
#include "app_config.h"
#include "app_toggle.h"
#include "hotkeys.h"
#include "util.h"
#include "settings_ui.h"
#include "welcome_ui.h"
#include "../platform/windows/startup.h"
#include "../resource/resource.h"

#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <map>
#include <chrono>
#include <set>

static const wchar_t* kWndClass = L"LGInputSwitchHiddenWnd";
static UINT HKID_CYCLE = 1;
static std::map<UINT, std::string> g_directById;
static int g_cycleIndex = -1;
static std::chrono::steady_clock::time_point g_lastPress;

static NOTIFYICONDATA nid{};
static AppConfig g_cfg;
static Target g_target;

// Dynamic input menu id range
static const UINT ID_INPUT_BASE = 41000;
static std::map<UINT, size_t> g_menuInputIdToIndex; // menu id -> index into g_cfg.inputs

// Message posted by settings dialog when user saves
static const UINT WM_SETTINGS_SAVED = WM_APP + 2;

static void UnregisterAllHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, HKID_CYCLE);
    for (UINT id = 100; id < 200; ++id) UnregisterHotKey(hwnd, id);
}

static void Balloon(const wchar_t* msg) {
    if (!g_cfg.showNotifications) return;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"LGInputSwitch");
    wcsncpy_s(nid.szInfo, msg, _TRUNCATE);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

static HMENU Menu() {
    HMENU h = CreatePopupMenu();

    // Cycle
    AppendMenu(h, MF_STRING, ID_TRAY_CYCLE, L"Cycle");

    // Dynamic inputs from config
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    g_menuInputIdToIndex.clear();
    for (size_t i = 0; i < g_cfg.inputs.size(); ++i) {
        const auto& in = g_cfg.inputs[i];
        std::wstring label = ToW(in.label);
        UINT id = ID_INPUT_BASE + (UINT)i;
        g_menuInputIdToIndex[id] = i;
        AppendMenu(h, MF_STRING, id, label.c_str());
    }

    // Settings / Exit
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    AppendMenu(h, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    AppendMenu(h, MF_STRING, ID_TRAY_EXIT, L"Exit");
    return h;
}

static void RegisterHK(HWND hwnd) {
    bool failed = false;
    HotkeySpec hs{};
    if (ParseHotkey(g_cfg.hotkeys.cycle, hs))
        failed = !RegisterHotKey(hwnd, HKID_CYCLE, hs.fsModifiers | MOD_NOREPEAT, hs.vk);
    else if (!g_cfg.hotkeys.cycle.empty()) failed = true;

    UINT base = 100;
    g_directById.clear();
    for (auto& kv : g_cfg.hotkeys.direct) {
        HotkeySpec h2{};
        if (ParseHotkey(kv.second, h2)) {
            UINT id = base++;
            if (RegisterHotKey(hwnd, id, h2.fsModifiers | MOD_NOREPEAT, h2.vk))
                g_directById[id] = kv.first; // label
            else failed = true;
        } else if (!kv.second.empty()) failed = true;
    }
    if (failed) MessageBox(hwnd, L"One or more hotkeys are invalid or already in use. Open Settings to choose different combinations.",
        L"LG Input Switch", MB_OK | MB_ICONWARNING);
}

static std::vector<InputDef> OrderedInputs() {
    return lg::OrderInputs(g_cfg.inputs, g_cfg.cycleOrder);
}

static void RememberInput(const std::string& label) {
    g_cycleIndex = lg::InputPosition(OrderedInputs(), label);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Tray icon (custom)
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_APP + 1;
        nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON),
                                     IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        wcscpy_s(nid.szTip, L"LGInputSwitch");
        Shell_NotifyIcon(NIM_ADD, &nid);

        // First-run flow: LoadConfig returns false if file missing/bad
        bool loaded = LoadConfig(g_cfg);
        if (!loaded) {
            // First-run: show Welcome (modal) and then settings (modal) so user configures before hotkeys
            if (!ShowWelcomeDialog(hwnd) || !ShowSettingsDialogModal(hwnd, g_cfg)) {
                DestroyWindow(hwnd);
                return 0;
            }
        }

        if (g_cfg.targets.empty()) g_cfg.targets.push_back({ 5,0 });
        g_target = { g_cfg.targets[0].first, g_cfg.targets[0].second };
        if (!SetStartWithWindows(g_cfg.startWithWindows))
            Balloon(L"Could not update Start with Windows. Check Settings and try again.");
        RegisterHK(hwnd);
        return 0;
    }
    case WM_HOTKEY: {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastPress).count() < g_cfg.debounceMs)
            return 0;
        g_lastPress = now;

        if (wParam == HKID_CYCLE) {
            auto ord = OrderedInputs();
            if (ToggleCycle(g_target, g_cfg.i2cSourceAddr.c_str(), ord, g_cycleIndex)) {
                std::wstring msg = (g_cycleIndex >= 0 && g_cycleIndex < (int)ord.size())
                    ? ToW(ord[g_cycleIndex].label) : L"Switched";
                Balloon(msg.c_str());
            } else {
                Balloon(ToW(LastInputError()).c_str());
            }
            return 0;
        }
        // Direct hotkeys (mapped by label)
        auto it = g_directById.find((UINT)wParam);
        if (it != g_directById.end()) {
            for (size_t i = 0; i < g_cfg.inputs.size(); ++i) {
                if (g_cfg.inputs[i].label == it->second) {
                    if (SendInputCode(g_target, g_cfg.i2cSourceAddr.c_str(), g_cfg.inputs[i].code.c_str())) {
                        RememberInput(it->second);
                        Balloon(ToW(it->second).c_str());
                    } else {
                        Balloon(ToW(LastInputError()).c_str());
                    }
                    break;
                }
            }
            return 0;
        }
        return 0;
    }
    case WM_APP + 1: {
        if (lParam == WM_RBUTTONUP) {
            HMENU m = Menu();
            POINT pt; GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(m, TPM_BOTTOMALIGN | TPM_LEFTALIGN, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(m);
        }
        return 0;
    }
    case WM_COMMAND: {
        const UINT cmd = LOWORD(wParam);

        if (cmd == ID_TRAY_SETTINGS) {
            // Open modeless settings dialog. Dialog itself saves and will post WM_SETTINGS_SAVED when done.
            ShowSettingsDialog(hwnd, g_cfg);
            return 0;
        }

        if (cmd == ID_TRAY_CYCLE) {
            PostMessage(hwnd, WM_HOTKEY, HKID_CYCLE, 0);
            return 0;
        }

        if (cmd == ID_TRAY_EXIT) {
            DestroyWindow(hwnd);
            return 0;
        }

        // Dynamic inputs
        auto mit = g_menuInputIdToIndex.find(cmd);
        if (mit != g_menuInputIdToIndex.end()) {
            size_t i = mit->second;
            if (i < g_cfg.inputs.size()) {
                const auto& in = g_cfg.inputs[i];
                if (SendInputCode(g_target, g_cfg.i2cSourceAddr.c_str(), in.code.c_str())) {
                    RememberInput(in.label);
                    Balloon(ToW(in.label).c_str());
                } else {
                    Balloon(ToW(LastInputError()).c_str());
                }
            }
            return 0;
        }

        return 0;
    }
    case WM_DESTROY:
        Shell_NotifyIcon(NIM_DELETE, &nid);
        if (nid.hIcon) DestroyIcon(nid.hIcon);
        PostQuitMessage(0);
        return 0;

    case WM_SETTINGS_SAVED: {
        // Reload config from disk (settings dialog already saved)
        AppConfig tmp;
        if (LoadConfig(tmp)) {
            g_cfg = tmp;
            g_cycleIndex = -1;
            if (!SetStartWithWindows(g_cfg.startWithWindows))
                MessageBox(hwnd, L"Settings saved, but Start with Windows could not be updated.",
                    L"LG Input Switch", MB_OK | MB_ICONWARNING);
            if (!g_cfg.targets.empty())
                g_target = { g_cfg.targets[0].first, g_cfg.targets[0].second };
            UnregisterAllHotkeys(hwnd);
            RegisterHK(hwnd);
            Balloon(L"Settings saved");
        } else {
            Balloon(L"Settings saved (reload failed)");
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int RunTrayApp() {
    // Use WNDCLASSEX so we can set a small icon too
    WNDCLASSEX wc{};
    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON),
                                  IMAGE_ICON, 16, 16, 0);
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, kWndClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, GetModuleHandle(nullptr), nullptr);
    ShowWindow(GetConsoleWindow(), SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}
