// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// EnhancerSkin.cpp - native control panel for the Enhancer APO.
// Two looks: "vector" (default) redraws the original layout with the original colours at any scale with crisp
// fonts; "pixel" blits the original skin sprite sheet extracted from dsp_enh.dll with integer zoom.
// Talks to the APO through HKLM\SOFTWARE\EnhancerAPO (params) and reads AutoGain published by the APO.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <commctrl.h>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>
#include <algorithm>
#include "resource.h"
#include "Installer.h"

static const wchar_t* PARAM_KEY = L"SOFTWARE\\EnhancerAPO";
static const int W = 275, H = 232;
static const int NSL = 10;
static const wchar_t* SL_NAMES[NSL] = {L"Volume", L"HarmBass", L"HarmBassRange", L"DrumBass", L"DrumBassRange", L"Dry", L"HarmTreble", L"HarmTrebleRange", L"Ambience", L"AmbienceRange"};
static const wchar_t* SL_LABELS[NSL] = {L"Volume", L"Harmonic Bass", L"Harmonic Bass Range", L"Drum Bass", L"Drum Bass Range", L"Dry Signal", L"Harmonic Treble", L"Harmonic Treble Range", L"Ambience", L"Ambience Range"};
static const wchar_t* BTN_LABELS[5] = {L"Power", L"Boost", L"Presets", L"Help", L"About"};
// geometry measured on the original skin bitmap (logical 275x232 coordinates)
static const int GROOVE_X0 = 14, GROOVE_X1 = 260, GROOVE_Y0 = 22, GROOVE_DY = 19, GROOVE_H = 10, LABEL_Y0 = 12;
static const int KNOB_W = 23, KNOB_H = 10, KNOB_SRC_Y = 254, KNOB_ACT_SRC_Y = 265, KNOB_SRC_X0 = 1, KNOB_SRC_DX = 25;
static const int MAX_SRC_X = 250, MAX_SRC_Y = 244, MAX_W = 23, MAX_H = 7;
struct Btn { int x0, x1; };
static const Btn BTN[5] = {{13, 59}, {63, 109}, {114, 160}, {165, 211}, {215, 261}};
static const int BTN_Y0 = 208, BTN_Y1 = 222, PRESSED_SRC_Y = 232, PRESSED_DY = 26, PRESSED_H = 17;
static const RECT RC_MIN = {252, 2, 263, 11}, RC_CLOSE = {264, 2, 275, 11};
// colours sampled from the original skin
static const COLORREF C_BG = RGB(33, 33, 57), C_GROOVE = RGB(24, 24, 16), C_GRAY = RGB(132, 132, 132), C_BLACK = RGB(0, 0, 0);
static const COLORREF C_GREEN = RGB(0, 255, 0), C_CYAN = RGB(0, 239, 239), C_RED = RGB(255, 0, 0), C_GOLD = RGB(222, 222, 107);
static const COLORREF C_FRAME = RGB(90, 107, 132), C_FRAME2 = RGB(24, 24, 49), C_TITLEBTN = RGB(123, 132, 156);
static const COLORREF C_BTN = RGB(156, 156, 156), C_BTN_HI = RGB(181, 189, 189), C_BTN_LO = RGB(99, 99, 107), C_LED_OFF = RGB(24, 72, 24);

struct Preset { std::wstring name; int v[NSL]; };
static const wchar_t* FACTORY =
L"Normal|72,31,50,30,50,100,50,50,0,50;Normal with ambience|72,31,50,30,50,100,50,50,25,50;General improvements|72,31,50,30,50,100,72,50,17,31;"
L"Party ! (with ambience)|100,31,100,38,71,100,82,62,20,50;Party ! (with more bass)|100,40,100,38,56,100,62,62,0,50;Aggressive|100,31,50,30,50,100,90,53,0,50;"
L"Bass & Treble boost|72,50,50,50,50,100,85,50,0,50;Extrem Bass & Treble|72,50,50,50,50,50,89,40,0,50;Drums boost|72,0,50,75,100,100,30,85,0,50;"
L"Deep Bass boost|72,63,75,0,0,100,30,85,0,50;Extrem Bass boost|72,62,62,62,38,100,30,85,0,50";

static HINSTANCE g_inst; static HWND g_wnd; static HBITMAP g_skin; static HDC g_skinDC;
static int g_val[NSL]; static bool g_power = true, g_boost = false; static double g_autoGain = 1.0;
static int g_drag = -1; static std::vector<Preset> g_presets; static bool g_keyOk = false;
static int g_zoom10 = 10; static bool g_vector = true; static int g_dpi = 96;   // zoom in tenths (10 = 1x)

static double curScale() { return g_vector ? (g_zoom10 / 10.0) * g_dpi / 96.0 : (double)std::max(1, g_zoom10 / 10); }
static int winW() { return (int)std::lround(W * curScale()); }
static int winH() { return (int)std::lround(H * curScale()); }

// ---------------------------------------------------------------- registry
static bool regReadAll() {
    HKEY k; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    bool changed = false;
    auto rd = [&](const wchar_t* n, int def) { DWORD v = def, sz = sizeof v; RegQueryValueExW(k, n, nullptr, nullptr, (BYTE*)&v, &sz); return (int)v; };
    for (int i = 0; i < NSL; i++) { int v = std::clamp(rd(SL_NAMES[i], g_val[i]), 0, 100); if (v != g_val[i]) { g_val[i] = v; changed = true; } }
    bool p = rd(L"Power", g_power) != 0, b = rd(L"Boost", g_boost) != 0;
    if (p != g_power || b != g_boost) { g_power = p; g_boost = b; changed = true; }
    double ag = rd(L"AutoGain", 10000) / 10000.0; if (std::fabs(ag - g_autoGain) > 1e-4) { g_autoGain = ag; changed = true; }
    RegCloseKey(k); return changed;
}
static bool regKeyExists() {   // "can we open the settings key" - independent of whether the values changed
    HKEY k; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    RegCloseKey(k); return true;
}
static void regWrite(const wchar_t* name, int v) {
    HKEY k; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return;
    DWORD d = (DWORD)v; RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&d, sizeof d); RegCloseKey(k);
}
static int userPref(const wchar_t* name, int def) {
    HKEY k; DWORD v = def, sz = sizeof v;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\EnhancerAPO", 0, KEY_READ, &k) == ERROR_SUCCESS) { RegQueryValueExW(k, name, nullptr, nullptr, (BYTE*)&v, &sz); RegCloseKey(k); }
    return (int)v;
}
static void setUserPref(const wchar_t* name, int v) {
    HKEY k; if (RegCreateKeyExW(HKEY_CURRENT_USER, L"SOFTWARE\\EnhancerAPO", 0, nullptr, 0, KEY_WRITE, nullptr, &k, nullptr) == ERROR_SUCCESS) { DWORD d = v; RegSetValueExW(k, name, 0, REG_DWORD, (const BYTE*)&d, sizeof d); RegCloseKey(k); }
}

