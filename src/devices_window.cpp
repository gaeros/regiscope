// Finestra "Periferiche": fotocamera, posizione, microfono, batteria e sensori.
#include "devices_window.h"
#include "devices.h"
#include "messages.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

enum : int { ID_PREVIEW = 401, ID_PHOTO, ID_POSITION, ID_RECORD, ID_SENSORS };
constexpr UINT_PTR kBatteryTimer = 7;

HWND g_wnd = nullptr;
HINSTANCE g_inst = nullptr;
PanelFonts g_fonts;
HWND g_buttons[5] = {};
const wchar_t *const kButtonText[5] = {L"Anteprima", L"Scatta", L"Aggiorna", L"Registra 2 s", L"Aggiorna"};

int S(int v) { return MulDiv(v, g_fonts.dpi, 96); }

struct Card {
    RECT rc{};
    const wchar_t *title = L"";
};

Card g_camera{{}, L"Fotocamera"}, g_position{{}, L"Posizione"}, g_mic{{}, L"Microfono"},
    g_battery{{}, L"Batteria"}, g_sensors{{}, L"Sensori"};

// ------------------------------------------------------------------ disegno

int LineH(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}

int TextAt(HDC dc, int x, int y, const std::wstring &s, HFONT f, COLORREF c, int maxRight = 0) {
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SIZE sz{};
    GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
    RECT r{x, y, maxRight ? maxRight : x + sz.cx + 2, y + sz.cy};
    DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
    SelectObject(dc, old);
    return sz.cx;
}

void FillBox(HDC dc, RECT r, COLORREF c) {
    SetDCBrushColor(dc, c);
    FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
}

void FrameBox(HDC dc, RECT r, COLORREF fill, COLORREF line, int radius) {
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, line);
    SetDCBrushColor(dc, fill);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
}

// Disegna la scheda e restituisce l'area interna (sotto il titolo).
RECT DrawCard(HDC dc, const Card &c) {
    FrameBox(dc, c.rc, theme::panel, theme::border, S(10));
    SetBkMode(dc, TRANSPARENT);
    TextAt(dc, c.rc.left + S(14), c.rc.top + S(12), c.title, g_fonts.bold, theme::text);
    RECT in{c.rc.left + S(14), c.rc.top + S(14) + LineH(dc, g_fonts.bold) + S(8), c.rc.right - S(14),
            c.rc.bottom - S(12)};
    return in;
}

// Una fila di bit (il più significativo a sinistra).
void DrawBits(HDC dc, int x, int y, uint64_t value, int nbits, int cell) {
    int gap = std::max(1, cell / 4);
    for (int i = 0; i < nbits; ++i) {
        int bit = nbits - 1 - i;
        int px = x + i * (cell + 1) + (i / 8) * gap;
        RECT r{px, y, px + cell, y + cell};
        FillBox(dc, r, theme::bitGrid);
        InflateRect(&r, -1, -1);
        FillBox(dc, r, ((value >> bit) & 1) ? theme::bitOne : theme::bitZero);
    }
}

int BitsWidth(int nbits, int cell) { return nbits * (cell + 1) + (nbits / 8) * std::max(1, cell / 4); }

void DrawImage(HDC dc, RECT box, const dev::Image &img) {
    if (img.w <= 0 || img.h <= 0) return;
    int bw = box.right - box.left, bh = box.bottom - box.top;
    double scale = std::min((double)bw / img.w, (double)bh / img.h);
    int w = std::max(1, (int)(img.w * scale)), h = std::max(1, (int)(img.h * scale));
    int x = box.left + (bw - w) / 2, y = box.top + (bh - h) / 2;
    // DIB a 24 bit: righe allineate a 4 byte, ordine dei colori B G R.
    int rowBytes = (img.w * 3 + 3) & ~3;
    std::vector<uint8_t> dib((size_t)rowBytes * img.h);
    for (int yy = 0; yy < img.h; ++yy)
        for (int xx = 0; xx < img.w; ++xx) {
            const uint8_t *p = &img.rgb[((size_t)yy * img.w + xx) * 3];
            uint8_t *q = &dib[(size_t)yy * rowBytes + xx * 3];
            q[0] = p[2];
            q[1] = p[1];
            q[2] = p[0];
        }
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader);
    bi.bmiHeader.biWidth = img.w;
    bi.bmiHeader.biHeight = -img.h; // dall'alto in basso
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 24;
    bi.bmiHeader.biCompression = BI_RGB;
    // Immagini piccole (es. 32x24 dello snippet): pixel netti; foto grandi: sfumate.
    SetStretchBltMode(dc, scale >= 2 ? COLORONCOLOR : HALFTONE);
    SetBrushOrgEx(dc, 0, 0, nullptr);
    StretchDIBits(dc, x, y, w, h, 0, 0, img.w, img.h, dib.data(), &bi, DIB_RGB_COLORS, SRCCOPY);
}

