/*
 * StarField for Windows: a native .scr screensaver.
 *
 * Supports the standard screensaver arguments:
 *   /s            run full screen (one window per monitor)
 *   /p <HWND>     draw a live preview inside the Screen Saver Settings dialog
 *   /c[:<HWND>]   show the settings dialog (also the default with no args)
 *
 * Rendering uses Direct2D. The C core decides what to draw; this file only
 * turns the draw list into antialiased circles and triangular tails.
 */
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <mmsystem.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <vector>

#include "../core/starfield.h"
#include "resource.h"

namespace {

const wchar_t kRegistryKey[] = L"Software\\StarField";
const wchar_t kWindowClass[] = L"StarFieldScreenSaver";

/* ------------------------------------------------------------------------ */
/* Settings (HKCU\Software\StarField, values stored as DWORD x 1000)          */
/* ------------------------------------------------------------------------ */

double ReadValue(HKEY key, const wchar_t *name, double fallback) {
    DWORD value = 0, size = sizeof value, type = 0;
    if (key && RegQueryValueExW(key, name, nullptr, &type,
                                reinterpret_cast<BYTE *>(&value),
                                &size) == ERROR_SUCCESS &&
        type == REG_DWORD)
        return value / 1000.0;
    return fallback;
}

void WriteValue(HKEY key, const wchar_t *name, double v) {
    DWORD value = static_cast<DWORD>(std::lround(v * 1000.0));
    RegSetValueExW(key, name, 0, REG_DWORD, reinterpret_cast<BYTE *>(&value),
                   sizeof value);
}

sf_settings LoadSettings() {
    sf_settings s;
    sf_settings_defaults(&s);
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, KEY_READ, &key) ==
        ERROR_SUCCESS) {
        s.speed = ReadValue(key, L"Speed", s.speed);
        s.density = ReadValue(key, L"Density", s.density);
        s.tail_length = ReadValue(key, L"TailLength", s.tail_length);
        s.variation = ReadValue(key, L"Variation", s.variation);
        s.phase_shift = ReadValue(key, L"PhaseShift", s.phase_shift) != 0;
        s.motion = ReadValue(key, L"Motion", s.motion) != 0;
        RegCloseKey(key);
    }
    sf_settings_sanitize(&s);
    return s;
}

void SaveSettings(const sf_settings &s) {
    HKEY key = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegistryKey, 0, nullptr, 0,
                        KEY_WRITE, nullptr, &key, nullptr) != ERROR_SUCCESS)
        return;
    WriteValue(key, L"Speed", s.speed);
    WriteValue(key, L"Density", s.density);
    WriteValue(key, L"TailLength", s.tail_length);
    WriteValue(key, L"Variation", s.variation);
    WriteValue(key, L"PhaseShift", s.phase_shift ? 1 : 0);
    WriteValue(key, L"Motion", s.motion ? 1 : 0);
    RegCloseKey(key);
}

/* ------------------------------------------------------------------------ */
/* Config dialog                                                              */
/* ------------------------------------------------------------------------ */

struct Slider {
    int id, label;
    const wchar_t *name;
    double min, step;
    int steps;
};
const Slider kSpeed = {IDC_SPEED, IDC_SPEED_LABEL, L"Speed", SF_SPEED_MIN, 0.025, 79};
const Slider kDensity = {IDC_DENSITY, IDC_DENSITY_LABEL, L"Density", SF_DENSITY_MIN, 0.25, 39};
const Slider kVariation = {IDC_VARIATION, IDC_VARIATION_LABEL, L"Star variation", 0, 0.05, 20};
const Slider kTail = {IDC_TAIL, IDC_TAIL_LABEL, L"Tail length", 0, 0.25, 12};

double SliderValue(HWND dlg, const Slider &s) {
    LRESULT pos = SendDlgItemMessageW(dlg, s.id, TBM_GETPOS, 0, 0);
    return s.min + s.step * static_cast<double>(pos);
}

