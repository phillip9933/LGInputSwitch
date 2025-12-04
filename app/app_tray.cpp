// app_tray.cpp (instrumented + defensive)
// Drop-in replacement: writes diagnostics to %TEMP%\lginputswitch.log
#include "app_tray.h"
#include "app_config.h"
#include "app_toggle.h"
#include "hotkeys.h"
#include "util.h"
#include "settings_ui.h"
#include "welcome_ui.h"
#include "../resource/resource.h"

#include <windows.h>
#include <shellapi.h>
#include <vector>
#include <map>
#include <chrono>
#include <set>
#include <fstream>
#include <ctime>

#include "../amdddc/adl.h"
extern bool InitADL();
extern "C" {
    extern ADLPROCS      adlprocs;
    extern LPAdapterInfo lpAdapterInfo;
}

static const wchar_t* kWndClass = L"LGInputSwitchHiddenWnd";
static UINT HKID_CYCLE = 1;
static std::map<UINT, std::string> g_directById;
static int g_cycleIndex = -1;
static std::chrono::steady_clock::time_point g_lastPress;

static NOTIFYICONDATA nid{};
static AppConfig g_cfg;
static Target g_target;

static const UINT ID_INPUT_BASE = 41000;
static std::map<UINT, size_t> g_menuInputIdToIndex;
static const UINT WM_SETTINGS_SAVED = WM_APP + 2;

// diagnostic logging
static std::string g_logPath;
static void LogInit() {
    char tmpPath[MAX_PATH];
    GetTempPathA(MAX_PATH, tmpPath);
    g_logPath = std::string(tmpPath) + "lginputswitch.log";
    // prepend header
    std::ofstream f(g_logPath, std::ios::app);
    if (f) {
        std::time_t t = std::time(nullptr);
        f << "---- run: " << std::asctime(std::localtime(&t)) << "----\n";
    }
}
static void Log(const char* fmt, ...) {
    std::ofstream f(g_logPath, std::ios::app);
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    char buf[1024];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    f << buf << "\n";
}

// ADL available flag
static bool g_adlAvailable = false;

// Ensure ADL & lpAdapterInfo are populated. Returns true if ADL usable.
static bool InitADLAndState() {
    Log("InitADLAndState() start: lpAdapterInfo=%p", (void*)lpAdapterInfo);

    // Basic sanity: ADL function pointers present?
    if (!InitADL()) {
        Log("InitADL() returned false");
        g_adlAvailable = false;
        return false;
    }
    if (!adlprocs.ADL_Adapter_NumberOfAdapters_Get) {
        Log("adlprocs.ADL_Adapter_NumberOfAdapters_Get is NULL");
        g_adlAvailable = false;
        return false;
    }

    int nAdapters = 0;
    int rc = adlprocs.ADL_Adapter_NumberOfAdapters_Get(&nAdapters);
    Log("ADL_Adapter_NumberOfAdapters_Get rc=%d nAdapters=%d", rc, nAdapters);
    if (rc != 0 || nAdapters <= 0) {
        Log("No adapters or error; rc=%d nAdapters=%d", rc, nAdapters);
        g_adlAvailable = false;
        return false;
    }

    // If already allocated, assume it's fine (but sanity-check size not possible here)
    if (!lpAdapterInfo) {
        lpAdapterInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * nAdapters);
        if (!lpAdapterInfo) {
            Log("malloc(lpAdapterInfo) failed");
            g_adlAvailable = false;
            return false;
        }
        memset(lpAdapterInfo, 0, sizeof(AdapterInfo) * nAdapters);
        rc = adlprocs.ADL_Adapter_AdapterInfo_Get(lpAdapterInfo, sizeof(AdapterInfo) * nAdapters);
        Log("ADL_Adapter_AdapterInfo_Get rc=%d", rc);
        if (rc != 0) {
            Log("ADL_Adapter_AdapterInfo_Get failed rc=%d; freeing lpAdapterInfo", rc);
            free(lpAdapterInfo);
            lpAdapterInfo = nullptr;
            g_adlAvailable = false;
            return false;
        }
    } else {
        Log("lpAdapterInfo already present at %p", (void*)lpAdapterInfo);
    }

    g_adlAvailable = true;
    Log("InitADLAndState() success: g_adlAvailable=true");
    return true;
}

static void UnregisterAllHotkeys(HWND hwnd) {
    UnregisterHotKey(hwnd, HKID_CYCLE);
    for (UINT id = 100; id < 200; ++id) UnregisterHotKey(hwnd, id);
}

