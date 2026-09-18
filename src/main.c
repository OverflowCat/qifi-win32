/*
 * main.c — Qifi Win32: Settings dialog + entry point
 *
 * Shows a dialog for the user to configure FPS, slice size, ECC level,
 * URL prefix, and select a file. On "Start" launches the QR window.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>

#include "resource.h"
#include "qr_window.h"

#pragma comment(lib, "comctl32.lib")

/* ---- State ---- */
static char g_filePath[MAX_PATH] = {0};

/* ---- Dialog callback ---- */
static INT_PTR CALLBACK DlgProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {

    case WM_INITDIALOG: {
        /* Defaults */
        SetDlgItemInt(hDlg, IDC_FPS,   30,  FALSE);
        SetDlgItemInt(hDlg, IDC_SLICE, 80,  FALSE);
        CheckRadioButton(hDlg, IDC_ECC_LOW, IDC_ECC_HIGH, IDC_ECC_LOW);

        /* Spinners */
        HWND hFps = GetDlgItem(hDlg, IDC_FPS_SPIN);
        SendMessage(hFps, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, IDC_FPS), 0);
        SendMessage(hFps, UDM_SETRANGE, 0, MAKELPARAM(120, 1));

        HWND hSl = GetDlgItem(hDlg, IDC_SLICE_SPIN);
        SendMessage(hSl, UDM_SETBUDDY, (WPARAM)GetDlgItem(hDlg, IDC_SLICE), 0);
        SendMessage(hSl, UDM_SETRANGE, 0, MAKELPARAM(2000, 8));

        /* If file was passed on command line, show it */
        if (g_filePath[0]) {
            SetDlgItemTextA(hDlg, IDC_FILE_PATH, g_filePath);
        }

        /* Center on screen */
        RECT rc;
        GetWindowRect(hDlg, &rc);
        int w = rc.right - rc.left, h = rc.bottom - rc.top;
        int sx = (GetSystemMetrics(SM_CXSCREEN) - w) / 2;
        int sy = (GetSystemMetrics(SM_CYSCREEN) - h) / 2;
        SetWindowPos(hDlg, NULL, sx, sy, 0, 0, SWP_NOSIZE | SWP_NOZORDER);

        return TRUE;
    }

    case WM_COMMAND:
        switch (LOWORD(wp)) {

        case IDC_BROWSE: {
            OPENFILENAMEA ofn = {0};
            char szFile[MAX_PATH] = {0};
            ofn.lStructSize = sizeof(ofn);
            ofn.hwndOwner   = hDlg;
            ofn.lpstrFile   = szFile;
            ofn.nMaxFile    = MAX_PATH;
            ofn.lpstrFilter = "All Files\0*.*\0";
            ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
            if (GetOpenFileNameA(&ofn)) {
                strcpy(g_filePath, szFile);
                SetDlgItemTextA(hDlg, IDC_FILE_PATH, g_filePath);
            }
            return TRUE;
        }

        case IDC_START: {
            /* Validate file */
            if (!g_filePath[0]) {
                MessageBoxA(hDlg, "Please select a file first.",
                            "Qifi", MB_ICONWARNING);
                return TRUE;
            }
            DWORD attr = GetFileAttributesA(g_filePath);
            if (attr == INVALID_FILE_ATTRIBUTES) {
                MessageBoxA(hDlg, "File not found.",
                            "Qifi", MB_ICONERROR);
                return TRUE;
            }

            /* Read settings */
            QifiSettings s = {0};
            s.fps        = GetDlgItemInt(hDlg, IDC_FPS,   NULL, FALSE);
            s.slice_size = GetDlgItemInt(hDlg, IDC_SLICE, NULL, FALSE);
            s.prefix     = "";  /* TODO: read from IDC_PREFIX if needed */

            if (s.fps < 1 || s.fps > 120)  s.fps = 30;
            if (s.slice_size < 8 || s.slice_size > 2000) s.slice_size = 80;

            if (IsDlgButtonChecked(hDlg, IDC_ECC_MED))       s.ecc = 1;
            else if (IsDlgButtonChecked(hDlg, IDC_ECC_QUART)) s.ecc = 2;
            else if (IsDlgButtonChecked(hDlg, IDC_ECC_HIGH))  s.ecc = 3;
            else                                               s.ecc = 0;

            /* Read prefix */
            {
                char prefix_buf[512];
                GetDlgItemTextA(hDlg, IDC_PREFIX, prefix_buf, sizeof(prefix_buf));
                if (prefix_buf[0]) {
                    char *p = _strdup(prefix_buf);
                    s.prefix = p;
                }
            }

            EndDialog(hDlg, IDOK);

            /* Launch QR window */
            qr_window_run(g_filePath, &s);

            if (s.prefix && s.prefix[0])
                free((void *)s.prefix);
            return TRUE;
        }

        case IDCANCEL:
            EndDialog(hDlg, IDCANCEL);
            return TRUE;
        }
        break;

    case WM_CLOSE:
        EndDialog(hDlg, IDCANCEL);
        return TRUE;
    }

    return FALSE;
}

/* ========================================================================
 *  Entry point
 * ======================================================================== */

int main(int argc, char *argv[]) {
    InitCommonControlsEx(&(INITCOMMONCONTROLSEX){sizeof(INITCOMMONCONTROLSEX),
                                                  ICC_STANDARD_CLASSES});

    /* If file passed on command line, pre-fill it */
    if (argc >= 2 && argv[1][0] != '-') {
        strncpy(g_filePath, argv[1], MAX_PATH - 1);
    }

    /* Quick CLI mode: --fps N --slice N <file> → skip dialog */
    if (argc >= 2) {
        const char *file = NULL;
        QifiSettings s = {30, 80, 0, ""};

        for (int i = 1; i < argc; i++) {
            if (strcmp(argv[i], "--fps") == 0 && i + 1 < argc)
                s.fps = atoi(argv[++i]);
            else if (strcmp(argv[i], "--slice") == 0 && i + 1 < argc)
                s.slice_size = atoi(argv[++i]);
            else if (strcmp(argv[i], "--ecc") == 0 && i + 1 < argc) {
                const char *e = argv[++i];
                if (e[0] == 'm' || e[0] == 'M') s.ecc = 1;
                else if (e[0] == 'q' || e[0] == 'Q') s.ecc = 2;
                else if (e[0] == 'h' || e[0] == 'H') s.ecc = 3;
            }
            else if (strcmp(argv[i], "--prefix") == 0 && i + 1 < argc)
                s.prefix = argv[++i];
            else if (argv[i][0] != '-')
                file = argv[i];
        }

        if (file) {
            /* Direct launch without dialog */
            return qr_window_run(file, &s) ? 0 : 1;
        }
    }

    /* No file on command line → show settings dialog */
    DialogBoxA(GetModuleHandle(NULL),
               MAKEINTRESOURCE(IDD_SETTINGS),
               NULL, DlgProc);

    return 0;
}
