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
    extern ADLPROCS        adlprocs;             // implemented in adl.cpp
    extern LPAdapterInfo   lpAdapterInfo;        // implemented in adl.cpp
    extern LPADLDisplayInfo lpAdlDisplayInfo;    // implemented in adl.cpp
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
    
    // Attempt to initialize. 
    InitADL(); 

    // Safety check: ensure ADL loaded and we have the function pointer
    if (!adlprocs.ADL_Adapter_NumberOfAdapters_Get) {
        return;
    }

    int nAdapters = 0;
    if (adlprocs.ADL_Adapter_NumberOfAdapters_Get(&nAdapters) != 0 || nAdapters <= 0) {
        return;
    }

    // Allocate memory for adapter info
    lpAdapterInfo = (LPAdapterInfo)malloc(sizeof(AdapterInfo) * nAdapters);
    if (!lpAdapterInfo) return; 
    
    memset(lpAdapterInfo, 0, sizeof(AdapterInfo) * nAdapters);
    
    // Get adapter info
    if (adlprocs.ADL_Adapter_AdapterInfo_Get(lpAdapterInfo, sizeof(AdapterInfo) * nAdapters) == 0) {
        for (int i = 0; i < nAdapters; ++i) {
            int displayCount = 0;

            // Cleanup previous display info if it exists from a prior loop (though unlikely in this flow)
            if (lpAdlDisplayInfo) { 
                ADL_Main_Memory_Free((void**)&lpAdlDisplayInfo); 
                lpAdlDisplayInfo = nullptr; 
            }

            // Get display info for this adapter
            if (adlprocs.ADL_Display_DisplayInfo_Get(lpAdapterInfo[i].iAdapterIndex, &displayCount, &lpAdlDisplayInfo, 0) != 0)
                continue;

            for (int j = 0; j < displayCount; ++j) {
                const auto& di = lpAdlDisplayInfo[j];

                // Need both CONNECTED and MAPPED flags
                const int required = ADL_DISPLAY_DISPLAYINFO_DISPLAYCONNECTED | ADL_DISPLAY_DISPLAYINFO_DISPLAYMAPPED;
                if ((di.iDisplayInfoValue & required) != required)
                    continue;

                // Must be mapped to this adapter
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
    
    // Cleanup
    if (lpAdapterInfo) {
        free(lpAdapterInfo);
        lpAdapterInfo = nullptr;
    }
    if (lpAdlDisplayInfo) {
        ADL_Main_Memory_Free((void**)&lpAdlDisplayInfo);
        lpAdlDisplayInfo = nullptr;
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
        SetDlgItem