// ---------------------------------------------------------------- presets
static void loadPresets() {
    g_presets.clear();
    auto parse = [&](const std::wstring& name, const std::wstring& vals) {
        Preset p; p.name = name; int n = 0; size_t s = 0;
        while (n < NSL && s <= vals.size()) { size_t e = vals.find(L',', s); if (e == std::wstring::npos) e = vals.size(); p.v[n++] = _wtoi(vals.substr(s, e - s).c_str()); s = e + 1; }
        if (n == NSL) g_presets.push_back(p);
    };
    FILE* f = _wfopen(L"C:\\Program Files (x86)\\Winamp\\Plugins\\Enhancer\\017\\enhancer.set", L"r");
    if (f) {
        char line[512]; std::vector<std::string> lines;
        while (fgets(line, sizeof line, f)) { std::string s(line); while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back(); if (!s.empty()) lines.push_back(s); }
        fclose(f);
        for (size_t i = 1; i + 1 < lines.size(); i += 2) { std::wstring n(lines[i].begin(), lines[i].end()), v(lines[i + 1].begin(), lines[i + 1].end()); parse(n, v); }
    }
    if (g_presets.empty()) {
        std::wstring all = FACTORY; size_t s = 0;
        while (s < all.size()) { size_t e = all.find(L';', s); if (e == std::wstring::npos) e = all.size(); std::wstring item = all.substr(s, e - s); size_t bar = item.find(L'|'); parse(item.substr(0, bar), item.substr(bar + 1)); s = e + 1; }
    }
}

// ---------------------------------------------------------------- tray icon
#define WM_TRAY (WM_APP + 1)
static NOTIFYICONDATAW g_nid{}; static HICON g_icon = nullptr;
static HICON makeIcon(bool on) {
    HDC dc = GetDC(nullptr); HDC m = CreateCompatibleDC(dc); HBITMAP color = CreateCompatibleBitmap(dc, 32, 32); HBITMAP old = (HBITMAP)SelectObject(m, color);
    HBRUSH bg = CreateSolidBrush(C_BG); RECT r = {0, 0, 32, 32}; FillRect(m, &r, bg); DeleteObject(bg);
    HBRUSH led = CreateSolidBrush(on ? C_GREEN : C_LED_OFF); RECT l = {8, 8, 24, 24}; FillRect(m, &l, led); DeleteObject(led);
    HBRUSH edge = CreateSolidBrush(C_TITLEBTN); FrameRect(m, &r, edge); DeleteObject(edge);
    SelectObject(m, old); DeleteDC(m); ReleaseDC(nullptr, dc);
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO ii{}; ii.fIcon = TRUE; ii.hbmColor = color; ii.hbmMask = mask;
    HICON ic = CreateIconIndirect(&ii); DeleteObject(color); DeleteObject(mask); return ic;
}
static void trayUpdate(HWND h, bool add) {
    if (g_icon) DestroyIcon(g_icon);
    g_icon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(g_power ? IDI_MAIN : IDI_OFF), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
    g_nid.cbSize = sizeof g_nid; g_nid.hWnd = h; g_nid.uID = 1; g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP; g_nid.uCallbackMessage = WM_TRAY; g_nid.hIcon = g_icon;
    wcscpy_s(g_nid.szTip, g_power ? L"Enhancer Reloaded (on)" : L"Enhancer Reloaded (off)");
    Shell_NotifyIconW(add ? NIM_ADD : NIM_MODIFY, &g_nid);
}
static void trayRemove() { if (g_nid.hWnd) Shell_NotifyIconW(NIM_DELETE, &g_nid); }

// ---------------------------------------------------------------- drawing helpers
static int knobX(int v) { return GROOVE_X0 + (int)std::lround(v * (double)(GROOVE_X1 - GROOVE_X0 + 1 - KNOB_W) / 100.0); }
static int knobCell(int v) { return v == 0 ? 0 : std::max(1, std::min(10, (v + 5) / 10)); }
static int maxIndicatorValue() {
    double dB = 20 * std::log10(std::max(1e-6, std::pow(10.0, (0.4 * g_val[0] - 20) / 20.0) * g_autoGain));
    return std::clamp((int)std::lround((dB + 20) / 0.4), 0, 100);
}

// --- pixel look: blit the original sprite sheet (integer zoom)
static void paintPixel(HDC dc) {
    HDC m = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, W, H); HBITMAP old = (HBITMAP)SelectObject(m, bmp);
    BitBlt(m, 0, 0, W, H, g_skinDC, 0, 0, SRCCOPY);
    for (int i = 0; i < NSL; i++) {
        int y = GROOVE_Y0 + i * GROOVE_DY, x = knobX(g_val[i]);
        BitBlt(m, x, y, KNOB_W, KNOB_H, g_skinDC, KNOB_SRC_X0 + knobCell(g_val[i]) * KNOB_SRC_DX, (g_drag == i) ? KNOB_ACT_SRC_Y : KNOB_SRC_Y, SRCCOPY);
    }
    if (g_power && g_autoGain < 0.999) BitBlt(m, knobX(maxIndicatorValue()), GROOVE_Y0 + 1, MAX_W, MAX_H, g_skinDC, MAX_SRC_X, MAX_SRC_Y, SRCCOPY);
    // background shows Power/Boost with the LED lit (= ON); the strip sprite is the LED-off look
    if (!g_power) BitBlt(m, BTN[0].x0, PRESSED_SRC_Y - PRESSED_DY, BTN[0].x1 - BTN[0].x0 + 1, PRESSED_H, g_skinDC, BTN[0].x0, PRESSED_SRC_Y, SRCCOPY);
    if (!g_boost) BitBlt(m, BTN[1].x0, PRESSED_SRC_Y - PRESSED_DY, BTN[1].x1 - BTN[1].x0 + 1, PRESSED_H, g_skinDC, BTN[1].x0, PRESSED_SRC_Y, SRCCOPY);
    int z = std::max(1, g_zoom10 / 10);
    if (z == 1) BitBlt(dc, 0, 0, W, H, m, 0, 0, SRCCOPY);
    else { SetStretchBltMode(dc, COLORONCOLOR); StretchBlt(dc, 0, 0, W * z, H * z, m, 0, 0, W, H, SRCCOPY); }
    SelectObject(m, old); DeleteObject(bmp); DeleteDC(m);
}