void UpdateLabel(HWND dlg, const Slider &s) {
    wchar_t text[64];
    double v = SliderValue(dlg, s);
    if (s.id == IDC_DENSITY || s.id == IDC_VARIATION)
        swprintf(text, 64, L"%ls: %d%%", s.name, static_cast<int>(std::lround(v * 100)));
    else if (s.id == IDC_SPEED)
        swprintf(text, 64, L"%ls: %g\u00D7", s.name, std::round(v * 1000) / 1000);
    else
        swprintf(text, 64, L"%ls: %.2f\u00D7", s.name, v);
    SetDlgItemTextW(dlg, s.label, text);
}

void InitSlider(HWND dlg, const Slider &s, double value) {
    SendDlgItemMessageW(dlg, s.id, TBM_SETRANGE, FALSE, MAKELPARAM(0, s.steps));
    LPARAM pos = static_cast<LPARAM>(std::lround((value - s.min) / s.step));
    SendDlgItemMessageW(dlg, s.id, TBM_SETPOS, TRUE, pos);
    UpdateLabel(dlg, s);
}

void LoadDialog(HWND dlg, const sf_settings &s) {
    CheckDlgButton(dlg, IDC_MOTION, s.motion ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(dlg, IDC_PHASESHIFT, s.phase_shift ? BST_CHECKED : BST_UNCHECKED);
    InitSlider(dlg, kSpeed, s.speed);
    InitSlider(dlg, kDensity, s.density);
    InitSlider(dlg, kVariation, s.variation);
    InitSlider(dlg, kTail, s.tail_length);
}

INT_PTR CALLBACK ConfigProc(HWND dlg, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_INITDIALOG:
        LoadDialog(dlg, LoadSettings());
        return TRUE;
    case WM_HSCROLL: {
        HWND ctl = reinterpret_cast<HWND>(lp);
        for (const Slider *s : {&kSpeed, &kDensity, &kVariation, &kTail})
            if (ctl == GetDlgItem(dlg, s->id)) UpdateLabel(dlg, *s);
        return TRUE;
    }
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_DEFAULTS: {
            sf_settings d;
            sf_settings_defaults(&d);
            LoadDialog(dlg, d);
            return TRUE;
        }
        case IDOK: {
            sf_settings s;
            s.motion = IsDlgButtonChecked(dlg, IDC_MOTION) == BST_CHECKED;
            s.phase_shift = IsDlgButtonChecked(dlg, IDC_PHASESHIFT) == BST_CHECKED;
            s.speed = SliderValue(dlg, kSpeed);
            s.density = SliderValue(dlg, kDensity);
            s.variation = SliderValue(dlg, kVariation);
            s.tail_length = SliderValue(dlg, kTail);
            sf_settings_sanitize(&s);
            SaveSettings(s);
            EndDialog(dlg, IDOK);
            return TRUE;
        }
        case IDCANCEL:
            EndDialog(dlg, IDCANCEL);
            return TRUE;
        }
        break;
    case WM_CLOSE:
        EndDialog(dlg, IDCANCEL);
        return TRUE;
    }
    return FALSE;
}

/* ------------------------------------------------------------------------ */
/* Rendering                                                                  */
/* ------------------------------------------------------------------------ */

enum class Mode { Saver, Preview };

struct Screen {
    HWND hwnd = nullptr;
    sf_field field{};
    ID2D1HwndRenderTarget *target = nullptr;
    ID2D1SolidColorBrush *brush = nullptr;
    UINT width = 0, height = 0; /* physical pixels */
    double dpr = 1;
};

struct App {
    Mode mode = Mode::Saver;
    sf_settings settings{};
    ID2D1Factory *factory = nullptr;
    /* One unit triangle (0,0), (1,-1), (1,1), reused for every tail via a
       per-star transform: tail length/direction/width are just the matrix. */
    ID2D1PathGeometry *tail = nullptr;
    std::vector<Screen *> screens;
    POINT first_mouse{};
    bool have_mouse = false;
    bool closing = false;
} app;

