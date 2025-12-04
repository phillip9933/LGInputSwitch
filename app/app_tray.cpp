1) Replace app/settings_ui.h
#pragma once
#include <windows.h>
#include "app_config.h"

// Show settings dialog modeless (returns immediately) — used by tray menu.
// The dialog will post WM_APP+2 to the parent when the user saves.
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg);

// Show settings dialog modally (blocking) — used at first-run before hotkeys registered.
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg);

2) Replace app/settings_ui.cpp
#include "settings_ui.h"
#include "hotkeys.h"
#include "util.h"
#include "types.h"
#include "../resource/resource.h"

#include <windows.h>
#include <vector>
#include <string>
#include <algorithm>
#include <sstream>
#include <set>
#include <stdio.h> // for swprintf

// Pull in ADL types / constants / globals
#include "../amdddc/adl.h"        // has ADLPROCS, AdapterInfo, ADL_DISPLAY_* flags

extern bool InitADL();            // from adl.cpp

// NOTE: This file MUST NOT mutate global ADL pointers (lpAdapterInfo, lpAdlDisplayInfo).
// We allocate ONLY local ADL adapter/display structures to avoid stomping global state
// used by the main app / hotkey thread.


// Helper to pack/unpack adapter/display into a single pointer-sized value
static inline LPARAM PackAD(int adapter, int display) {
    return (LPARAM)((((unsigned long long)(unsigned int)adapter) & 0xFFFFFFFFull) |
        ((((unsigned long long)(unsigned int)display) & 0xFFFFFFFFull) << 32));
}
static inline void UnpackAD(LPARAM v, int& adapter, int& display) {
    adapter = (int)((unsigned int)(v & 0xFFFFFFFFull));
    display = (int)((unsigned int)((v >> 32) & 0xFFFFFFFFull));
}

struct TargetItem {
    int adapterIndex{};
    int displayIndex{};
    std::wstring label; // e.g., "Adapter 5 - LG ULTRAGEAR+ (Display 0)"
};

static std::vector<TargetItem> g_targets;

// EnumerateTargets: use local ADL buffers only, do not touch global ADL memory.
static void EnumerateTargets() {
    g_targets.clear();

    // Ensure ADL initialized (safe to call multiple times)
    if (!InitADL()) return;

    // Get number of adapters
    int nAdapters = 0;
    if (adlprocs.ADL_Adapter_NumberOfAdapters_Get(&nAdapters) != 0 || nAdapters <= 0) {
        return;
    }

    // Allocate a local adapter info array
    std::vector<AdapterInfo> localAdapters;
    try {
        localAdapters.resize(nAdapters);
    } catch (...) {
        return;
    }
    memset(localAdapters.data(), 0, sizeof(AdapterInfo) * nAdapters);

    if (adlprocs.ADL_Adapter_AdapterInfo_Get(localAdapters.data(), sizeof(AdapterInfo) * nAdapters) != 0) {
        return;
    }

    // For each adapter, fetch display info into a local display pointer
    for (int i = 0; i < nAdapters; ++i) {
        int displayCount = 0;
        LPADLDisplayInfo localDisplayInfo = nullptr;

        int adapterIndex = localAdapters[i].iAdapterIndex;
        if (adlprocs.ADL_Display_DisplayInfo_Get(adapterIndex, &displayCount, &localDisplayInfo, 0) != 0) {
            // failed for this adapter — continue
            if (localDisplayInfo) { ADL_Main_Memory_Free((void**)&localDisplayInfo); localDisplayInfo = nullptr; }
            continue;
        }

        if (!localDisplayInfo) continue;

        for (int j = 0; j < displayCount; ++j) {
            const auto& di = localDisplayInfo[j];

            // Need both CONNECTED and MAPPED flags
            const int required = ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
            if ((di.iDisplayInfoValue & required) != required)
                continue;

            // Ensure the display is mapped to this adapter (compare fields from localAdapters)
            if (adapterIndex != di.displayID.iDisplayLogicalAdapterIndex)
                continue;

            TargetItem t;
            t.adapterIndex = adapterIndex;
            t.displayIndex = di.displayID.iDisplayLogicalIndex;

            std::wstring dispName = ToW(std::string(di.strDisplayName));
            std::wstringstream ss;
            ss << L"Adapter " << t.adapterIndex << L" - " << dispName << L" (Display " << t.displayIndex << L")";
            t.label = ss.str();

            g_targets.push_back(t);
        }

        // Free the local display info allocated by ADL
        ADL_Main_Memory_Free((void**)&localDisplayInfo);
    }
}