// --- vector look: same layout and colours, crisp at any scale
struct VG {
    HDC dc; double s;
    int X(double v) const { return (int)std::lround(v * s); }
    void fill(double x0, double y0, double x1, double y1, COLORREF c) { RECT r = {X(x0), X(y0), X(x1), X(y1)}; HBRUSH b = CreateSolidBrush(c); FillRect(dc, &r, b); DeleteObject(b); }
    void box(double x0, double y0, double x1, double y1, COLORREF face, COLORREF tl, COLORREF br) {   // bevelled rectangle, 1 logical px borders
        fill(x0, y0, x1, y1, face);
        fill(x0, y0, x1, y0 + 1, tl); fill(x0, y0, x0 + 1, y1, tl);
        fill(x0, y1 - 1, x1, y1, br); fill(x1 - 1, y0, x1, y1, br);
    }
    HFONT font(double px, bool bold, const wchar_t* face = L"Segoe UI") {
        return CreateFontW(-X(px), 0, 0, 0, bold ? FW_BOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
    }
    void text(double x, double y, const wchar_t* t, COLORREF c, HFONT f, UINT align = TA_LEFT | TA_TOP) {
        HFONT old = (HFONT)SelectObject(dc, f); SetBkMode(dc, TRANSPARENT); SetTextColor(dc, c); SetTextAlign(dc, align);
        TextOutW(dc, X(x), X(y), t, (int)wcslen(t)); SelectObject(dc, old);
    }
    void textCentered(double x0, double y0, double x1, double y1, const wchar_t* t, COLORREF c, HFONT f, COLORREF* shadow = nullptr) {
        RECT r = {X(x0), X(y0), X(x1), X(y1)}; HFONT old = (HFONT)SelectObject(dc, f); SetBkMode(dc, TRANSPARENT);
        if (shadow) { RECT r2 = r; OffsetRect(&r2, std::max(1, X(0.5)), std::max(1, X(0.5))); SetTextColor(dc, *shadow); DrawTextW(dc, t, -1, &r2, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); }
        SetTextColor(dc, c); DrawTextW(dc, t, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX); SelectObject(dc, old);
    }
};
static void paintVector(HDC dc) {
    double s = curScale(); VG g{dc, s};
    HFONT fLabel = g.font(8.5, false), fTitle = g.font(9.5, true), fKnob = g.font(8, true), fBtn = g.font(8.5, false), fSmall = g.font(6.5, true);
    // background + frame
    g.fill(0, 0, W, H, C_BG);
    g.fill(0, 0, W, 1, C_BLACK); g.fill(0, 0, 1, H, C_BLACK); g.fill(1, 1, W - 1, 2, C_FRAME); g.fill(1, 1, 2, H - 1, C_FRAME);
    g.fill(W - 1, 0, W, H, C_BLACK); g.fill(0, H - 1, W, H, C_BLACK); g.fill(W - 2, 1, W - 1, H - 1, C_FRAME2); g.fill(1, H - 2, W - 1, H - 1, C_FRAME2);
    // title bar: gold double lines left/right of the centred title, minimize and close glyphs
    const double ty = 5.5;
    for (double yy : {ty - 1.5, ty + 1.0}) { g.fill(8, yy, 78, yy + 1, C_GOLD); g.fill(197, yy, 250, yy + 1, C_GOLD); }
    g.fill(8, ty - 0.5, 78, ty + 1.0, C_GROOVE); g.fill(197, ty - 0.5, 250, ty + 1.0, C_GROOVE);
    COLORREF sh = C_BLACK; g.textCentered(78, 0, 197, 12, L"Enhancer Reloaded", RGB(255, 255, 255), fTitle, &sh);
    g.fill(RC_MIN.left + 2, 8, RC_MIN.right - 2, 10, C_TITLEBTN);
    { HPEN p = CreatePen(PS_SOLID, std::max(1, g.X(1.0)), C_TITLEBTN); HPEN op = (HPEN)SelectObject(dc, p);
      MoveToEx(dc, g.X(RC_CLOSE.left + 2), g.X(3), nullptr); LineTo(dc, g.X(RC_CLOSE.right - 2), g.X(10));
      MoveToEx(dc, g.X(RC_CLOSE.right - 2), g.X(3), nullptr); LineTo(dc, g.X(RC_CLOSE.left + 2), g.X(10));
      SelectObject(dc, op); DeleteObject(p); }
    // sliders
    for (int i = 0; i < NSL; i++) {
        double y = GROOVE_Y0 + i * GROOVE_DY;
        g.text(GROOVE_X0, LABEL_Y0 + i * GROOVE_DY - 1.5, SL_LABELS[i], C_GREEN, fLabel);
        g.box(GROOVE_X0, y, GROOVE_X1 + 1, y + GROOVE_H, C_GROOVE, C_BLACK, C_GRAY);
        int kx = knobX(g_val[i]); bool act = (g_drag == i);
        g.box(kx, y, kx + KNOB_W, y + KNOB_H, C_GROOVE, C_GRAY, C_BLACK);
        wchar_t t[8]; int cell = knobCell(g_val[i]);
        if (cell == 0) wcscpy_s(t, L"off"); else swprintf_s(t, L"%d", cell);
        g.textCentered(kx, y, kx + KNOB_W, y + KNOB_H, t, act ? C_CYAN : C_GREEN, cell == 0 ? fSmall : fKnob);
        if (i == 0 && g_power && g_autoGain < 0.999) {
            int mx = knobX(maxIndicatorValue());
            g.fill(mx, y + 1, mx + MAX_W, y + 1 + MAX_H, C_GROOVE);
            g.textCentered(mx, y, mx + MAX_W, y + GROOVE_H, L"max", C_RED, fSmall);
        }
    }
    // buttons
    for (int b = 0; b < 5; b++) {
        g.box(BTN[b].x0, BTN_Y0, BTN[b].x1 + 1, BTN_Y1 + 1, C_BTN, C_BTN_HI, C_BTN_LO);
        g.fill(BTN[b].x1, BTN_Y0 + 1, BTN[b].x1 + 1, BTN_Y1 + 1, C_BLACK); g.fill(BTN[b].x0 + 1, BTN_Y1, BTN[b].x1 + 1, BTN_Y1 + 1, C_BLACK);
        double tx0 = BTN[b].x0;
        if (b < 2) { bool on = b == 0 ? g_power : g_boost; g.box(BTN[b].x0 + 4, BTN_Y0 + 4, BTN[b].x0 + 10, BTN_Y0 + 9, on ? C_GREEN : C_LED_OFF, on ? RGB(160, 255, 160) : RGB(60, 100, 60), C_BLACK); tx0 += 8; }
        COLORREF emb = RGB(222, 222, 222); g.textCentered(tx0, BTN_Y0, BTN[b].x1 + 1, BTN_Y1 + 1, BTN_LABELS[b], RGB(0, 0, 8), fBtn, &emb);
    }
    for (HFONT f : {fLabel, fTitle, fKnob, fBtn, fSmall}) DeleteObject(f);
}
static void paint(HDC dc) {
    int w = winW(), h = winH();
    HDC m = CreateCompatibleDC(dc); HBITMAP bmp = CreateCompatibleBitmap(dc, w, h); HBITMAP old = (HBITMAP)SelectObject(m, bmp);
    if (g_vector) paintVector(m); else paintPixel(m);
    BitBlt(dc, 0, 0, w, h, m, 0, 0, SRCCOPY);
    SelectObject(m, old); DeleteObject(bmp); DeleteDC(m);
}
static void redraw() { InvalidateRect(g_wnd, nullptr, FALSE); }
static void applyLook(HWND h, int zoom10, bool vec) {
    g_zoom10 = zoom10; g_vector = vec; setUserPref(L"UIScale10", zoom10); setUserPref(L"UIVector", vec ? 1 : 0);
    SetWindowPos(h, nullptr, 0, 0, winW(), winH(), SWP_NOMOVE | SWP_NOZORDER); redraw();
}

// ---------------------------------------------------------------- interaction (logical coordinates)
static POINT logical(int x, int y) { double s = curScale(); return {(int)std::floor(x / s), (int)std::floor(y / s)}; }
static int sliderAt(int x, int y) {
    if (x < GROOVE_X0 - 2 || x > GROOVE_X1 + 2) return -1;
    for (int i = 0; i < NSL; i++) { int y0 = GROOVE_Y0 + i * GROOVE_DY; if (y >= y0 - 1 && y <= y0 + GROOVE_H) return i; }
    return -1;
}
static void setSliderFromX(int i, int x) {
    double span = GROOVE_X1 - GROOVE_X0 + 1 - KNOB_W;
    int v = std::clamp((int)std::lround((x - GROOVE_X0 - KNOB_W / 2) * 100.0 / span), 0, 100);
    if (v != g_val[i]) { g_val[i] = v; regWrite(SL_NAMES[i], v); redraw(); }
}
static int buttonAt(int x, int y) { if (y < BTN_Y0 || y > BTN_Y1) return -1; for (int i = 0; i < 5; i++) if (x >= BTN[i].x0 && x <= BTN[i].x1) return i; return -1; }

// ---------------------------------------------------------------- INI (next to the exe): user presets + last state
static std::wstring g_ini; static std::vector<Preset> g_userPresets; static std::wstring g_lastPreset;
static std::wstring valuesToString(const int* v) { std::wstring s; for (int i = 0; i < NSL; i++) { if (i) s += L','; s += std::to_wstring(v[i]); } return s; }
static bool stringToValues(const std::wstring& s, int* v) {
    int n = 0; size_t p = 0;
    while (n < NSL && p <= s.size()) { size_t e = s.find(L',', p); if (e == std::wstring::npos) e = s.size(); v[n++] = std::clamp(_wtoi(s.substr(p, e - p).c_str()), 0, 100); p = e + 1; }
    return n == NSL;
}
static std::wstring iniGet(const wchar_t* sec, const wchar_t* key, const wchar_t* def = L"") { wchar_t buf[1024]; GetPrivateProfileStringW(sec, key, def, buf, 1024, g_ini.c_str()); return buf; }
static void iniSet(const wchar_t* sec, const wchar_t* key, const wchar_t* val) { WritePrivateProfileStringW(sec, key, val, g_ini.c_str()); }
static void saveState() {
    iniSet(L"State", L"Values", valuesToString(g_val).c_str());
    iniSet(L"State", L"Boost", g_boost ? L"1" : L"0");
    iniSet(L"State", L"LastPreset", g_lastPreset.c_str());
}
static void loadUserPresets() {
    g_userPresets.clear();
    wchar_t keys[8192]; DWORD n = GetPrivateProfileStringW(L"Presets", nullptr, L"", keys, 8192, g_ini.c_str());
    for (wchar_t* k = keys; *k && k < keys + n; k += wcslen(k) + 1) {
        Preset p; p.name = k; if (stringToValues(iniGet(L"Presets", k), p.v)) g_userPresets.push_back(p);
    }
}
static bool loadState() {   // returns true if a saved state existed
    std::wstring v = iniGet(L"State", L"Values"); int vals[NSL];
    if (v.empty() || !stringToValues(v, vals)) return false;
    for (int i = 0; i < NSL; i++) g_val[i] = vals[i];
    g_boost = iniGet(L"State", L"Boost", L"0") == L"1";
    g_lastPreset = iniGet(L"State", L"LastPreset");
    return true;
}
static void pushAll() { for (int i = 0; i < NSL; i++) regWrite(SL_NAMES[i], g_val[i]); regWrite(L"Boost", g_boost); regWrite(L"Power", g_power); }
static void applyPreset(const Preset& p) { for (int i = 0; i < NSL; i++) { g_val[i] = p.v[i]; regWrite(SL_NAMES[i], p.v[i]); } g_lastPreset = p.name; saveState(); redraw(); }

// ---------------------------------------------------------------- start with Windows (HKCU Run, starts in the tray)
static const wchar_t* RUN_KEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static bool autostartEnabled() { HKEY k; wchar_t buf[MAX_PATH * 2]; DWORD sz = sizeof buf; bool on = false;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_READ, &k) == ERROR_SUCCESS) { on = RegQueryValueExW(k, L"EnhancerAPO", nullptr, nullptr, (BYTE*)buf, &sz) == ERROR_SUCCESS; RegCloseKey(k); } return on; }
static void setAutostart(bool on) {
    HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (on) { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); std::wstring cmd = L"\"" + std::wstring(exe) + L"\" --tray"; RegSetValueExW(k, L"EnhancerAPO", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t))); }
    else RegDeleteValueW(k, L"EnhancerAPO");
    RegCloseKey(k);
}