template <typename T> void Release(T *&p) {
    if (p) {
        p->Release();
        p = nullptr;
    }
}

typedef UINT(WINAPI *GetDpiForWindowFn)(HWND);

double WindowDpr(HWND hwnd) {
    static GetDpiForWindowFn fn = reinterpret_cast<GetDpiForWindowFn>(
        reinterpret_cast<void *>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
    UINT dpi = fn ? fn(hwnd) : 0;
    if (!dpi) {
        HDC dc = GetDC(hwnd);
        dpi = static_cast<UINT>(GetDeviceCaps(dc, LOGPIXELSX));
        ReleaseDC(hwnd, dc);
    }
    return dpi ? dpi / 96.0 : 1.0;
}

void DiscardTarget(Screen &s) {
    Release(s.brush);
    Release(s.target);
}

bool EnsureTarget(Screen &s) {
    if (s.target) return true;
    D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties();
    props.dpiX = props.dpiY = 96.0f; /* work in physical pixels */
    D2D1_HWND_RENDER_TARGET_PROPERTIES hwnd_props = D2D1::HwndRenderTargetProperties(
        s.hwnd, D2D1::SizeU(s.width, s.height), D2D1_PRESENT_OPTIONS_IMMEDIATELY);
    if (FAILED(app.factory->CreateHwndRenderTarget(props, hwnd_props, &s.target)))
        return false;
    s.target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    if (FAILED(s.target->CreateSolidColorBrush(D2D1::ColorF(1, 1, 1, 1), &s.brush))) {
        DiscardTarget(s);
        return false;
    }
    return true;
}

void Resize(Screen &s) {
    RECT rc;
    GetClientRect(s.hwnd, &rc);
    s.width = static_cast<UINT>(std::max<LONG>(1, rc.right - rc.left));
    s.height = static_cast<UINT>(std::max<LONG>(1, rc.bottom - rc.top));
    s.dpr = WindowDpr(s.hwnd);
    if (s.target) s.target->Resize(D2D1::SizeU(s.width, s.height));
    /* Logical (CSS-equivalent) size drives star count, sizes and tail caps. */
    sf_field_resize(&s.field, s.width / s.dpr, s.height / s.dpr, s.dpr);
}

void Render(Screen &s, double seconds) {
    if (!EnsureTarget(s)) return;
    size_t n = sf_field_step(&s.field, &app.settings, seconds);
    ID2D1HwndRenderTarget *t = s.target;
    t->BeginDraw();
    const D2D1::Matrix3x2F view =
        D2D1::Matrix3x2F::Scale(static_cast<float>(s.dpr), static_cast<float>(s.dpr));
    t->SetTransform(view);
    t->Clear(D2D1::ColorF(SF_BACKGROUND_R / 255.0f, SF_BACKGROUND_G / 255.0f,
                          SF_BACKGROUND_B / 255.0f, 1.0f));
    for (size_t i = 0; i < n; i++) {
        const sf_draw_star &d = s.field.draw[i];
        float r = d.color.r / 255.0f, g = d.color.g / 255.0f, b = d.color.b / 255.0f;
        if (d.has_tail) {
            sf_tail_frame f;
            if (app.tail && sf_tail_triangle(&d, &f)) {
                s.brush->SetColor(D2D1::ColorF(r, g, b, static_cast<float>(d.tail_opacity)));
                D2D1::Matrix3x2F place(
                    static_cast<float>(f.along_x), static_cast<float>(f.along_y),
                    static_cast<float>(f.across_x), static_cast<float>(f.across_y),
                    static_cast<float>(f.tip_x), static_cast<float>(f.tip_y));
                t->SetTransform(place * view);
                t->FillGeometry(app.tail, s.brush);
                t->SetTransform(view);
            }
        }
        s.brush->SetColor(D2D1::ColorF(r, g, b, static_cast<float>(d.head.opacity)));
        float radius = static_cast<float>(d.head.radius);
        t->FillEllipse(D2D1::Ellipse(D2D1::Point2F(static_cast<float>(d.head.x),
                                                   static_cast<float>(d.head.y)),
                                     radius, radius),
                       s.brush);
    }
    if (t->EndDraw() == static_cast<HRESULT>(D2DERR_RECREATE_TARGET)) DiscardTarget(s);
}

void Quit() {
    if (app.closing) return;
    app.closing = true;
    PostQuitMessage(0);
}

LRESULT CALLBACK ScreenProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    Screen *s = reinterpret_cast<Screen *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    const bool saver = app.mode == Mode::Saver;
    switch (msg) {
    case WM_NCCREATE: {
        auto *cs = reinterpret_cast<CREATESTRUCTW *>(lp);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(cs->lpCreateParams));
        break;
    }
    case WM_SIZE:
    case WM_DPICHANGED:
        if (s) {
            Resize(*s);
            if (!app.settings.motion) InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        if (s) Render(*s, 0);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        if (saver) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (saver) {
            POINT p;
            GetCursorPos(&p);
            if (!app.have_mouse) {
                app.first_mouse = p;
                app.have_mouse = true;
            } else if (std::abs(p.x - app.first_mouse.x) > 4 ||
                       std::abs(p.y - app.first_mouse.y) > 4) {
                Quit();
            }
        }
        break;
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_MBUTTONDOWN:
    case WM_XBUTTONDOWN:
    case WM_MOUSEWHEEL:
        if (saver) Quit();
        break;
    case WM_ACTIVATEAPP:
        if (saver && !wp) Quit();
        break;
    case WM_SYSCOMMAND:
        if (saver && ((wp & 0xFFF0) == SC_SCREENSAVE || (wp & 0xFFF0) == SC_MONITORPOWER))
            return 0;
        break;
    case WM_CLOSE:
        Quit();
        return 0;
    case WM_DESTROY:
        /* In preview mode the settings dialog destroys our child window. */
        Quit();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

Screen *CreateScreen(HINSTANCE inst, DWORD style, DWORD ex_style, RECT rc,
                     HWND parent, uint64_t seed) {
    auto *s = new Screen();
    if (!sf_field_init(&s->field, seed)) {
        delete s;
        return nullptr;
    }
    s->hwnd = CreateWindowExW(ex_style, kWindowClass, L"StarField", style, rc.left,
                              rc.top, rc.right - rc.left, rc.bottom - rc.top, parent,
                              nullptr, inst, s);
    if (!s->hwnd) {
        sf_field_free(&s->field);
        delete s;
        return nullptr;
    }
    Resize(*s);
    app.screens.push_back(s);
    return s;
}

BOOL CALLBACK AddMonitor(HMONITOR, HDC, LPRECT rc, LPARAM inst) {
    CreateScreen(reinterpret_cast<HINSTANCE>(inst), WS_POPUP | WS_VISIBLE,
                 WS_EX_TOPMOST, *rc, nullptr,
                 static_cast<uint64_t>(GetTickCount64()) ^
                     (static_cast<uint64_t>(app.screens.size()) << 32));
    return TRUE;
}

double NowMs() {
    static LARGE_INTEGER freq = [] {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        return f;
    }();
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return static_cast<double>(now.QuadPart) * 1000.0 / static_cast<double>(freq.QuadPart);
}

int Run(HINSTANCE inst, Mode mode, HWND preview_parent) {
    app.mode = mode;
    app.settings = LoadSettings();

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &app.factory)))
        return 1;
    if (SUCCEEDED(app.factory->CreatePathGeometry(&app.tail))) {
        ID2D1GeometrySink *sink = nullptr;
        if (SUCCEEDED(app.tail->Open(&sink))) {
            sink->BeginFigure(D2D1::Point2F(0, 0), D2D1_FIGURE_BEGIN_FILLED);
            const D2D1_POINT_2F base[2] = {D2D1::Point2F(1, -1), D2D1::Point2F(1, 1)};
            sink->AddLines(base, 2);
            sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            if (FAILED(sink->Close())) Release(app.tail);
            sink->Release();
        } else {
            Release(app.tail);
        }
    }

    WNDCLASSW wc = {};
    wc.lpfnWndProc = ScreenProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(IDI_STARFIELD));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc);

    if (mode == Mode::Preview) {
        RECT rc;
        GetClientRect(preview_parent, &rc);
        CreateScreen(inst, WS_CHILD | WS_VISIBLE, 0, rc, preview_parent,
                     static_cast<uint64_t>(GetTickCount64()));
    } else {
        EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(inst));
        if (!app.screens.empty()) SetForegroundWindow(app.screens.front()->hwnd);
    }
    if (app.screens.empty()) return 1;

    timeBeginPeriod(1);
    sf_clock clock;
    sf_clock_reset(&clock);
    MSG msg;
    for (;;) {
        if (!app.settings.motion) {
            /* Static dots: draw on WM_PAINT only, no animation loop. */
            BOOL got = GetMessageW(&msg, nullptr, 0, 0);
            if (got <= 0) break;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            continue;
        }
        bool quit = false;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                quit = true;
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (quit) break;
        /* Pace to the compositor's refresh, then cap simulation/draws at 60 Hz. */
        if (FAILED(DwmFlush())) Sleep(1);
        double seconds;
        if (sf_clock_tick(&clock, NowMs(), &seconds))
            for (Screen *s : app.screens) Render(*s, seconds);
    }
    timeEndPeriod(1);

    for (Screen *s : app.screens) {
        DiscardTarget(*s);
        if (IsWindow(s->hwnd)) {
            SetWindowLongPtrW(s->hwnd, GWLP_USERDATA, 0);
            DestroyWindow(s->hwnd);
        }
        sf_field_free(&s->field);
        delete s;
    }
    app.screens.clear();
    Release(app.tail);
    Release(app.factory);
    return 0;
}