static void Balloon(const wchar_t* msg) {
    if (!g_cfg.showNotifications) return;
    nid.uFlags = NIF_INFO;
    wcscpy_s(nid.szInfoTitle, L"LGInputSwitch");
    wcscpy_s(nid.szInfo, msg);
    Shell_NotifyIcon(NIM_MODIFY, &nid);
}

static HMENU Menu() {
    HMENU h = CreatePopupMenu();
    AppendMenu(h, MF_STRING, ID_TRAY_CYCLE, L"Cycle");
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    g_menuInputIdToIndex.clear();
    for (size_t i = 0; i < g_cfg.inputs.size(); ++i) {
        const auto& in = g_cfg.inputs[i];
        std::wstring label = ToW(in.label);
        UINT id = ID_INPUT_BASE + (UINT)i;
        g_menuInputIdToIndex[id] = i;
        AppendMenu(h, MF_STRING, id, label.c_str());
    }
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    AppendMenu(h, MF_STRING, ID_TRAY_SETTINGS, L"Settings...");
    AppendMenu(h, MF_SEPARATOR, 0, nullptr);
    AppendMenu(h, MF_STRING, ID_TRAY_EXIT, L"Exit");
    return h;
}

static void RegisterHK(HWND hwnd) {
    Log("RegisterHK: unregistering all first");
    UnregisterAllHotkeys(hwnd);

    HotkeySpec hs{};
    if (ParseHotkey(g_cfg.hotkeys.cycle, hs)) {
        if (!RegisterHotKey(hwnd, HKID_CYCLE, hs.fsModifiers, hs.vk)) {
            Log("RegisterHotKey failed for cycle: mods=%u vk=%u err=%d", hs.fsModifiers, hs.vk, GetLastError());
        } else {
            Log("RegisterHotKey cycle registered: mods=%u vk=%u", hs.fsModifiers, hs.vk);
        }
    }

    UINT base = 100;
    g_directById.clear();
    for (auto& kv : g_cfg.hotkeys.direct) {
        HotkeySpec h2{};
        if (ParseHotkey(kv.second, h2)) {
            UINT id = base++;
            if (RegisterHotKey(hwnd, id, h2.fsModifiers, h2.vk)) {
                g_directById[id] = kv.first; // label
                Log("RegisterHotKey direct: id=%u label=%s mods=%u vk=%u", id, kv.first.c_str(), h2.fsModifiers, h2.vk);
            } else {
                Log("RegisterHotKey direct FAILED id=%u label=%s err=%d", id, kv.first.c_str(), GetLastError());
            }
        } else {
            Log("ParseHotkey failed for %s -> %s", kv.first.c_str(), kv.second.c_str());
        }
    }
}

