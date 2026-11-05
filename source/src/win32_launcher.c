/* Super Hang-On Ultimate launcher: DIP switch B settings, display mode, ROM check and PLAY. Returns to WinMain with the chosen settings. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include "win32_launcher.h"

enum { ID_DEMO = 101, ID_DIFF, ID_TIME, ID_MUSIC, ID_DISPLAY, ID_PLAY, ID_ROM, ID_ROMDETAIL, ID_CHK0, ID_CHK1, ID_CHKLBL0, ID_CHKLBL1 };
#define NCHK 2
#define NOPT 5

static HWND hwnd, h_lbl[NOPT], h_desc[NOPT], h_combo[NOPT], h_play, h_rom, h_romdetail, h_title, h_sub;
static HWND h_chk[NCHK], h_chklbl[NCHK], h_chkdesc[NCHK], h_md, h_mddetail;
static HFONT f_text, f_small, f_head, f_play;
static HBRUSH panel_brush;
static int dpi = 96, rom_ok, md_ok, result, last_nonmd;
static launcher_settings *S;
static launcher_rom_check_fn check_rom;
static const COLORREF PANEL = RGB(13, 33, 92), YELLOW = RGB(255, 214, 0), WHITE = RGB(255, 255, 255),
                      LIGHT = RGB(170, 190, 230), GOOD = RGB(70, 230, 120), BAD = RGB(255, 90, 90);

#define ART_W 0     /* no artwork: the window is just the settings panel */
#define ART_H 0
#define PANEL_W 420
#define CLIENT_H 586   /* room for all the options and the Mega Drive options */
static int S_(int v) { return MulDiv(v, dpi, 96); }

static int window_dpi(HWND w) {
    typedef UINT (WINAPI *gdfw_t)(HWND);
    static gdfw_t f; static int tried;
    if (!tried) { tried = 1; f = (gdfw_t)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "GetDpiForWindow"); }
    if (f && w) return (int)f(w);
    HDC dc = GetDC(NULL); int d = GetDeviceCaps(dc, LOGPIXELSY); ReleaseDC(NULL, dc); return d;
}

