#include "welcome_ui.h"
#include "../resource/resource.h"
#include <windows.h>

static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_COMMAND:
        if (LOWORD(wParam) == IDC_WELCOME_NEXT) { EndDialog(hDlg, IDOK); return TRUE; }
        if (LOWORD(wParam) == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    }
    return FALSE;
}

bool ShowWelcomeDialog(HWND parent) {
    INT_PTR r = DialogBox(GetModuleHandle(nullptr), MAKEINTRESOURCE(IDD_WELCOME), parent, DlgProc);
    return r == IDOK;
}