void PaintCamera(HDC dc, const dev::State &st) {
    RECT in = DrawCard(dc, g_camera);
    int lh = LineH(dc, g_fonts.smallFont);
    std::wstring sub = st.cameraName.empty() ? L"Webcam del PC" : st.cameraName;
    if (!st.image.source.empty()) sub += L" · " + st.image.source;
    TextAt(dc, in.left, in.top, sub, g_fonts.smallFont, theme::quiet, in.right);
    int y = in.top + lh + S(8);

    int footer = LineH(dc, g_fonts.ui) + lh * 4 + S(32);
    RECT box{in.left, y, in.right, std::max(y + S(60), (int)in.bottom - footer)};
    FrameBox(dc, box, RGB(0x2A, 0x2D, 0x30), RGB(0x2A, 0x2D, 0x30), S(6));
    RECT inner = box;
    InflateRect(&inner, -S(4), -S(4));
    if (st.image.w > 0) {
        DrawImage(dc, inner, st.image);
    } else {
        std::wstring hint = st.cameraMessage.empty()
                                ? L"Premi Anteprima o Scatta, oppure usa camera_scatta() in uno snippet."
                                : st.cameraMessage;
        HGDIOBJ old = SelectObject(dc, g_fonts.ui);
        SetTextColor(dc, RGB(0xE6, 0xE6, 0xE6));
        DrawTextW(dc, hint.c_str(), -1, &inner, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        SelectObject(dc, old);
    }
    y = box.bottom + S(10);
    if (st.image.w > 0 && !st.cameraMessage.empty()) {
        TextAt(dc, in.left, y, st.cameraMessage, g_fonts.smallFont, theme::accent, in.right);
        y += lh + S(4);
    }

    // Il pixel al centro, byte per byte: così si vede come è fatto un colore.
    if (st.image.w > 0) {
        const uint8_t *p = &st.image.rgb[((size_t)(st.image.h / 2) * st.image.w + st.image.w / 2) * 3];
        wchar_t buf[96];
        swprintf(buf, 96, L"Pixel al centro (%d, %d): 3 byte, uno per colore", st.image.w / 2, st.image.h / 2);
        TextAt(dc, in.left, y, buf, g_fonts.ui, theme::text, in.right);
        y += LineH(dc, g_fonts.ui) + S(6);
        const wchar_t *names[3] = {L"Rosso", L"Verde", L"Blu"};
        COLORREF swatch[3] = {RGB(p[0], 0, 0), RGB(0, p[1], 0), RGB(0, 0, p[2])};
        int cell = std::max(S(9), lh - S(3));
        for (int i = 0; i < 3; ++i) {
            int x = in.left;
            RECT sw{x, y + (lh - cell) / 2, x + cell, y + (lh - cell) / 2 + cell};
            FillBox(dc, sw, swatch[i]);
            x += cell + S(6);
            TextAt(dc, x, y, names[i], g_fonts.smallFont, theme::quiet);
            x += S(44);
            swprintf(buf, 96, L"%3d", p[i]);
            TextAt(dc, x, y, buf, g_fonts.mono, theme::text);
            x += S(34);
            DrawBits(dc, x, y + (lh - cell) / 2, p[i], 8, cell);
            y += lh + S(2);
        }
        RECT all{in.right - S(40), y - 3 * (lh + S(2)), in.right, y - S(2)};
        FrameBox(dc, all, RGB(p[0], p[1], p[2]), theme::border, S(4));
    }
}

void PaintPosition(HDC dc, const dev::State &st) {
    RECT in = DrawCard(dc, g_position);
    int lh = LineH(dc, g_fonts.ui);
    int y = in.top;
    const dev::Position &p = st.position;
    if (st.positionBusy) {
        TextAt(dc, in.left, y, L"Ricerca della posizione…", g_fonts.ui, theme::quiet, in.right);
        return;
    }
    if (!p.valid) {
        std::wstring msg = p.message.empty() ? L"Premi Aggiorna o usa posizione_leggi() in uno snippet." : p.message;
        RECT r{in.left, y, in.right, in.bottom};
        HGDIOBJ old = SelectObject(dc, g_fonts.ui);
        SetTextColor(dc, theme::quiet);
        DrawTextW(dc, msg.c_str(), -1, &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
        SelectObject(dc, old);
        return;
    }
    wchar_t buf[128];
    swprintf(buf, 128, L"Latitudine   %.6f°", p.lat);
    TextAt(dc, in.left, y, buf, g_fonts.mono, theme::text);
    y += lh + S(2);
    swprintf(buf, 128, L"Longitudine  %.6f°", p.lon);
    TextAt(dc, in.left, y, buf, g_fonts.mono, theme::text);
    y += lh + S(2);
    swprintf(buf, 128, L"Precisione   ± %.0f m", p.accuracy);
    TextAt(dc, in.left, y, buf, g_fonts.mono, theme::text);
    y += lh + S(8);
    // La latitudine è un double: 64 bit (segno, esponente, mantissa).
    uint64_t bits;
    memcpy(&bits, &p.lat, 8);
    TextAt(dc, in.left, y, L"La latitudine in memoria (double, 64 bit):", g_fonts.smallFont, theme::quiet, in.right);
    y += LineH(dc, g_fonts.smallFont) + S(3);
    int avail = in.right - in.left;
    int cell = std::max(3, std::min(S(9), (avail - S(16)) / 64 - 1));
    if (y + cell <= in.bottom) DrawBits(dc, in.left, y, bits, 64, cell);
}

void PaintMic(HDC dc, const dev::State &st) {
    RECT in = DrawCard(dc, g_mic);
    int lh = LineH(dc, g_fonts.smallFont);
    const dev::Audio &a = st.audio;
    std::wstring info;
    if (st.audioBusy) info = L"Registrazione in corso… parla vicino al microfono";
    else if (!a.valid) info = a.message.empty() ? L"Premi Registra o usa microfono_registra() in uno snippet." : a.message;
    else {
        int peak = 0;
        for (int16_t s : a.samples) peak = std::max(peak, std::abs((int)s));
        wchar_t buf[128];
        swprintf(buf, 128, L"%.1f s · %d campioni al secondo · picco %d (%d%%)", (double)a.samples.size() / a.rate,
                 a.rate, peak, peak * 100 / 32767);
        info = buf;
    }
    TextAt(dc, in.left, in.top, info, g_fonts.smallFont, st.audioBusy ? theme::stepDark : theme::quiet, in.right);
    RECT box{in.left, in.top + lh + S(6), in.right, in.bottom};
    if (box.bottom - box.top < S(16)) return;
    FrameBox(dc, box, RGB(0xF7, 0xF8, 0xFA), theme::border, S(4));
    int mid = (box.top + box.bottom) / 2;
    SelectObject(dc, GetStockObject(DC_PEN));
    SetDCPenColor(dc, theme::bitGrid);
    MoveToEx(dc, box.left + 1, mid, nullptr);
    LineTo(dc, box.right - 1, mid);
    if (!a.valid || a.samples.empty()) return;
    // Forma d'onda: per ogni colonna di pixel il minimo e il massimo dei campioni.
    SetDCPenColor(dc, theme::accent);
    int w = box.right - box.left - 2, half = (box.bottom - box.top) / 2 - 2;
    size_t n = a.samples.size();
    for (int x = 0; x < w; ++x) {
        size_t from = n * x / w, to = std::max(from + 1, n * (x + 1) / w);
        int lo = 0, hi = 0;
        for (size_t i = from; i < to && i < n; ++i) {
            lo = std::min(lo, (int)a.samples[i]);
            hi = std::max(hi, (int)a.samples[i]);
        }
        MoveToEx(dc, box.left + 1 + x, mid - hi * half / 32768, nullptr);
        LineTo(dc, box.left + 1 + x, mid - lo * half / 32768 + 1);
    }
}

void PaintBattery(HDC dc, const dev::State &st) {
    RECT in = DrawCard(dc, g_battery);
    const dev::Battery &b = st.battery;
    int lh = LineH(dc, g_fonts.ui);
    if (!b.valid) {
        TextAt(dc, in.left, in.top, L"Lettura in corso…", g_fonts.ui, theme::quiet, in.right);
        return;
    }
    if (!b.hasBattery) {
        TextAt(dc, in.left, in.top, L"Nessuna batteria: il PC è collegato alla corrente.", g_fonts.ui, theme::quiet,
               in.right);
        return;
    }
    wchar_t buf[96];
    swprintf(buf, 96, L"%d%% · %ls", b.percent, b.charging ? L"in carica" : b.onAc ? L"collegato" : L"a batteria");
    TextAt(dc, in.left, in.top, buf, g_fonts.ui, theme::text, in.right);
    RECT bar{in.left, in.top + lh + S(6), in.right, in.top + lh + S(16)};
    FillBox(dc, bar, theme::barTrack);
    RECT fill = bar;
    fill.right = bar.left + (bar.right - bar.left) * std::max(0, b.percent) / 100;
    FillBox(dc, fill, b.percent <= 15 ? RGB(0xD0, 0x4A, 0x3A) : theme::step);
}

void PaintSensors(HDC dc, const dev::State &st) {
    RECT in = DrawCard(dc, g_sensors);
    const dev::Sensors &s = st.sensors;
    int lh = LineH(dc, g_fonts.ui);
    if (!s.valid) {
        TextAt(dc, in.left, in.top, L"Lettura in corso…", g_fonts.ui, theme::quiet, in.right);
        return;
    }
    wchar_t buf[128];
    if (s.hasLight) swprintf(buf, 128, L"Luce          %.0f lux", s.lux);
    else swprintf(buf, 128, L"Luce          sensore non presente");
    TextAt(dc, in.left, in.top, buf, g_fonts.mono, s.hasLight ? theme::text : theme::quiet, in.right);
    if (s.hasAccel) swprintf(buf, 128, L"Accelerometro x %+.2f  y %+.2f  z %+.2f g", s.ax, s.ay, s.az);
    else swprintf(buf, 128, L"Accelerometro sensore non presente");
    TextAt(dc, in.left, in.top + lh + S(4), buf, g_fonts.mono, s.hasAccel ? theme::text : theme::quiet, in.right);
}

void Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    FillBox(mem, rc, theme::window);
    SetBkMode(mem, TRANSPARENT);
    dev::State st = dev::GetState();
    PaintCamera(mem, st);
    PaintPosition(mem, st);
    PaintMic(mem, st);
    PaintBattery(mem, st);
    PaintSensors(mem, st);
    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ------------------------------------------------------------------ pulsanti e layout

void DrawButton(const DRAWITEMSTRUCT *di) {
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    bool pressed = (di->itemState & ODS_SELECTED) != 0;
    bool disabledState = (di->itemState & ODS_DISABLED) != 0;
    bool green = di->CtlID == ID_PREVIEW && dev::GetState().previewOn;
    FillBox(dc, rc, theme::panel);
    COLORREF fill = green ? (pressed ? theme::stepDark : theme::step) : (pressed ? theme::buttonDown : theme::panel);
    FrameBox(dc, rc, fill, green ? theme::stepDark : theme::border, S(8));
    wchar_t label[64];
    GetWindowTextW(di->hwndItem, label, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabledState ? theme::disabled : green ? RGB(0xFF, 0xFF, 0xFF) : theme::text);
    HGDIOBJ old = SelectObject(dc, green ? g_fonts.bold : g_fonts.ui);
    DrawTextW(dc, label, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, old);
}

void Layout(int w, int h) {
    int m = S(10);
    int leftW = (w - 3 * m) * 56 / 100;
    g_camera.rc = RECT{m, m, m + leftW, h - m};
    int x = m + leftW + m, rw = w - x - m;
    int total = h - 2 * m - 3 * m;
    int hPos = total * 30 / 100, hMic = total * 30 / 100, hBat = total * 16 / 100;
    int hSen = total - hPos - hMic - hBat;
    int y = m;
    g_position.rc = RECT{x, y, x + rw, y + hPos};
    y += hPos + m;
    g_mic.rc = RECT{x, y, x + rw, y + hMic};
    y += hMic + m;
    g_battery.rc = RECT{x, y, x + rw, y + hBat};
    y += hBat + m;
    g_sensors.rc = RECT{x, y, x + rw, y + hSen};

    int bh = S(26), top = S(8);
    auto place = [&](HWND b, const Card &c, int right, int width) {
        MoveWindow(b, right - width, c.rc.top + top, width, bh, TRUE);
        return right - width - S(6);
    };
    int r = g_camera.rc.right - S(10);
    r = place(g_buttons[1], g_camera, r, S(76));
    place(g_buttons[0], g_camera, r, S(92));
    place(g_buttons[2], g_position, g_position.rc.right - S(10), S(80));
    place(g_buttons[3], g_mic, g_mic.rc.right - S(10), S(100));
    place(g_buttons[4], g_sensors, g_sensors.rc.right - S(10), S(80));
}

void UpdateButtons() {
    dev::State st = dev::GetState();
    SetWindowTextW(g_buttons[0], st.previewOn ? L"Ferma anteprima" : L"Anteprima");
    EnableWindow(g_buttons[2], !st.positionBusy);
    EnableWindow(g_buttons[3], !st.audioBusy);
    InvalidateRect(g_buttons[0], nullptr, FALSE);
}

LRESULT CALLBACK DevicesProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        for (int i = 0; i < 5; ++i) {
            g_buttons[i] = CreateWindowExW(0, L"BUTTON", kButtonText[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0,
                                           10, 10, hwnd, (HMENU)(INT_PTR)(ID_PREVIEW + i), g_inst, nullptr);
            SendMessageW(g_buttons[i], WM_SETFONT, (WPARAM)g_fonts.ui, TRUE);
        }
        dev::SetWindow(hwnd);
        return 0;
    case WM_SIZE:
        Layout(LOWORD(lp), HIWORD(lp));
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        auto *mm = reinterpret_cast<MINMAXINFO *>(lp);
        mm->ptMinTrackSize = {S(820), S(560)};
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        Paint(hwnd);
        return 0;
    case WM_DRAWITEM:
        DrawButton(reinterpret_cast<DRAWITEMSTRUCT *>(lp));
        return TRUE;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_PREVIEW: dev::TogglePreview(); break;
        case ID_PHOTO: dev::TakePhotoAsync(); break;
        case ID_POSITION: dev::UpdatePositionAsync(); break;
        case ID_RECORD: dev::RecordAsync(2000); break;
        case ID_SENSORS: dev::UpdateBatteryAndSensorsAsync(); break;
        }
        UpdateButtons();
        return 0;
    case WM_APP_DEVICES_CHANGED: {
        // Più notifiche in coda (anteprima a 30 fotogrammi al secondo): basta un ridisegno.
        MSG dup;
        while (PeekMessageW(&dup, hwnd, WM_APP_DEVICES_CHANGED, WM_APP_DEVICES_CHANGED, PM_REMOVE)) {
        }
        UpdateButtons();
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    case WM_TIMER:
        if (wp == kBatteryTimer) dev::UpdateBatteryAndSensorsAsync();
        return 0;
    case WM_SHOWWINDOW:
        if (wp) {
            dev::UpdateBatteryAndSensorsAsync();
            SetTimer(hwnd, kBatteryTimer, 5000, nullptr);
        } else {
            KillTimer(hwnd, kBatteryTimer);
        }
        return 0;
    case WM_CLOSE:
        // Chiudere la finestra la nasconde soltanto, e spegne l'anteprima.
        if (dev::GetState().previewOn) dev::TogglePreview();
        ShowWindow(hwnd, SW_HIDE);
        return 0;
    case WM_DESTROY:
        dev::SetWindow(nullptr);
        g_wnd = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void ShowDevicesWindow(HWND owner, HINSTANCE inst, const PanelFonts &fonts, HICON icon, bool activate) {
    if (!g_wnd) {
        g_inst = inst;
        g_fonts = fonts;
        static bool registered = false;
        if (!registered) {
            WNDCLASSW wc{};
            wc.lpfnWndProc = DevicesProc;
            wc.hInstance = inst;
            wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
            wc.hIcon = icon;
            wc.lpszClassName = L"RegiScopeDevices";
            RegisterClassW(&wc);
            registered = true;
        }
        RECT orc;
        GetWindowRect(owner, &orc);
        g_wnd = CreateWindowExW(0, L"RegiScopeDevices", L"RegiScope · Periferiche", WS_OVERLAPPEDWINDOW,
                                orc.left + S(60), orc.top + S(60), S(1000), S(660), owner, nullptr, inst, nullptr);
    }
    if (!g_wnd) return;
    if (!IsWindowVisible(g_wnd)) ShowWindow(g_wnd, activate ? SW_SHOWNORMAL : SW_SHOWNOACTIVATE);
    else if (IsIconic(g_wnd)) ShowWindow(g_wnd, SW_RESTORE);
    else if (activate) SetForegroundWindow(g_wnd);
}

bool DevicesWindowVisible() { return g_wnd && IsWindowVisible(g_wnd); }