static void make_fonts(void) {
    if (f_text) { DeleteObject(f_text); DeleteObject(f_small); DeleteObject(f_head); DeleteObject(f_play); }
    f_text  = CreateFontA(-S_(15), 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_small = CreateFontA(-S_(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_head  = CreateFontA(-S_(22), 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
    f_play  = CreateFontA(-S_(28), 0, 0, 0, FW_HEAVY, 1, 0, 0, DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, "Segoe UI");
}

static void layout(void) {
    int x0 = S_(ART_W) + S_(28), w = S_(PANEL_W) - S_(56), y = S_(18);
    MoveWindow(h_title, x0, y, w, S_(30), FALSE); y += S_(30);
    MoveWindow(h_sub, x0, y, w, S_(18), FALSE); y += S_(28);
    for (int i = 0; i < NOPT; i++) {
        MoveWindow(h_lbl[i], x0, y, S_(130), S_(22), FALSE);
        MoveWindow(h_combo[i], x0 + S_(130), y - S_(3), w - S_(130), S_(200), FALSE);   /* height includes the drop list */
        MoveWindow(h_desc[i], x0, y + S_(23), w, S_(16), FALSE);
        y += S_(47);
    }
    y += S_(6);
    for (int i = 0; i < NCHK; i++) {
        MoveWindow(h_chk[i], x0, y + S_(2), S_(18), S_(18), FALSE);
        MoveWindow(h_chklbl[i], x0 + S_(26), y, w - S_(26), S_(22), FALSE);
        MoveWindow(h_chkdesc[i], x0 + S_(26), y + S_(22), w - S_(26), S_(16), FALSE);
        y += i == 0 ? S_(32) : S_(44);   /* Mega Drive Enhancements has no description line (the ROM status covers it) */
    }
    int ybottom = S_(CLIENT_H);
    MoveWindow(h_play, x0, ybottom - S_(22) - S_(60), w, S_(60), FALSE);
    int yb = ybottom - S_(22) - S_(60) - S_(12);
    MoveWindow(h_mddetail, x0, yb - S_(18), w, S_(18), FALSE);
    MoveWindow(h_md, x0, yb - S_(18) - S_(22), w, S_(22), FALSE);
    MoveWindow(h_romdetail, x0, yb - S_(18) - S_(22) - S_(6) - S_(18), w, S_(18), FALSE);
    MoveWindow(h_rom, x0, yb - S_(18) - S_(22) - S_(6) - S_(18) - S_(22), w, S_(22), FALSE);
    HWND all[] = { h_title, h_sub, h_rom, h_romdetail, h_play, h_chklbl[0], h_chklbl[1], h_chkdesc[0], h_chkdesc[1], h_md, h_mddetail };
    for (int i = 0; i < 11; i++) InvalidateRect(all[i], NULL, TRUE);
    InvalidateRect(hwnd, NULL, TRUE);
}

static void apply_fonts(void) {
    SendMessageA(h_title, WM_SETFONT, (WPARAM)f_head, TRUE);
    SendMessageA(h_sub, WM_SETFONT, (WPARAM)f_small, TRUE);
    for (int i = 0; i < NOPT; i++) {
        SendMessageA(h_lbl[i], WM_SETFONT, (WPARAM)f_text, TRUE);
        SendMessageA(h_combo[i], WM_SETFONT, (WPARAM)f_text, TRUE);
        SendMessageA(h_desc[i], WM_SETFONT, (WPARAM)f_small, TRUE);
    }
    for (int i = 0; i < NCHK; i++) { SendMessageA(h_chklbl[i], WM_SETFONT, (WPARAM)f_text, TRUE); SendMessageA(h_chkdesc[i], WM_SETFONT, (WPARAM)f_small, TRUE); }
    SendMessageA(h_rom, WM_SETFONT, (WPARAM)f_text, TRUE);
    SendMessageA(h_romdetail, WM_SETFONT, (WPARAM)f_small, TRUE);
    SendMessageA(h_md, WM_SETFONT, (WPARAM)f_text, TRUE);
    SendMessageA(h_mddetail, WM_SETFONT, (WPARAM)f_small, TRUE);
    SendMessageA(h_play, WM_SETFONT, (WPARAM)f_play, TRUE);
}

static void resize_client(void) {
    RECT r = { 0, 0, S_(ART_W + PANEL_W), S_(CLIENT_H) };
    typedef BOOL (WINAPI *awrd_t)(LPRECT, DWORD, BOOL, DWORD, UINT);
    awrd_t f = (awrd_t)(void *)GetProcAddress(GetModuleHandleA("user32.dll"), "AdjustWindowRectExForDpi");
    DWORD style = (DWORD)GetWindowLongA(hwnd, GWL_STYLE);
    if (f) f(&r, style, FALSE, 0, (UINT)dpi); else AdjustWindowRect(&r, style, FALSE);
    SetWindowPos(hwnd, NULL, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER);
}

static void update_music_item(void);
static void refresh_rom(void) {
    char msg[256] = "", detail[256] = "";
    rom_ok = check_rom(msg, sizeof msg, detail, sizeof detail);
    SetWindowTextA(h_rom, msg);
    SetWindowTextA(h_romdetail, detail);
    EnableWindow(h_play, rom_ok);
    if (S->md_check) {   /* Mega Drive ROM: without it the Mega Drive options are greyed out */
        char m2[256] = "", d2[256] = "";
        md_ok = S->md_check(m2, sizeof m2, d2, sizeof d2);
        SetWindowTextA(h_md, m2); SetWindowTextA(h_mddetail, d2);
        for (int i = 0; i < NCHK; i++) { EnableWindow(h_chk[i], md_ok); InvalidateRect(h_chklbl[i], NULL, TRUE); InvalidateRect(h_chkdesc[i], NULL, TRUE); }
        InvalidateRect(h_md, NULL, TRUE); InvalidateRect(h_mddetail, NULL, TRUE);
    }
    update_music_item();
    InvalidateRect(h_rom, NULL, TRUE); InvalidateRect(h_romdetail, NULL, TRUE); InvalidateRect(h_play, NULL, TRUE);
}

/* the Mega Drive Music entry of the Music list needs the Mega Drive ROM and the Mega Drive Enhancements tick */
static int music_md_ok(void) { return md_ok && h_chk[0] && SendMessageA(h_chk[0], BM_GETCHECK, 0, 0) == BST_CHECKED; }
static void update_music_item(void) {
    if (!h_combo[3]) return;
    if (!music_md_ok() && SendMessageA(h_combo[3], CB_GETCURSEL, 0, 0) == 2) SendMessageA(h_combo[3], CB_SETCURSEL, (WPARAM)last_nonmd, 0);
    InvalidateRect(h_combo[3], NULL, TRUE);
}

static void read_choices(void) {
    S->demo_sounds = (int)SendMessageA(h_combo[0], CB_GETCURSEL, 0, 0) == 0;
    S->difficulty = (int)SendMessageA(h_combo[1], CB_GETCURSEL, 0, 0);
    S->time_adj = (int)SendMessageA(h_combo[2], CB_GETCURSEL, 0, 0);
    { int ms = (int)SendMessageA(h_combo[3], CB_GETCURSEL, 0, 0); S->arrange_music = ms == 1; S->md_music = ms == 2 && music_md_ok(); }
    S->widescreen = (int)SendMessageA(h_combo[4], CB_GETCURSEL, 0, 0) == 0;
    if (md_ok) {   /* greyed out: keep the saved choices for when the ROM is back */
        S->md_enhancements = SendMessageA(h_chk[0], BM_GETCHECK, 0, 0) == BST_CHECKED;
        S->md_controls = SendMessageA(h_chk[1], BM_GETCHECK, 0, 0) == BST_CHECKED;
    }
}

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_PAINT: {
        PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps);
        RECT rc; GetClientRect(h, &rc);
        RECT panel = { S_(ART_W), 0, rc.right, rc.bottom };
        FillRect(dc, &panel, panel_brush);
        /* yellow accent line down the left edge */
        RECT bar = { S_(ART_W), 0, S_(ART_W) + S_(5), rc.bottom };
        HBRUSH yb = CreateSolidBrush(YELLOW); FillRect(dc, &bar, yb); DeleteObject(yb);
        EndPaint(h, &ps);
        return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_CTLCOLORSTATIC: {
        HDC dc = (HDC)w; HWND c = (HWND)l;
        SetBkMode(dc, TRANSPARENT);
        COLORREF col = WHITE;
        if (c == h_title) col = YELLOW;
        else if (c == h_chklbl[0] || c == h_chklbl[1]) col = md_ok ? YELLOW : RGB(105, 115, 150);
        else if (c == h_chkdesc[0] || c == h_chkdesc[1]) col = md_ok ? LIGHT : RGB(95, 105, 140);
        else if (c == h_md) col = md_ok ? GOOD : RGB(175, 180, 200);
        else if (c == h_mddetail) col = md_ok ? RGB(150, 210, 170) : RGB(140, 148, 175);
        else if (c == h_rom) col = rom_ok ? GOOD : BAD;
        else if (c == h_romdetail) col = rom_ok ? RGB(150, 210, 170) : RGB(255, 170, 170);
        else for (int i = 0; i < NOPT; i++) if (c == h_desc[i] || c == h_sub) col = LIGHT;
        SetTextColor(dc, col);
        return (LRESULT)panel_brush; }
    case WM_MEASUREITEM: {
        MEASUREITEMSTRUCT *mi = (MEASUREITEMSTRUCT *)l;
        if (mi->CtlID == ID_MUSIC) { mi->itemHeight = (UINT)S_(20); return TRUE; }
        break; }
    case WM_DRAWITEM: {
        DRAWITEMSTRUCT *d = (DRAWITEMSTRUCT *)l;
        if (d->CtlID == ID_MUSIC) {   /* Music list: the Mega Drive entry is greyed out unless it can be chosen */
            char t[64] = ""; int idx = (int)d->itemID;
            if (idx >= 0) SendMessageA(d->hwndItem, CB_GETLBTEXT, (WPARAM)idx, (LPARAM)t);
            int off = idx == 2 && !music_md_ok(), sel = (d->itemState & ODS_SELECTED) && !off && !(d->itemState & ODS_COMBOBOXEDIT && 0);
            FillRect(d->hDC, &d->rcItem, GetSysColorBrush(sel ? COLOR_HIGHLIGHT : COLOR_WINDOW));
            SetBkMode(d->hDC, TRANSPARENT);
            SetTextColor(d->hDC, GetSysColor(off ? COLOR_GRAYTEXT : sel ? COLOR_HIGHLIGHTTEXT : COLOR_WINDOWTEXT));
            HGDIOBJ of = SelectObject(d->hDC, f_text);
            RECT tr = d->rcItem; tr.left += S_(6);
            DrawTextA(d->hDC, t, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            SelectObject(d->hDC, of);
            return TRUE;
        }
        if (d->CtlID != ID_PLAY) break;
        int enabled = IsWindowEnabled(d->hwndItem), down = (d->itemState & ODS_SELECTED) != 0;
        COLORREF fill = !enabled ? RGB(90, 90, 110) : down ? RGB(170, 20, 25) : RGB(220, 35, 40);
        HBRUSH b = CreateSolidBrush(fill);
        HPEN p = CreatePen(PS_SOLID, S_(3), enabled ? YELLOW : RGB(140, 140, 160));
        HGDIOBJ ob = SelectObject(d->hDC, b), op = SelectObject(d->hDC, p);
        FillRect(d->hDC, &d->rcItem, panel_brush);
        RoundRect(d->hDC, d->rcItem.left + S_(2), d->rcItem.top + S_(2), d->rcItem.right - S_(2), d->rcItem.bottom - S_(2), S_(14), S_(14));
        SelectObject(d->hDC, ob); SelectObject(d->hDC, op); DeleteObject(b); DeleteObject(p);
        SetBkMode(d->hDC, TRANSPARENT); SetTextColor(d->hDC, enabled ? WHITE : RGB(190, 190, 200));
        HGDIOBJ of = SelectObject(d->hDC, f_play);
        RECT tr = d->rcItem; if (down) OffsetRect(&tr, 0, S_(1));
        DrawTextA(d->hDC, "PLAY", -1, &tr, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        SelectObject(d->hDC, of);
        return TRUE; }
    case WM_COMMAND:
        if (LOWORD(w) >= ID_CHKLBL0 && LOWORD(w) <= ID_CHKLBL1 && md_ok) {   /* clicking the text toggles the box */
            HWND b = h_chk[LOWORD(w) - ID_CHKLBL0];
            SendMessageA(b, BM_SETCHECK, SendMessageA(b, BM_GETCHECK, 0, 0) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED, 0);
            update_music_item();
            return 0;
        }
        if (LOWORD(w) == ID_CHK0 || LOWORD(w) == ID_CHK1) { update_music_item(); return 0; }
        if (LOWORD(w) == ID_MUSIC && HIWORD(w) == CBN_SELCHANGE) {
            int sel = (int)SendMessageA(h_combo[3], CB_GETCURSEL, 0, 0);
            if (sel == 2 && !music_md_ok()) SendMessageA(h_combo[3], CB_SETCURSEL, (WPARAM)last_nonmd, 0); else if (sel != 2) last_nonmd = sel;
            return 0;
        }
        if (LOWORD(w) == ID_PLAY && rom_ok) { read_choices(); result = 1; DestroyWindow(h); }
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(w) != WA_INACTIVE && check_rom) refresh_rom();   /* re-check after the user adds the ROM */
        break;
    case WM_DPICHANGED: {
        dpi = HIWORD(w);
        RECT *r = (RECT *)l;
        SetWindowPos(h, NULL, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER);
        make_fonts(); apply_fonts(); resize_client(); layout();
        return 0; }
    case WM_CLOSE: result = 0; DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static HWND label(const char *t, DWORD extra) { return CreateWindowExA(0, "STATIC", t, WS_CHILD | WS_VISIBLE | extra, 0, 0, 10, 10, hwnd, NULL, NULL, NULL); }
static HWND combo(int id, const char *const *items, int n, int sel) {
    HWND c = CreateWindowExA(0, "COMBOBOX", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST | (id == ID_MUSIC ? CBS_OWNERDRAWFIXED | CBS_HASSTRINGS : 0),
                             0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)id, NULL, NULL);
    for (int i = 0; i < n; i++) SendMessageA(c, CB_ADDSTRING, 0, (LPARAM)items[i]);
    SendMessageA(c, CB_SETCURSEL, (WPARAM)sel, 0);
    return c;
}

int launcher_run(HINSTANCE inst, launcher_settings *s, launcher_rom_check_fn check) {
    S = s; check_rom = check; result = 0;
    LoadLibraryA("comctl32.dll");   /* themed controls (manifest requests v6) */
    panel_brush = CreateSolidBrush(PANEL);
    WNDCLASSA wc; memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = proc; wc.hInstance = inst; wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hIcon = LoadIconA(inst, MAKEINTRESOURCEA(1)); wc.lpszClassName = "SuperHangOnLauncher";
    RegisterClassA(&wc);
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    hwnd = CreateWindowExA(0, wc.lpszClassName, "Super Hang-On Ultimate", style, CW_USEDEFAULT, CW_USEDEFAULT, 800, 520, NULL, NULL, inst, NULL);
    dpi = window_dpi(hwnd);
    make_fonts();

    static const char *const onoff[] = { "On", "Off" };
    static const char *const levels[] = { "Easy", "Normal", "Hard", "Hardest" };
    static const char *const display[] = { "16:9 widescreen", "4:3 original" };
    static const char *const music[] = { "Regular Soundtrack", "Arrange Soundtrack", "Mega Drive Music" };
    h_title = label("Super Hang-On Ultimate", 0);
    h_sub = label("Native Port + Mega Drive Original Mode Mod", 0);
    const char *labels[NOPT] = { "Demo sounds", "Difficulty", "Time limit", "Music", "Display" };
    const char *descs[NOPT] = { "Sound during the attract mode", "How often rival bikes appear",
                                "Time allowed to reach each checkpoint", s->music_note[0] ? s->music_note : "The four music-select songs",
                                "F9 still switches during play" };
    for (int i = 0; i < NOPT; i++) { h_lbl[i] = label(labels[i], SS_LEFT); h_desc[i] = label(descs[i], SS_LEFT); }
    h_combo[0] = combo(ID_DEMO, onoff, 2, s->demo_sounds ? 0 : 1);
    h_combo[1] = combo(ID_DIFF, levels, 4, s->difficulty & 3);
    h_combo[2] = combo(ID_TIME, levels, 4, s->time_adj & 3);
    last_nonmd = s->arrange_music ? 1 : 0;
    h_combo[3] = combo(ID_MUSIC, music, 3, s->md_music && s->md_enhancements ? 2 : last_nonmd);
    h_combo[4] = combo(ID_DISPLAY, display, 2, s->widescreen ? 0 : 1);
    {   /* Mega Drive options: check box + text (themed check boxes ignore text colours on a dark panel) */
        const char *cl[NCHK] = { "Mega Drive Enhancements", "Apply Mega Drive Controls" };
        const char *cd[NCHK] = { "", "Mega Drive steering feel, for arcade or Original Mode" };
        int on[NCHK] = { s->md_enhancements, s->md_controls };
        for (int i = 0; i < NCHK; i++) {
            h_chk[i] = CreateWindowExA(0, "BUTTON", "", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_CHK0 + i), NULL, NULL);
            SendMessageA(h_chk[i], BM_SETCHECK, on[i] ? BST_CHECKED : BST_UNCHECKED, 0);
            h_chklbl[i] = CreateWindowExA(0, "STATIC", cl[i], WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOTIFY, 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)(ID_CHKLBL0 + i), NULL, NULL);
            h_chkdesc[i] = label(cd[i], SS_LEFT);
        }
    }
    h_rom = label("", SS_LEFT | SS_ENDELLIPSIS);
    h_romdetail = label("", SS_LEFT | SS_ENDELLIPSIS);
    h_md = label("", SS_LEFT | SS_ENDELLIPSIS);
    h_mddetail = label("", SS_LEFT | SS_ENDELLIPSIS);
    h_play = CreateWindowExA(0, "BUTTON", "PLAY", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW | BS_DEFPUSHBUTTON,
                             0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)ID_PLAY, NULL, NULL);
    apply_fonts(); resize_client(); layout();
    refresh_rom();
    ShowWindow(hwnd, SW_SHOW);
    SetFocus(rom_ok ? h_play : h_combo[0]);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_ESCAPE) { result = 0; DestroyWindow(hwnd); continue; }
        if (msg.message == WM_KEYDOWN && msg.wParam == VK_RETURN && rom_ok) {
            /* Enter plays, unless a drop-down list is open */
            int open = 0; for (int i = 0; i < NOPT; i++) if (SendMessageA(h_combo[i], CB_GETDROPPEDSTATE, 0, 0)) open = 1;
            if (!open) { read_choices(); result = 1; DestroyWindow(hwnd); continue; }
        }
        if (!IsDialogMessageA(hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageA(&msg); }
    }
    DeleteObject(panel_brush);
    DeleteObject(f_text); DeleteObject(f_small); DeleteObject(f_head); DeleteObject(f_play);
    f_text = f_small = f_head = f_play = NULL;
    return result;
}