static void FillFromConfig(HWND hDlg, const AppConfig& cfg) {
    // Monitor combo
    HWND cb = GetDlgItem(hDlg, IDC_MONITOR);
    SendMessage(cb, CB_RESETCONTENT, 0, 0);
    int selectIndex = 0;

    for (size_t i = 0; i < g_targets.size(); ++i) {
        const auto& t = g_targets[i];
        int idx = (int)SendMessage(cb, CB_ADDSTRING, 0, (LPARAM)t.label.c_str());
        SendMessage(cb, CB_SETITEMDATA, idx, PackAD(t.adapterIndex, t.displayIndex));
        if (!cfg.targets.empty() &&
            t.adapterIndex == cfg.targets[0].first &&
            t.displayIndex == cfg.targets[0].second) {
            selectIndex = idx;
        }
    }
    SendMessage(cb, CB_SETCURSEL, selectIndex, 0);

    auto hasLabel = [&](const std::string& label) {
        return std::find_if(cfg.inputs.begin(), cfg.inputs.end(), [&](const InputDef& x) { return x.label == label; }) != cfg.inputs.end();
    };

    CheckDlgButton(hDlg, IDC_INPUT_DP, hasLabel("DisplayPort") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_INPUT_USBC, hasLabel("USB-C") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_INPUT_HDMI1, hasLabel("HDMI1") ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(hDlg, IDC_INPUT_HDMI2, hasLabel("HDMI2") ? BST_CHECKED : BST_UNCHECKED);

    // Hotkeys
    SetWindowTextA(GetDlgItem(hDlg, IDC_HOTKEY_CYCLE), cfg.hotkeys.cycle.c_str());

    auto setHK = [&](int id, const char* label) {
        auto it = cfg.hotkeys.direct.find(label);
        SetWindowTextA(GetDlgItem(hDlg, id), it == cfg.hotkeys.direct.end() ? "" : it->second.c_str());
    };

    setHK(IDC_HK_DP, "DisplayPort");
    setHK(IDC_HK_USBC, "USB-C");
    setHK(IDC_HK_HDMI1, "HDMI1");
    setHK(IDC_HK_HDMI2, "HDMI2");

    // Cycle order list: merge cycleOrder with currently enabled inputs
    HWND lb = GetDlgItem(hDlg, IDC_ORDER_LIST);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);

    std::set<std::string> enabled;
    for (const auto& in : cfg.inputs) enabled.insert(in.label);

    std::set<std::string> used;

    for (const auto& name : cfg.cycleOrder) {
        if (enabled.count(name) && !used.count(name)) {
            std::wstring ws = ToW(name);
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
            used.insert(name);
        }
    }
    for (const auto& in : cfg.inputs) {
        if (!used.count(in.label)) {
            std::wstring ws = ToW(in.label);
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
            used.insert(in.label);
        }
    }
    SendMessage(lb, LB_SETCURSEL, 0, 0);

    // I2C addr (show value or default 0x50)
    std::wstring wI2C = ToW(cfg.i2cSourceAddr.empty() ? std::string("0x50") : cfg.i2cSourceAddr);
    SetDlgItemTextW(hDlg, IDC_I2C_ADDR, wI2C.c_str());

    // Debounce (show value or default 750)
    int dbVal = (cfg.debounceMs > 0 ? cfg.debounceMs : 750);
    wchar_t bufDB[32];
    _snwprintf_s(bufDB, _TRUNCATE, L"%d", dbVal);
    SetDlgItemTextW(hDlg, IDC_DEBOUNCE, bufDB);
}

static std::string GetEditA(HWND hDlg, int ctlId) {
    char tmp[256] = {};
    GetWindowTextA(GetDlgItem(hDlg, ctlId), tmp, (int)sizeof(tmp));
    return std::string(tmp);
}

static void MoveSelected(HWND lb, bool up) {
    int sel = (int)SendMessage(lb, LB_GETCURSEL, 0, 0);
    if (sel == LB_ERR) return;
    int count = (int)SendMessage(lb, LB_GETCOUNT, 0, 0);
    int tgt = up ? sel - 1 : sel + 1;
    if (tgt < 0 || tgt >= count) return;

    wchar_t buf[128]; SendMessage(lb, LB_GETTEXT, sel, (LPARAM)buf);
    SendMessage(lb, LB_DELETESTRING, sel, 0);
    SendMessage(lb, LB_INSERTSTRING, tgt, (LPARAM)buf);
    SendMessage(lb, LB_SETCURSEL, tgt, 0);
}

