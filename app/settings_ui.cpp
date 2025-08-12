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

// Pull in ADL types / constants / globals
#include "../amdddc/adl.h"        // has ADLPROCS, AdapterInfo, ADL_DISPLAY_* flags

extern bool InitADL();            // from your adl.cpp

extern "C" {
    extern ADLPROCS        adlprocs;         // implemented in adl.cpp
    extern LPAdapterInfo   lpAdapterInfo;    // implemented in adl.cpp
    extern LPADLDisplayInfo lpAdlDisplayInfo;// implemented in adl.cpp
}

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

static void EnumerateTargets() {
    g_targets.clear();
    if (!InitADL()) return;

    int nAdapters = 0;
    if (adlprocs.ADL_Adapter_NumberOfAdapters_Get(&nAdapters) != 0 || nAdapters <= 0) return;

    lpAdapterInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * nAdapters);
    memset(lpAdapterInfo, 0, sizeof(AdapterInfo) * nAdapters);
    adlprocs.ADL_Adapter_AdapterInfo_Get(lpAdapterInfo, sizeof(AdapterInfo) * nAdapters);

    for (int i = 0; i < nAdapters; ++i) {
        int displayCount = 0;

        if (lpAdlDisplayInfo) { ADL_Main_Memory_Free((void**)&lpAdlDisplayInfo); lpAdlDisplayInfo = nullptr; }
        if (adlprocs.ADL_Display_DisplayInfo_Get(lpAdapterInfo[i].iAdapterIndex, &displayCount, &lpAdlDisplayInfo, 0) != 0)
            continue;

        for (int j = 0; j < displayCount; ++j) {
            const auto& di = lpAdlDisplayInfo[j];

            // need both CONNECTED and MAPPED flags
            const int required = ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
            if ((di.iDisplayInfoValue & required) != required)
                continue;

            // must be mapped to this adapter
            if (lpAdapterInfo[i].iAdapterIndex != di.displayID.iDisplayLogicalAdapterIndex)
                continue;

            TargetItem t;
            t.adapterIndex = lpAdapterInfo[i].iAdapterIndex;
            t.displayIndex = di.displayID.iDisplayLogicalIndex;

            std::wstring dispName = ToW(std::string(di.strDisplayName));
            std::wstringstream ss;
            ss << L"Adapter " << t.adapterIndex << L" - " << dispName << L" (Display " << t.displayIndex << L")";
            t.label = ss.str();

            g_targets.push_back(t);
        }
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

    // Cycle order list
    HWND lb = GetDlgItem(hDlg, IDC_ORDER_LIST);
    SendMessage(lb, LB_RESETCONTENT, 0, 0);

    // Enabled labels (based on the checkboxes / cfg.inputs)
    std::set<std::string> enabled;
    for (const auto& in : cfg.inputs) enabled.insert(in.label);

    std::set<std::string> used;

    // 1) Add items from cycleOrder that are enabled
    for (const auto& name : cfg.cycleOrder) {
        if (enabled.count(name) && !used.count(name)) {
            std::wstring ws = ToW(name);
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
            used.insert(name);
        }
    }
    // 2) Append any remaining enabled inputs not already in the list
    for (const auto& in : cfg.inputs) {
        if (!used.count(in.label)) {
            std::wstring ws = ToW(in.label);
            SendMessage(lb, LB_ADDSTRING, 0, (LPARAM)ws.c_str());
            used.insert(in.label);
        }
    }

    SendMessage(lb, LB_SETCURSEL, 0, 0);
    // I2C addr (show value or default 0x50)
    {
        std::wstring w = ToW(cfg.i2cSourceAddr.empty() ? std::string("0x50") : cfg.i2cSourceAddr);
        SetDlgItemTextW(hDlg, IDC_I2C_ADDR, w.c_str());
    }

    // Debounce (show value or default 750)
    {
        int db = (cfg.debounceMs > 0 ? cfg.debounceMs : 750);
        wchar_t buf[32];
        _snwprintf_s(buf, _TRUNCATE, L"%d", db);
        SetDlgItemTextW(hDlg, IDC_DEBOUNCE, buf);
    }
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
    int idx = (int)SendMessage(cb, CB_GETCURSEL, 0, 0);
    if (idx == CB_ERR && !g_targets.empty()) idx = 0;
    if (idx != CB_ERR) {
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
    if (io.i2cSourceAddr.empty()) io.i2cSourceAddr = "0x50";  // <-- default LG alt path

    std::string d = GetEditA(hDlg, IDC_DEBOUNCE);
    io.debounceMs = d.empty() ? 750 : std::max(0, atoi(d.c_str()));

    return true;
}

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    static AppConfig* s_cfg = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        s_cfg = reinterpret_cast<AppConfig*>(lParam);
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
            SaveConfig(*s_cfg);
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        case IDC_CANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;
    }
    }
    return FALSE;
}

bool ShowSettingsDialog(HWND parent, AppConfig& ioCfg) {
    INT_PTR r = DialogBoxParam(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_SETTINGS), parent, DlgProc, (LPARAM)&ioCfg);
    return r == IDOK;
}