/* Parse "/p 1234", "/p:1234", "-P1234" etc. Returns the HWND or null. */
HWND ParseHwnd(const char *rest) {
    while (*rest == ':' || *rest == ' ' || *rest == '\t') rest++;
    unsigned long long v = std::strtoull(rest, nullptr, 10);
    return reinterpret_cast<HWND>(static_cast<uintptr_t>(v));
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR cmd, int) {
    const char *p = cmd ? cmd : "";
    while (*p == ' ' || *p == '\t' || *p == '"') p++;
    char flag = 0;
    if (*p == '/' || *p == '-') flag = static_cast<char>(p[1] | 0x20);

    INITCOMMONCONTROLSEX icc = {sizeof icc, ICC_BAR_CLASSES};
    InitCommonControlsEx(&icc);

    switch (flag) {
    case 's':
        return Run(inst, Mode::Saver, nullptr);
    case 'p': {
        HWND parent = ParseHwnd(p + 2);
        if (!parent || !IsWindow(parent)) return 1;
        return Run(inst, Mode::Preview, parent);
    }
    case 'c': {
        HWND owner = ParseHwnd(p + 2);
        if (!owner || !IsWindow(owner)) owner = GetForegroundWindow();
        DialogBoxW(inst, MAKEINTRESOURCEW(DLG_SCRNSAVECONFIGURE), owner, ConfigProc);
        return 0;
    }
    default:
        DialogBoxW(inst, MAKEINTRESOURCEW(DLG_SCRNSAVECONFIGURE), nullptr, ConfigProc);
        return 0;
    }
}