// ---------------------------------------------------------------- tiny modal text input (preset name)
static std::wstring g_inputResult; static bool g_inputOk = false;
static LRESULT CALLBACK inputProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_COMMAND:
        if (LOWORD(wp) == IDOK) { wchar_t buf[128]; GetWindowTextW(GetDlgItem(h, 100), buf, 128); g_inputResult = buf; g_inputOk = !g_inputResult.empty(); DestroyWindow(h); }
        else if (LOWORD(wp) == IDCANCEL) DestroyWindow(h);
        return 0;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}
static bool inputBox(HWND parent, const wchar_t* title, const wchar_t* prompt, const std::wstring& initial) {
    static bool reg = false; if (!reg) { WNDCLASSW wc{}; wc.lpfnWndProc = inputProc; wc.hInstance = g_inst; wc.lpszClassName = L"EnhInput"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1); RegisterClassW(&wc); reg = true; }
    g_inputOk = false; double s = g_dpi / 96.0; auto X = [&](int v) { return (int)std::lround(v * s); };
    HWND d = CreateWindowExW(WS_EX_DLGMODALFRAME | WS_EX_TOPMOST, L"EnhInput", title, WS_POPUP | WS_CAPTION | WS_SYSMENU, CW_USEDEFAULT, CW_USEDEFAULT, X(320), X(130), parent, nullptr, g_inst, nullptr);
    HFONT f = CreateFontW(-X(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HWND c;
    c = CreateWindowW(L"STATIC", prompt, WS_CHILD | WS_VISIBLE, X(10), X(8), X(290), X(18), d, nullptr, g_inst, nullptr); SendMessage(c, WM_SETFONT, (WPARAM)f, 0);
    c = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", initial.c_str(), WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, X(10), X(30), X(290), X(22), d, (HMENU)100, g_inst, nullptr); SendMessage(c, WM_SETFONT, (WPARAM)f, 0); SendMessage(c, EM_SETSEL, 0, -1); SetFocus(c);
    c = CreateWindowW(L"BUTTON", L"OK", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON, X(140), X(62), X(75), X(24), d, (HMENU)IDOK, g_inst, nullptr); SendMessage(c, WM_SETFONT, (WPARAM)f, 0);
    c = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP, X(225), X(62), X(75), X(24), d, (HMENU)IDCANCEL, g_inst, nullptr); SendMessage(c, WM_SETFONT, (WPARAM)f, 0);
    RECT pr; GetWindowRect(parent, &pr); SetWindowPos(d, nullptr, pr.left + 20, pr.top + 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    EnableWindow(parent, FALSE); ShowWindow(d, SW_SHOW);
    MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { if (!IsDialogMessage(d, &msg)) { TranslateMessage(&msg); DispatchMessage(&msg); } }
    EnableWindow(parent, TRUE); SetForegroundWindow(parent); DeleteObject(f);
    return g_inputOk;
}

static void showPresetsMenu() {
    HMENU m = CreatePopupMenu();
    for (size_t i = 0; i < g_presets.size(); i++) AppendMenuW(m, MF_STRING | (g_presets[i].name == g_lastPreset ? MF_CHECKED : 0), 100 + i, g_presets[i].name.c_str());
    if (!g_userPresets.empty()) {
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        for (size_t i = 0; i < g_userPresets.size(); i++) AppendMenuW(m, MF_STRING | (g_userPresets[i].name == g_lastPreset ? MF_CHECKED : 0), 300 + i, g_userPresets[i].name.c_str());
    }
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, 98, L"Save current as preset...");
    if (!g_userPresets.empty()) {
        HMENU del = CreatePopupMenu();
        for (size_t i = 0; i < g_userPresets.size(); i++) AppendMenuW(del, MF_STRING, 500 + i, g_userPresets[i].name.c_str());
        AppendMenuW(m, MF_POPUP, (UINT_PTR)del, L"Delete preset");
    }
    AppendMenuW(m, MF_STRING, 99, L"Reset effects (0 dB, all off)");
    POINT pt; GetCursorPos(&pt);
    int id = TrackPopupMenu(m, TPM_RETURNCMD | TPM_LEFTALIGN | TPM_TOPALIGN, pt.x, pt.y, 0, g_wnd, nullptr);
    DestroyMenu(m);
    if (id == 99) { Preset z{L"", {50, 0, 50, 0, 50, 100, 0, 50, 0, 50}}; applyPreset(z); }
    else if (id == 98) {
        if (inputBox(g_wnd, L"Save preset", L"Preset name:", g_lastPreset)) {
            iniSet(L"Presets", g_inputResult.c_str(), valuesToString(g_val).c_str());
            g_lastPreset = g_inputResult; saveState(); loadUserPresets(); redraw();
        }
    }
    else if (id >= 500 && id - 500 < (int)g_userPresets.size()) {
        const std::wstring& n = g_userPresets[id - 500].name;
        if (MessageBoxW(g_wnd, (L"Delete preset \"" + n + L"\"?").c_str(), L"Enhancer", MB_YESNO | MB_ICONQUESTION) == IDYES) {
            WritePrivateProfileStringW(L"Presets", n.c_str(), nullptr, g_ini.c_str());
            if (g_lastPreset == n) { g_lastPreset.clear(); saveState(); }
            loadUserPresets(); redraw();
        }
    }
    else if (id >= 300 && id - 300 < (int)g_userPresets.size()) applyPreset(g_userPresets[id - 300]);
    else if (id >= 100 && id - 100 < (int)g_presets.size()) applyPreset(g_presets[id - 100]);
}
// Closing the program switches the effect off in Windows and exits (the panel running == effect on).
static void exitApp(HWND h) { saveState(); g_power = false; regWrite(L"Power", 0); DestroyWindow(h); }