static bool CollectToConfig(HWND hDlg, AppConfig& io) {
    // Monitor selection
    HWND cb = GetDlgItem(hDlg, IDC_MONITOR);

    if (!g_targets.empty()) {
        int idx = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
        if (idx == CB_ERR) idx = 0;

        LPARAM data = SendMessage(cb, CB_GETITEMDATA, idx, 0);
        int adapter = 0, display = 0; UnpackAD(data, adapter, display);
        io.targets = { {adapter, display} };
    }

    // Inputs with codes
    std::vector<InputDef> inputs;
    if (IsDlgButtonChecked(hDlg, IDC_INPUT_DP) == BST_CHECKED) inputs.push_back({ "DisplayPort","0xD0" });
    if (IsDlgButtonChecked(hDlg, IDC_INPUT_USBC) == BST_CHECKED) inputs.push_back({ "USB-C","0xD1" });
    if (IsDlgButtonChecked(hDlg, IDC_INPUT_HDMI1) == BST_CHECKED) inputs.push_back({ "HDMI1","0x90" });
    if (IsDlgButtonChecked(hDlg, IDC_INPUT_HDMI2) == BST_CHECKED) inputs.push_back({ "HDMI2","0x91" });
    if (inputs.empty()) return false;
    io.inputs = std::move(inputs);

    // Hotkeys
    io.hotkeys.cycle = GetEditA(hDlg, IDC_HOTKEY_CYCLE);
    io.hotkeys.direct.clear();
    auto addHK = [&](int id, const char* label) {
        auto v = GetEditA(hDlg, id);
        if (!v.empty()) io.hotkeys.direct[label] = v;
    };
    addHK(IDC_HK_DP, "DisplayPort");
    addHK(IDC_HK_USBC, "USB-C");
    addHK(IDC_HK_HDMI1, "HDMI1");
    addHK(IDC_HK_HDMI2, "HDMI2");

    // Build a set of enabled labels
    std::set<std::string> enabledLabels;
    for (auto& in : io.inputs) enabledLabels.insert(in.label);

    // Cycle order (only keep enabled items)
    HWND lb = GetDlgItem(hDlg, IDC_ORDER_LIST);
    int count = (int)SendMessage(lb, LB_GETCOUNT, 0, 0);
    io.cycleOrder.clear();
    for (int i = 0; i < count; ++i) {
        wchar_t w[128]; SendMessage(lb, LB_GETTEXT, i, (LPARAM)w);
        std::string s(w, w + wcslen(w));
        if (enabledLabels.count(s)) io.cycleOrder.push_back(s);
    }
    if (io.cycleOrder.empty()) {
        // default to all checked inputs, in the order they appear in `io.inputs`
        for (auto& in : io.inputs) io.cycleOrder.push_back(in.label);
    }

    // I2C + debounce
    io.i2cSourceAddr = GetEditA(hDlg, IDC_I2C_ADDR);
    if (io.i2cSourceAddr.empty()) io.i2cSourceAddr = "0x50";

    std::string d = GetEditA(hDlg, IDC_DEBOUNCE);
    io.debounceMs = d.empty() ? 750 : std::max(0, atoi(d.c_str()));

    return true;
}

// forward-declare wrapper procedures so we can create modal & modeless variations
static INT_PTR CALLBACK DlgProcModal(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
static INT_PTR CALLBACK DlgProcModeless(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

// Common handler used by modal+modeless wrappers
static INT_PTR DlgProcCommon(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam, bool modal) {
    static AppConfig* s_cfg = nullptr;

    switch (msg) {
    case WM_INITDIALOG: {
        s_cfg = reinterpret_cast<AppConfig*>(lParam);
        // Enumerate current targets (local-only)
        EnumerateTargets();
        FillFromConfig(hDlg, *s_cfg);

        // Seed order list with currently checked inputs if empty
        HWND lb = GetDlgItem(hDlg, IDC_ORDER_LIST);
        if (SendMessage(lb, LB_GETCOUNT, 0, 0) == 0) {
            if (IsDlgButtonChecked(hDlg, IDC_INPUT_DP) == BST_CHECKED)    SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)L"DisplayPort");
            if (IsDlgButtonChecked(hDlg, IDC_INPUT_USBC) == BST_CHECKED)  SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)L"USB-C");
            if (IsDlgButtonChecked(hDlg, IDC_INPUT_HDMI1) == BST_CHECKED) SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)L"HDMI1");
            if (IsDlgButtonChecked(hDlg, IDC_INPUT_HDMI2) == BST_CHECKED) SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)L"HDMI2");
        }
        return TRUE;
    }
    case WM_COMMAND: {
        switch (LOWORD(wParam)) {
        case IDC_ORDER_UP:   MoveSelected(GetDlgItem(hDlg, IDC_ORDER_LIST), true);  return TRUE;
        case IDC_ORDER_DOWN: MoveSelected(GetDlgItem(hDlg, IDC_ORDER_LIST), false); return TRUE;
        case IDC_SAVE: {
            if (!CollectToConfig(hDlg, *s_cfg)) {
                MessageBox(hDlg, L"Select at least one input.", L"LG Input Switch", MB_OK | MB_ICONWARNING);
                return TRUE;
            }
            // Save config to disk
            SaveConfig(*s_cfg);

            // Notify main window that settings were saved
            HWND parent = GetParent(hDlg);
            if (parent) {
                PostMessage(parent, WM_APP + 2, 0, 0);
            }

            if (modal) {
                EndDialog(hDlg, IDOK);
            } else {
                DestroyWindow(hDlg);
            }
            return TRUE;
        }
        case IDC_CANCEL:
            if (modal) EndDialog(hDlg, IDCANCEL);
            else DestroyWindow(hDlg);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

static INT_PTR CALLBACK DlgProcModal(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DlgProcCommon(hDlg, msg, wParam, lParam, true);
}
static INT_PTR CALLBACK DlgProcModeless(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    return DlgProcCommon(hDlg, msg, wParam, lParam, false);
}

// Show modeless (returns immediately). Dialog will POST WM_APP+2 to parent when saved.
bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg) {
    HWND dlg = CreateDialogParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_SETTINGS), parent, DlgProcModeless, (LPARAM)&ioCfg);
    if (!dlg) return false;
    ShowWindow(dlg, SW_SHOW);
    return true;
}