static std::vector<InputDef> OrderedInputs() {
    std::map<std::string, const InputDef*> enabled;
    for (auto& in : g_cfg.inputs) enabled[in.label] = &in;
    std::vector<InputDef> out;
    std::set<std::string> used;
    for (auto& name : g_cfg.cycleOrder) {
        auto it = enabled.find(name);
        if (it != enabled.end()) { out.push_back(*it->second); used.insert(name); }
    }
    for (auto& kv : enabled) {
        if (!used.count(kv.first)) out.push_back(*kv.second);
    }
    if (out.empty()) for (auto& in : g_cfg.inputs) out.push_back(in);
    return out;
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        nid.cbSize = sizeof(nid);
        nid.hWnd = hwnd;
        nid.uID = 1;
        nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
        nid.uCallbackMessage = WM_APP + 1;
        nid.hIcon = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON),
            IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
        wcscpy_s(nid.szTip, L"LGInputSwitch");
        Shell_NotifyIcon(NIM_ADD, &nid);

        // Try to initialize ADL state; log result
        bool adlOk = InitADLAndState();
        if (!adlOk) {
            Log("WM_CREATE: ADL initialization failed. g_adlAvailable=%d", (int)g_adlAvailable);
            // Show immediate balloon so user sees an issue
            Balloon(L"ADL init failed: switching disabled. Check log in %TEMP%\\lginputswitch.log");
        } else {
            Log("WM_CREATE: ADL init OK");
        }

        bool loaded = LoadConfig(g_cfg);
        if (!loaded) {
            if (!ShowWelcomeDialog(hwnd) || !ShowSettingsDialogModal(hwnd, g_cfg)) {
                DestroyWindow(hwnd);
                return 0;
            }
            SaveConfig(g_cfg);
        }

        if (g_cfg.targets.empty()) g_cfg.targets.push_back({ 5,0 });
        g_target = { g_cfg.targets[0].first, g_cfg.targets[0].second };
        RegisterHK(hwnd);
        return 0;
    }

    case WM_HOTKEY: {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - g_lastPress).count() < g_cfg.debounceMs)
            return 0;
        g_lastPress = now;

        if (!g_adlAvailable) {
            Balloon(L"ADL not initialized — cannot switch. See log.");
            Log("WM_HOTKEY: Aborting because g_adlAvailable=false");
            return 0;
        }

        if (wParam == HKID_CYCLE) {
            auto ord = OrderedInputs();
            Log("WM_HOTKEY: CYCLE requested, ord.size=%d", (int)ord.size());
            if (ToggleCycle(g_target, g_cfg.i2cSourceAddr.c_str(), ord, g_cycleIndex)) {
                std::wstring msg = (g_cycleIndex >= 0 && g_cycleIndex < (int)ord.size())
                    ? ToW(ord[g_cycleIndex].label) : L"Switched";
                Balloon(msg.c_str());
                Log("ToggleCycle succeeded new index=%d", g_cycleIndex);
            } else {
                Balloon(L"Switch failed (check I2C/target). See log.");
                Log("ToggleCycle FAILED");
            }
            return 0;
        }

        auto it = g_directById.find((UINT)wParam);
        if (it != g_directById.end()) {
            Log("WM_HOTKEY: direct hotkey id=%u label=%s", (UINT)wParam, it->second.c_str());
            for (size_t i = 0; i < g_cfg.inputs.size(); ++i) {
                if (g_cfg.inputs[i].label == it->second) {
                    bool ok = SendInputCode(g_target, g_cfg.i2cSourceAddr.c_str(), g_cfg.inputs[i].code.c_str());
                    if (ok) {
                        g_cycleIndex = (int)i;
                        Balloon(ToW(it->second).c_str());
                        Log("SendInputCode succeeded for %s", it->second.c_str());
                    } else {
                        Balloon(L"Switch failed (check I2C/target). See log.");
                        Log("SendInputCode FAILED for %s", it->second.c_str());
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
            Log("WM_COMMAND: Settings requested");
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
        auto mit = g_menuInputIdToIndex.find(cmd);
        if (mit != g_menuInputIdToIndex.end()) {
            size_t i = mit->second;
            if (i < g_cfg.inputs.size()) {
                const auto& in = g_cfg.inputs[i];
                if (!g_adlAvailable) {
                    Balloon(L"ADL not initialized — cannot switch. See log.");
                    Log("WM_COMMAND: attempted menu switch but ADL not available");
                } else {
                    bool ok = SendInputCode(g_target, g_cfg.i2cSourceAddr.c_str(), in.code.c_str());
                    if (ok) {
                        g_cycleIndex = (int)i;
                        Balloon(ToW(in.label).c_str());
                        Log("Menu SendInputCode succeeded for %s", in.label.c_str());
                    } else {
                        Balloon(L"Switch failed (check I2C/target). See log.");
                        Log("Menu SendInputCode FAILED for %s", in.label.c_str());
                    }
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
        Log("WM_SETTINGS_SAVED received: reloading config");
        AppConfig tmp;
        if (LoadConfig(tmp)) {
            g_cfg = tmp;
            if (!g_cfg.targets.empty())
                g_target = { g_cfg.targets[0].first, g_cfg.targets[0].second };
            UnregisterAllHotkeys(hwnd);
            RegisterHK(hwnd);
            Balloon(L"Settings saved");
            Log("Reloaded config OK");
        } else {
            Balloon(L"Settings saved (reload failed)");
            Log("Reloading config FAILED");
        }
        return 0;
    }
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

int RunTrayApp() {
    LogInit();
    Log("RunTrayApp() start");

    // Try ADL init early - log result
    bool adlOk = InitADLAndState();
    Log("InitADLAndState early returned %d (g_adlAvailable=%d)", (int)adlOk, (int)g_adlAvailable);

    WNDCLASSEX wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.lpszClassName = kWndClass;
    wc.hIcon = LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON));
    wc.hIconSm = (HICON)LoadImage(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_APPICON),
        IMAGE_ICON, 16, 16, 0);
    RegisterClassEx(&wc);

    HWND hwnd = CreateWindowEx(0, kWndClass, L"", 0, 0, 0, 0, 0,
        nullptr, nullptr, GetModuleHandle(nullptr), nullptr);

    ShowWindow(GetConsoleWindow(), SW_HIDE);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    Log("RunTrayApp() exit");
    return (int)msg.wParam;
}