// ---------------------------------------------------------------- progress window while an elevated helper runs
// Shows a small window with a marquee bar and the last line of install.log (the helper appends a line per step).
struct Progress { HWND wnd = nullptr, text = nullptr, bar = nullptr; HFONT font = nullptr; };
static LRESULT CALLBACK progressProc(HWND h, UINT m, WPARAM wp, LPARAM lp) { if (m == WM_CLOSE) return 0; return DefWindowProc(h, m, wp, lp); }
static Progress progressOpen(HWND owner, const wchar_t* title, const wchar_t* headline) {
    static bool reg = false;
    if (!reg) { INITCOMMONCONTROLSEX ic{sizeof ic, ICC_PROGRESS_CLASS}; InitCommonControlsEx(&ic);
        WNDCLASSW wc{}; wc.lpfnWndProc = progressProc; wc.hInstance = g_inst; wc.lpszClassName = L"EnhProgress"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW); wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1); wc.hIcon = (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE); RegisterClassW(&wc); reg = true; }
    Progress p; double s = g_dpi / 96.0; auto X = [&](int v) { return (int)std::lround(v * s); };
    p.wnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_DLGMODALFRAME, L"EnhProgress", title, WS_POPUP | WS_CAPTION, 0, 0, X(420), X(150), owner, nullptr, g_inst, nullptr);
    p.font = CreateFontW(-X(12), 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HWND head = CreateWindowW(L"STATIC", headline, WS_CHILD | WS_VISIBLE, X(14), X(12), X(390), X(20), p.wnd, nullptr, g_inst, nullptr); SendMessage(head, WM_SETFONT, (WPARAM)p.font, 0);
    p.bar = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE, X(14), X(42), X(384), X(18), p.wnd, nullptr, g_inst, nullptr);
    SendMessage(p.bar, PBM_SETMARQUEE, TRUE, 30);
    p.text = CreateWindowW(L"STATIC", L"Please wait...", WS_CHILD | WS_VISIBLE | SS_PATHELLIPSIS, X(14), X(70), X(390), X(40), p.wnd, nullptr, g_inst, nullptr); SendMessage(p.text, WM_SETFONT, (WPARAM)p.font, 0);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0); RECT r; GetWindowRect(p.wnd, &r);
    SetWindowPos(p.wnd, HWND_TOPMOST, (wa.right + wa.left - (r.right - r.left)) / 2, (wa.bottom + wa.top - (r.bottom - r.top)) / 2, 0, 0, SWP_NOSIZE | SWP_SHOWWINDOW);
    return p;
}
static void progressClose(Progress& p) { if (p.wnd) DestroyWindow(p.wnd); if (p.font) DeleteObject(p.font); p = Progress(); }
static std::wstring lastLogLine() {
    std::wstring t = inst::readText(inst::logPath()); while (!t.empty() && (t.back() == L'\n' || t.back() == L'\r')) t.pop_back();
    size_t nl = t.find_last_of(L'\n'); return nl == std::wstring::npos ? t : t.substr(nl + 1);
}
// pumps messages and refreshes the status line until `work` (a process or thread handle) finishes
static DWORD waitWithProgress(HWND owner, HANDLE work, const wchar_t* title, const wchar_t* headline) {
    Progress p = progressOpen(owner, title, headline); std::wstring last;
    for (;;) {
        DWORD r = MsgWaitForMultipleObjects(1, &work, FALSE, 250, QS_ALLINPUT);
        MSG msg; while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessage(&msg); }
        std::wstring l = lastLogLine(); if (!l.empty() && l != last) { last = l; SetWindowTextW(p.text, l.c_str()); }
        if (r == WAIT_OBJECT_0) break;
    }
    DWORD code = 1; GetExitCodeProcess(work, &code) || GetExitCodeThread(work, &code);
    progressClose(p); return code;
}
// elevated helper (UAC) with the progress window; returns false when UAC was refused
static bool elevateWithProgress(HWND owner, const wchar_t* arg, const wchar_t* title, const wchar_t* headline, DWORD* exitCode) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei{}; sei.cbSize = sizeof sei; sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.hwnd = owner; sei.lpVerb = L"runas"; sei.lpFile = exe; sei.lpParameters = arg; sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;
    DWORD code = waitWithProgress(owner, sei.hProcess, title, headline); CloseHandle(sei.hProcess);
    if (exitCode) *exitCode = code; return true;
}
static void showPanel(HWND h) { ShowWindow(h, SW_SHOW); ShowWindow(h, SW_RESTORE); SetForegroundWindow(h); }