// Show modal — used at first-run (blocks until user saves or cancels)
bool ShowSettingsDialogModal(HWND parent, AppConfig& ioCfg) {
    INT_PTR r = DialogBoxParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_SETTINGS), parent, DlgProcModal, (LPARAM)&ioCfg);
    return r == IDOK;
}


Notes:

WM_APP + 2 is the message posted to the parent (main window) to indicate settings were saved.

EnumerateTargets() uses local ADL buffers and does not touch lpAdapterInfo or lpAdlDisplayInfo globals.

Modeless dialog created by CreateDialogParam will not block the main thread — hotkeys continue to be delivered.

3) Replace app/app_tray.cpp

This is a full copy/paste replacement of your app_tray.cpp. It restores the first-run modal behavior (so first-run flow still blocks before hotkeys are registered), but uses the modeless settings dialog for the tray menu and listens for WM_APP+2 to reload/save.

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
    wcscpy_s(nid.szInfo, msg);
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
    HotkeySpec hs{};
    if (ParseHotkey(g_cfg.hotkeys.cycle, hs))
        RegisterHotKey(hwnd, HKID_CYCLE, hs.fsModifiers, hs.vk);

    UINT base = 100;
    g_directById.clear();
    for (auto& kv : g_cfg.hotkeys.direct) {
        HotkeySpec h2{};
        if (ParseHotkey(kv.second, h2)) {
            UINT id = base++;
            if (RegisterHotKey(hwnd, id, h2.fsModifiers, h2.vk))
                g_directById[id] = kv.first; // label
        }
    }
}

static std::vector<InputDef> OrderedInputs() {
    // Build map of enabled inputs
    std::map<std::string, const InputDef*> enabled;
    for (auto& in : g_cfg.inputs) enabled[in.label] = &in;

    std::vector<InputDef> out;
    std::set<std::string> used;

    // 1) take items from cycleOrder that are enabled
    for (auto& name : g_cfg.cycleOrder) {
        auto it = enabled.find(name);
        if (it != enabled.end()) {
            out.push_back(*it->second);
            used.insert(name);
        }
    }
    // 2) append remaining enabled inputs
    for (auto& kv : enabled) {
        if (!used.count(kv.first)) out.push_back(*kv.second);
    }
    // fallback
    if (out.empty()) {
        for (auto& in : g_cfg.inputs) out.push_back(in);
    }
    return out;
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

        if (wParam == HKID_CYCLE) {
            auto ord = OrderedInputs();
            if (ToggleCycle(g_target, g_cfg.i2cSourceAddr.c_str(), ord, g_cycleIndex)) {
                std::wstring msg = (g_cycleIndex >= 0 && g_cycleIndex < (int)ord.size())
                    ? ToW(ord[g_cycleIndex].label) : L"Switched";
                Balloon(msg.c_str());
            } else {
                Balloon(L"Switch failed (check I2C/target)");
            }
            return 0;
        }
        // Direct hotkeys (mapped by label)
        auto it = g_directById.find((UINT)wParam);
        if (it != g_directById.end()) {
            for (size_t i = 0; i < g_cfg.inputs.size(); ++i) {
                if (g_cfg.inputs[i].label == it->second) {
                    if (SendInputCode(g_target, g_cfg.i2cSourceAddr.c_str(), g_cfg.inputs[i].code.c_str())) {
                        g_cycleIndex = (int)i;
                        Balloon(ToW(it->second).c_str());
                    } else {
                        Balloon(L"Switch failed (check I2C/target)");
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
                    g_cycleIndex = (int)i;
                    Balloon(ToW(in.label).c_str());
                } else {
                    Balloon(L"Switch failed (check I2C/target)");
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