static LRESULT CALLBACK wndProc(HWND h, UINT m, WPARAM wp, LPARAM lp) {
    switch (m) {
    case WM_CREATE: SetTimer(h, 1, 100, nullptr); trayUpdate(h, true); return 0;
    case WM_TIMER: if (regReadAll()) { redraw(); trayUpdate(h, false); } return 0;
    case WM_CLOSE: exitApp(h); return 0;   // closing = effect off + exit (minimize hides to the tray instead)
    case WM_DPICHANGED: g_dpi = HIWORD(wp); SetWindowPos(h, nullptr, 0, 0, winW(), winH(), SWP_NOMOVE | SWP_NOZORDER); redraw(); return 0;
    case WM_EXITSIZEMOVE: {   // remember where the user put the panel
        RECT r; if (GetWindowRect(h, &r)) { iniSet(L"State", L"X", std::to_wstring(r.left).c_str()); iniSet(L"State", L"Y", std::to_wstring(r.top).c_str()); }
        return 0;
    }
    case WM_TRAY:
        if (lp == WM_LBUTTONUP || lp == WM_LBUTTONDBLCLK) showPanel(h);
        else if (lp == WM_RBUTTONUP) {
            HMENU mnu = CreatePopupMenu();
            AppendMenuW(mnu, MF_STRING, 1, L"Open panel"); AppendMenuW(mnu, MF_STRING | (g_power ? MF_CHECKED : 0), 2, L"Power (effect on)");
            AppendMenuW(mnu, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), 4, L"Start with Windows");
            AppendMenuW(mnu, MF_SEPARATOR, 0, nullptr); AppendMenuW(mnu, MF_STRING, 5, L"Uninstall Enhancer Reloaded...");
            AppendMenuW(mnu, MF_SEPARATOR, 0, nullptr); AppendMenuW(mnu, MF_STRING, 3, L"Exit (effect off)");
            POINT pt; GetCursorPos(&pt); SetForegroundWindow(h);
            int id = TrackPopupMenu(mnu, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, nullptr); DestroyMenu(mnu);
            if (id == 1) showPanel(h);
            else if (id == 2) { g_power = !g_power; regWrite(L"Power", g_power); redraw(); trayUpdate(h, false); }
            else if (id == 3) exitApp(h);
            else if (id == 4) setAutostart(!autostartEnabled());
            else if (id == 5) {
                if (MessageBoxW(h, L"Remove Enhancer Reloaded from Windows?\n\nThe audio component will be unregistered from every output, the original settings restored, the installed files deleted and the program will close. This program file itself stays where it is (delete it if you want).", L"Uninstall Enhancer Reloaded", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES) {
                    DWORD code = 1;
                    if (!elevateWithProgress(h, L"--uninstall --from-panel", L"Enhancer Reloaded - uninstall", L"Removing Enhancer Reloaded...", &code)) MessageBoxW(h, L"Uninstall was cancelled (administrator approval is required).", L"Enhancer Reloaded", MB_ICONWARNING);
                    else if (code != 0) { wchar_t m[96]; swprintf_s(m, L"Uninstall failed (code %lu).", code); MessageBoxW(h, m, L"Enhancer Reloaded", MB_ICONERROR); }
                    else { MessageBoxW(h, L"Enhancer Reloaded was removed. The sound is back to the original.", L"Enhancer Reloaded", MB_ICONINFORMATION); DestroyWindow(h); }
                }
            }
        }
        return 0;
    case WM_PAINT: { PAINTSTRUCT ps; HDC dc = BeginPaint(h, &ps); paint(dc); EndPaint(h, &ps); return 0; }
    case WM_ERASEBKGND: return 1;
    case WM_NCHITTEST: {
        POINT sp = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(h, &sp); POINT pt = logical(sp.x, sp.y);
        if (pt.y < 12 && !PtInRect(&RC_MIN, pt) && !PtInRect(&RC_CLOSE, pt)) return HTCAPTION;
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = logical(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)); int x = pt.x, y = pt.y;
        if (PtInRect(&RC_CLOSE, pt)) { PostMessage(h, WM_CLOSE, 0, 0); return 0; }
        if (PtInRect(&RC_MIN, pt)) { ShowWindow(h, SW_HIDE); return 0; }   // to the tray, effect keeps running
        int s = sliderAt(x, y);
        if (s >= 0) { g_drag = s; SetCapture(h); setSliderFromX(s, x); redraw(); return 0; }
        int b = buttonAt(x, y);
        if (b == 0) { g_power = !g_power; regWrite(L"Power", g_power); redraw(); trayUpdate(h, false); }
        else if (b == 1) { g_boost = !g_boost; regWrite(L"Boost", g_boost); saveState(); redraw(); }
        else if (b == 2) showPresetsMenu();
        else if (b == 3) ShellExecuteW(h, L"open", L"C:\\Program Files (x86)\\Winamp\\Plugins\\Enhancer\\017\\enhancer.htm", nullptr, nullptr, SW_SHOW);
        else if (b == 4) MessageBoxW(h,
            L"Enhancer Reloaded 1.0\n\n"
            L"System-wide Windows port (Audio Processing Object) of the Winamp plugin \"Enhancer 0.17\" by Adrian Iosca (2001).\n"
            L"The algorithm was recovered by reverse engineering and validated sample-exact against the original DLL.\n\n"
            L"Author: josiaslg\nGitHub: https://github.com/josiaslg/Enhancer-Reloaded\nE-mail: josiaslg@bsd.com.br\n\n"
            L"License: BSD 2-Clause. The original skin bitmap belongs to the Enhancer 0.17 author.\n"
            L"Right-click the panel for size, look and tray options.",
            L"About Enhancer", MB_OK | MB_ICONINFORMATION);
        return 0;
    }
    case WM_MOUSEMOVE: if (g_drag >= 0) setSliderFromX(g_drag, logical(GET_X_LPARAM(lp), GET_Y_LPARAM(lp)).x); return 0;
    case WM_LBUTTONUP: if (g_drag >= 0) { g_drag = -1; ReleaseCapture(); g_lastPreset.clear(); saveState(); redraw(); } return 0;
    case WM_MOUSEWHEEL: {
        POINT sp = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)}; ScreenToClient(h, &sp); POINT pt = logical(sp.x, sp.y);
        int s = sliderAt(pt.x, pt.y); if (s < 0) return 0;
        int v = std::clamp(g_val[s] + (GET_WHEEL_DELTA_WPARAM(wp) > 0 ? 2 : -2), 0, 100);
        if (v != g_val[s]) { g_val[s] = v; regWrite(SL_NAMES[s], v); g_lastPreset.clear(); saveState(); redraw(); }
        return 0;
    }
    case WM_RBUTTONUP: {
        HMENU mnu = CreatePopupMenu();
        AppendMenuW(mnu, MF_STRING, 1, L"Minimize"); AppendMenuW(mnu, MF_STRING, 2, L"Always on top");
        HMENU sz = CreatePopupMenu(); const int zooms[] = {10, 15, 20, 30, 40}; const wchar_t* zn[] = {L"1x", L"1.5x", L"2x", L"3x", L"4x"};
        for (int s = 0; s < 5; s++) AppendMenuW(sz, MF_STRING | (zooms[s] == g_zoom10 ? MF_CHECKED : 0), 11 + s, zn[s]);
        AppendMenuW(mnu, MF_POPUP, (UINT_PTR)sz, L"Size");
        HMENU lk = CreatePopupMenu(); AppendMenuW(lk, MF_STRING | (g_vector ? MF_CHECKED : 0), 21, L"High definition (vector)"); AppendMenuW(lk, MF_STRING | (!g_vector ? MF_CHECKED : 0), 22, L"Original skin (pixels)");
        AppendMenuW(mnu, MF_POPUP, (UINT_PTR)lk, L"Look");
        AppendMenuW(mnu, MF_STRING | (autostartEnabled() ? MF_CHECKED : 0), 4, L"Start with Windows");
        AppendMenuW(mnu, MF_SEPARATOR, 0, nullptr); AppendMenuW(mnu, MF_STRING, 3, L"Exit (effect off)");
        if (GetWindowLongPtr(h, GWL_EXSTYLE) & WS_EX_TOPMOST) CheckMenuItem(mnu, 2, MF_CHECKED);
        POINT pt; GetCursorPos(&pt); int id = TrackPopupMenu(mnu, TPM_RETURNCMD, pt.x, pt.y, 0, h, nullptr); DestroyMenu(mnu);
        if (id == 1) ShowWindow(h, SW_MINIMIZE);
        else if (id == 2) { bool top = GetWindowLongPtr(h, GWL_EXSTYLE) & WS_EX_TOPMOST; SetWindowPos(h, top ? HWND_NOTOPMOST : HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE); }
        else if (id == 3) exitApp(h);
        else if (id == 4) setAutostart(!autostartEnabled());
        else if (id >= 11 && id <= 15) { const int zooms[] = {10, 15, 20, 30, 40}; applyLook(h, zooms[id - 11], g_vector); }
        else if (id == 21) applyLook(h, g_zoom10, true);
        else if (id == 22) applyLook(h, g_zoom10, false);
        return 0;
    }
    case WM_DESTROY: KillTimer(h, 1); trayRemove(); PostQuitMessage(0); return 0;
    }
    return DefWindowProc(h, m, wp, lp);
}

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR cmdLine, int) {
    g_inst = inst;
    { HDC dc = GetDC(nullptr); g_dpi = GetDeviceCaps(dc, LOGPIXELSX); ReleaseDC(nullptr, dc); }
    // elevated helper modes (launched by ourselves through UAC)
    if (cmdLine && wcsstr(cmdLine, L"--install")) return inst::runInstall(inst, IDR_APODLL);
    if (cmdLine && wcsstr(cmdLine, L"--uninstall-ui")) {   // launched by "Apps & Features" (already elevated) or by hand
        if (!inst::isAdmin()) { DWORD code = 1; inst::elevate(nullptr, L"--uninstall-ui", &code); return (int)code; }
        if (MessageBoxW(nullptr, L"Remove Enhancer Reloaded from Windows?\n\nThe audio effect will be unregistered from every output and the original settings restored.", L"Uninstall Enhancer Reloaded", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) != IDYES) return 1;
        inst::closeRunningPanel();
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID) -> DWORD { return (DWORD)inst::runUninstall(); }, nullptr, 0, nullptr);
        DWORD rc = t ? waitWithProgress(nullptr, t, L"Enhancer Reloaded - uninstall", L"Removing Enhancer Reloaded...") : 1; if (t) CloseHandle(t);
        MessageBoxW(nullptr, rc == 0 ? L"Enhancer Reloaded was removed. The sound is back to the original." : L"Uninstall failed.", L"Enhancer Reloaded", rc == 0 ? MB_ICONINFORMATION : MB_ICONERROR);
        return (int)rc;
    }
    if (cmdLine && wcsstr(cmdLine, L"--uninstall")) { if (!wcsstr(cmdLine, L"--from-panel")) inst::closeRunningPanel(); return inst::runUninstall(); }
    bool startInTray = cmdLine && wcsstr(cmdLine, L"--tray") != nullptr;
    HANDLE single = CreateMutexW(nullptr, TRUE, L"Local\\EnhancerSkin.single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {   // second instance: bring the running panel back (or explain the wait)
        HWND other = FindWindowW(L"EnhancerSkinWnd", nullptr);
        if (other) { ShowWindow(other, SW_SHOW); SetForegroundWindow(other); }
        else MessageBoxW(nullptr, L"Enhancer Reloaded is already starting (or installing its audio component).\nPlease wait a few seconds; the panel will appear by itself.", L"Enhancer Reloaded", MB_ICONINFORMATION);
        return 0;
    }
    g_skin = (HBITMAP)LoadImageW(inst, MAKEINTRESOURCEW(IDB_SKIN), IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (!g_skin) { MessageBoxW(nullptr, L"Skin bitmap missing in resources", L"Enhancer", MB_ICONERROR); return 1; }
    // settings file: next to the exe when running "portable"; in %APPDATA%\EnhancerReloaded when installed in Program Files
    if (inst::runningFromInstallDir()) { std::wstring ad = inst::appDataDir(); CreateDirectoryW(ad.c_str(), nullptr); g_ini = ad + L"\\EnhancerReloaded.ini"; }
    else { std::wstring p = inst::thisExePath(); size_t dot = p.find_last_of(L'.'); g_ini = p.substr(0, dot) + L".ini"; }
    for (int i = 0; i < NSL; i++) g_val[i] = 50; g_val[5] = 100; g_val[1] = g_val[3] = g_val[6] = g_val[8] = 0;
    // first run: install the audio component (one UAC prompt), then continue as the normal panel
    bool installed = inst::isInstalled();
    bool exeOutdated = installed && !inst::runningFromInstallDir() && inst::fileExists(inst::installExePath()) && !inst::installedExeMatches();
    if (!installed || !inst::installedDllMatches(inst, IDR_APODLL) || exeOutdated) {
        const wchar_t* msg = installed
            ? L"This is a different version of Enhancer Reloaded than the one installed in Program Files.\n\nUpdate the installed copy now? (administrator rights required, a second of silence while the audio service restarts)"
            : L"Enhancer Reloaded needs to install its audio component into Windows (one time, administrator rights required).\n\nIt will be installed on every active audio output and can be removed later from the tray icon menu (Uninstall) or from Windows Apps & Features.\n\nInstall now?";
        if (MessageBoxW(nullptr, msg, L"Enhancer Reloaded - setup", MB_YESNO | MB_ICONQUESTION) != IDYES) { if (!installed) return 0; }
        else {
        inst::closeRunningPanel();   // an open (older) panel would keep the installed exe locked
        DWORD code = 1;
        if (!elevateWithProgress(nullptr, L"--install", L"Enhancer Reloaded - setup", installed ? L"Updating Enhancer Reloaded..." : L"Installing Enhancer Reloaded...", &code)) { MessageBoxW(nullptr, L"Installation was cancelled (administrator approval is required).", L"Enhancer Reloaded", MB_ICONWARNING); if (!installed) return 0; }
        else if (code != 0) { wchar_t m[160]; swprintf_s(m, L"Installation failed (code %lu). See C:\\Program Files\\EnhancerReloaded\\install.log", code); MessageBoxW(nullptr, m, L"Enhancer Reloaded", MB_ICONERROR); if (!installed) return 0; }
        else {
            Sleep(1500);   // give the audio stack a moment to come back
            MessageBoxW(nullptr, installed ? L"The audio component was updated." : L"Enhancer Reloaded was installed. The panel will open now.\nYou will find it in the Start Menu and, while running, as the purple hat in the tray.", L"Enhancer Reloaded - setup", MB_ICONINFORMATION);
        }
        }
    }
    // the program lives in Program Files together with its audio component: when started from anywhere else,
    // hand over to the installed copy (installing it first if an older setup only had the DLL there)
    if (!inst::runningFromInstallDir()) {
        if (!inst::fileExists(inst::installExePath()) && inst::isInstalled()) {
            if (MessageBoxW(nullptr, L"Install the Enhancer Reloaded program into Program Files (next to its audio component)?", L"Enhancer Reloaded - setup", MB_YESNO | MB_ICONQUESTION) == IDYES) {
                DWORD code = 1; if (elevateWithProgress(nullptr, L"--install", L"Enhancer Reloaded - setup", L"Installing Enhancer Reloaded...", &code) && code == 0) Sleep(1500);
            }
        }
        if (inst::fileExists(inst::installExePath())) {
            CloseHandle(single);
            ShellExecuteW(nullptr, L"open", inst::installExePath().c_str(), startInTray ? L"--tray" : nullptr, inst::INSTALL_DIR, SW_SHOW);
            return 0;
        }
    }
    g_keyOk = regKeyExists(); regReadAll();
    if (!g_keyOk) {   // the settings key is gone (an uninstall crossed our start?): offer a repair, which is simply a reinstall
        if (MessageBoxW(nullptr, L"The Enhancer Reloaded settings key is missing from Windows, so the installation is incomplete.\n\nRepair it now? (administrator rights required)", L"Enhancer Reloaded - repair", MB_YESNO | MB_ICONWARNING) == IDYES) {
            DWORD code = 1;
            if (elevateWithProgress(nullptr, L"--install", L"Enhancer Reloaded - setup", L"Repairing Enhancer Reloaded...", &code) && code == 0) { Sleep(1500); g_keyOk = regKeyExists(); regReadAll(); }
        }
        if (!g_keyOk) { MessageBoxW(nullptr, L"Enhancer Reloaded cannot run without its settings key. Run the program again to install.", L"Enhancer Reloaded", MB_ICONERROR); return 0; }
    }
    loadPresets(); loadUserPresets();
    // the panel running means the effect is on: restore the last state from the INI (or keep the registry values) and power on
    loadState();
    g_power = true; pushAll(); saveState();
    { HDC dc = GetDC(nullptr); g_dpi = GetDeviceCaps(dc, LOGPIXELSX); int sh = GetDeviceCaps(dc, VERTRES); ReleaseDC(nullptr, dc);
      int z = userPref(L"UIScale10", 0); g_zoom10 = (z >= 10 && z <= 40) ? z : ((sh >= 2000) ? 15 : 10);   // vector mode already scales with DPI
      g_vector = userPref(L"UIVector", 1) != 0; }
    WNDCLASSW wc{}; wc.lpfnWndProc = wndProc; wc.hInstance = inst; wc.lpszClassName = L"EnhancerSkinWnd"; wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(inst, MAKEINTRESOURCEW(IDI_MAIN), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    RegisterClassW(&wc);
    // default position: bottom-right corner of the work area, just above the taskbar (or the last saved position)
    int px, py;
    {
        RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
        int margin = (int)std::lround(8 * g_dpi / 96.0);
        px = wa.right - winW() - margin; py = wa.bottom - winH() - margin;
        std::wstring sx = iniGet(L"State", L"X"), sy = iniGet(L"State", L"Y");
        if (!sx.empty() && !sy.empty()) {
            int ix = _wtoi(sx.c_str()), iy = _wtoi(sy.c_str());
            RECT r = {ix, iy, ix + winW(), iy + winH()};
            if (MonitorFromRect(&r, MONITOR_DEFAULTTONULL)) { px = ix; py = iy; }   // only if still on a screen
        }
    }
    g_wnd = CreateWindowExW(WS_EX_APPWINDOW, wc.lpszClassName, L"Enhancer Reloaded", WS_POPUP | WS_SYSMENU | WS_MINIMIZEBOX, px, py, winW(), winH(), nullptr, nullptr, inst, nullptr);
    HDC dc = GetDC(g_wnd); g_skinDC = CreateCompatibleDC(dc); SelectObject(g_skinDC, g_skin); ReleaseDC(g_wnd, dc);
    if (!startInTray) ShowWindow(g_wnd, SW_SHOW);
    MSG msg; while (GetMessage(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessage(&msg); }
    CloseHandle(single);
    return 0;
}
