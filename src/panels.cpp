#include "panels.h"
#include "theme.h"

#include <algorithm>
#include <cstdio>
#include <vector>

ViewState g_view;

namespace {

PanelFonts g_fonts;
const wchar_t kRamClass[] = L"RegiScopeRamPanel";
const wchar_t kCpuClass[] = L"RegiScopeCpuPanel";

int S(int v) { return MulDiv(v, g_fonts.dpi, 96); }

std::wstring Hex64(uint64_t v) {
    wchar_t buf[24];
    swprintf(buf, 24, L"%016llX", (unsigned long long)v);
    return buf;
}

std::wstring FormatBytes(uint64_t b) {
    wchar_t buf[48];
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024;
    if (b >= (uint64_t)gb) swprintf(buf, 48, L"%.1f GB", b / gb);
    else if (b >= (uint64_t)mb) swprintf(buf, 48, L"%.0f MB", b / mb);
    else swprintf(buf, 48, L"%.0f KB", b / kb);
    std::wstring s = buf;
    std::replace(s.begin(), s.end(), L'.', L','); // formato italiano
    return s;
}

// Piccolo aiuto per disegnare con GDI.
struct Painter {
    HDC dc;

    void Fill(const RECT &r, COLORREF c) {
        SetDCBrushColor(dc, c);
        FillRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
    }
    void Frame(const RECT &r, COLORREF c) {
        SetDCBrushColor(dc, c);
        FrameRect(dc, &r, (HBRUSH)GetStockObject(DC_BRUSH));
    }
    void RoundBox(const RECT &r, COLORREF fill, COLORREF line, int radius) {
        SelectObject(dc, GetStockObject(DC_PEN));
        SelectObject(dc, GetStockObject(DC_BRUSH));
        SetDCPenColor(dc, line);
        SetDCBrushColor(dc, fill);
        RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    }
    int LineHeight(HFONT f) {
        SelectObject(dc, f);
        TEXTMETRICW tm{};
        GetTextMetricsW(dc, &tm);
        return tm.tmHeight;
    }
    int Width(const std::wstring &s, HFONT f) {
        SelectObject(dc, f);
        SIZE sz{};
        GetTextExtentPoint32W(dc, s.c_str(), (int)s.size(), &sz);
        return sz.cx;
    }
    // Scrive il testo (y = bordo superiore) e restituisce la larghezza.
    int Text(int x, int y, const std::wstring &s, HFONT f, COLORREF c) {
        SelectObject(dc, f);
        SetTextColor(dc, c);
        TextOutW(dc, x, y, s.c_str(), (int)s.size());
        return Width(s, f);
    }
    // Testo dentro un rettangolo, tagliato con "…" se non ci sta.
    void TextIn(RECT r, const std::wstring &s, HFONT f, COLORREF c, UINT align = DT_LEFT) {
        SelectObject(dc, f);
        SetTextColor(dc, c);
        DrawTextW(dc, s.c_str(), (int)s.size(), &r, align | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
    }
    void WrapText(RECT r, const std::wstring &s, HFONT f, COLORREF c) {
        SelectObject(dc, f);
        SetTextColor(dc, c);
        DrawTextW(dc, s.c_str(), (int)s.size(), &r, DT_LEFT | DT_WORDBREAK | DT_NOPREFIX);
    }
};

int BitGap(int cell) { return std::max(2, cell / 3); }
int BitRowWidth(int cell, int nbits) { return nbits * cell + (nbits / 8 - 1) * BitGap(cell); }

int FitCell(int avail, int nbits) {
    int cell = std::max(3, avail / nbits);
    while (cell > 3 && BitRowWidth(cell, nbits) > avail) --cell;
    return std::min(cell, S(14));
}

// Disegna nbits bit di 'value', il più significativo a sinistra, raggruppati per byte.
// I bit indicati in 'changed' sono colorati con l'accento.
void DrawBits(Painter &p, int x, int y, int cell, int h, uint64_t value, uint64_t changed, int nbits) {
    int gap = BitGap(cell);
    for (int i = 0; i < nbits; ++i) {
        int bit = nbits - 1 - i;
        int cx = x + i * cell + (i / 8) * gap;
        RECT r{cx, y, cx + cell - 1, y + h};
        bool one = (value >> bit) & 1;
        bool ch = (changed >> bit) & 1;
        p.Fill(r, ch ? (one ? theme::accent : theme::accentSoft) : (one ? theme::bitOne : theme::bitZero));
        if (!one) p.Frame(r, ch ? theme::accent : theme::bitGrid);
    }
}

uint64_t LoadLE(const uint8_t *b, size_t n) {
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i) v |= (uint64_t)b[i] << (8 * i);
    return v;
}

// ---------------------------------------------------------------- RAM

// Una riga di riepilogo dei banchi fisici: "16 GB · 8 banchi da 2,0 GB LPDDR4X · 4267 MT/s · Samsung".
std::wstring MemSummary() {
    const MemInfo &mem = g_view.mem;
    if (!g_view.memReady) return L"lettura dei banchi…";
    if (!mem.ok) return mem.error;
    if (mem.modules.empty()) return L"banchi non riportati dal sistema";

    struct Group { uint64_t capacity; std::wstring type; uint32_t speed; int count; };
    std::vector<Group> groups;
    uint64_t total = 0;
    bool sameMaker = true;
    for (const auto &m : mem.modules) {
        total += m.capacity;
        if (m.manufacturer != mem.modules[0].manufacturer) sameMaker = false;
        auto it = std::find_if(groups.begin(), groups.end(), [&](const Group &g) {
            return g.capacity == m.capacity && g.type == m.type && g.speed == m.speed;
        });
        if (it != groups.end()) ++it->count;
        else groups.push_back({m.capacity, m.type, m.speed, 1});
    }

    std::wstring s = FormatBytes(total) + L" · ";
    if (groups.size() == 1) {
        const Group &g = groups[0];
        s += std::to_wstring(g.count) + (g.count == 1 ? L" banco da " : L" banchi da ") + FormatBytes(g.capacity);
        if (!g.type.empty()) s += L" " + g.type;
        if (g.speed) s += L" · " + std::to_wstring(g.speed) + L" MT/s";
    } else {
        for (size_t i = 0; i < groups.size(); ++i) {
            const Group &g = groups[i];
            if (i) s += L" + ";
            s += std::to_wstring(g.count) + L" × " + FormatBytes(g.capacity);
            if (!g.type.empty()) s += L" " + g.type;
            if (g.speed) s += L" " + std::to_wstring(g.speed) + L" MT/s";
        }
    }
    if (sameMaker && !mem.modules[0].manufacturer.empty()) s += L" · " + mem.modules[0].manufacturer;
    int empty = mem.totalSlots - (int)mem.modules.size();
    if (empty > 0) s += L" · " + std::to_wstring(empty) + (empty == 1 ? L" slot libero" : L" slot liberi");
    return s;
}

void PaintUsage(Painter &p, int x, int &y, int right) {
    const auto &ms = g_view.memStatus;
    if (!ms.ullTotalPhys) return;
    uint64_t used = ms.ullTotalPhys - ms.ullAvailPhys;
    std::wstring s = L"RAM occupata: " + FormatBytes(used) + L" di " + FormatBytes(ms.ullTotalPhys) + L" (" +
                     std::to_wstring(ms.dwMemoryLoad) + L"%) · RegiScope: " + FormatBytes(g_view.appWorkingSet);
    p.Text(x, y, s, g_fonts.ui, theme::quiet);
    y += p.LineHeight(g_fonts.ui) + S(4);
    RECT track{x, y, right, y + S(8)};
    p.Fill(track, theme::barTrack);
    RECT fill = track;
    fill.right = x + (int)((right - x) * (double)used / (double)ms.ullTotalPhys);
    p.Fill(fill, RGB(0x9D, 0xBC, 0xF0));
    y += S(8);
}

// Byte precedente allo stesso indirizzo di stack, se presente nella fotografia precedente.
bool PrevStackByte(uint64_t addr, uint8_t &out) {
    const Snapshot &pv = g_view.prev;
    if (!pv.valid || addr < pv.stackBase || addr >= pv.stackBase + pv.stack.size()) return false;
    out = pv.stack[(size_t)(addr - pv.stackBase)];
    return true;
}

const HeapBlock *PrevHeapBlock(uint64_t addr) {
    if (!g_view.prev.valid) return nullptr;
    for (const auto &b : g_view.prev.heap)
        if (b.addr == addr && !b.freed) return &b;
    return nullptr;
}

// Righe dei dati dello snippet (stack e heap), a partire da y; nessun limite in basso:
// l'area ha la sua barra di scorrimento. Restituisce la y dopo l'ultima riga.
int PaintSnippetRows(Painter &p, int x, int y, int right, int clipTop, int clipBottom) {
    const Snapshot &s = g_view.cur;
    if (!s.valid) {
        RECT r{x, y, right, y + S(200)};
        p.WrapText(r, L"Premi Avanti (F10) per eseguire lo snippet passo passo: qui compaiono lo stack della "
                      L"funzione corrente e i blocchi malloc, bit per bit.",
                   g_fonts.ui, theme::quiet);
        return y + p.LineHeight(g_fonts.ui) * 3;
    }

    int labelW = p.Width(L"RSP+0x000 ", g_fonts.mono);
    int hexW = p.Width(L"0000000000000000", g_fonts.mono) + S(8);
    int cell = FitCell(right - x - labelW - hexW, 64);
    int rowH = std::max(cell + S(4), p.LineHeight(g_fonts.mono) + S(2));
    int bitsX = x + labelW;
    int bitsW = BitRowWidth(cell, 64);
    // Disegna solo le righe visibili; le altre contano solo per l'altezza.
    auto visible = [&](int yy) { return yy + rowH > clipTop && yy < clipBottom; };

    wchar_t buf[96];
    swprintf(buf, 96, L"Stack · da RSP = 0x%llX, 8 byte per riga", (unsigned long long)s.stackBase);
    p.Text(x, y, buf, g_fonts.smallFont, theme::quiet);
    y += p.LineHeight(g_fonts.smallFont) + S(2);

    for (size_t off = 0; off < s.stack.size(); off += 8) {
        if (visible(y)) {
            size_t n = std::min<size_t>(8, s.stack.size() - off);
            uint64_t v = LoadLE(&s.stack[off], n);
            uint64_t changed = 0;
            for (size_t j = 0; j < n; ++j) {
                uint8_t old;
                if (PrevStackByte(s.stackBase + off + j, old)) changed |= (uint64_t)(old ^ s.stack[off + j]) << (8 * j);
            }
            swprintf(buf, 96, L"RSP+0x%02llX", (unsigned long long)off);
            p.Text(x, y, buf, g_fonts.mono, theme::quiet);
            DrawBits(p, bitsX, y + (rowH - cell) / 2 - S(1), cell, cell, v, changed, 64);
            p.Text(bitsX + bitsW + S(8), y, Hex64(v), g_fonts.mono, theme::text);
        }
        y += rowH;
    }

    if (!s.heap.empty()) {
        y += S(6);
        p.Text(x, y, L"Heap · blocchi malloc", g_fonts.smallFont, theme::quiet);
        y += p.LineHeight(g_fonts.smallFont) + S(2);
        for (const auto &b : s.heap) {
            if (visible(y)) {
                swprintf(buf, 96, L"0x%llX · %llu byte%ls", (unsigned long long)b.addr, (unsigned long long)b.size,
                         b.freed ? L" · liberato" : L"");
                p.Text(x, y, buf, g_fonts.mono, b.freed ? theme::quiet : theme::text);
            }
            y += rowH;
            if (b.freed) continue;
            const HeapBlock *old = PrevHeapBlock(b.addr);
            for (size_t off = 0; off < b.bytes.size(); off += 8) {
                if (visible(y)) {
                    size_t n = std::min<size_t>(8, b.bytes.size() - off);
                    uint64_t v = LoadLE(&b.bytes[off], n);
                    uint64_t changed = 0;
                    if (old && off + n <= old->bytes.size()) changed = v ^ LoadLE(&old->bytes[off], n);
                    int nbits = (int)n * 8;
                    swprintf(buf, 96, L"+0x%02llX", (unsigned long long)off);
                    p.Text(x, y, buf, g_fonts.mono, theme::quiet);
                    int shift = BitRowWidth(cell, 64) - BitRowWidth(cell, nbits);
                    DrawBits(p, bitsX + shift, y + (rowH - cell) / 2 - S(1), cell, cell, v, changed, nbits);
                    p.Text(bitsX + bitsW + S(8), y, Hex64(v), g_fonts.mono, theme::text);
                }
                y += rowH;
            }
        }
    }
    return y + S(6);
}

// Intestazione del pannello RAM (titolo, riepilogo banchi, occupazione, titolo dei dati).
// Restituisce la y dove inizia l'area scorrevole dei dati dello snippet.
int PaintRamHeader(Painter &p, const RECT &rc, bool draw) {
    int pad = S(14);
    int x = rc.left + pad, y = rc.top + pad, right = rc.right - pad;
    int boldH = p.LineHeight(g_fonts.bold), uiH = p.LineHeight(g_fonts.ui);
    if (draw) {
        int w = p.Text(x, y, L"RAM", g_fonts.bold, theme::text);
        RECT t{x + w + S(10), y + boldH - uiH, right, y + boldH};
        p.TextIn(t, MemSummary(), g_fonts.ui, theme::quiet);
    }
    y += boldH + S(8);
    if (draw) PaintUsage(p, x, y, right);
    else y += uiH + S(4) + S(8);
    y += S(14);
    if (draw) p.Text(x, y, L"Dati dello snippet", g_fonts.bold, theme::text);
    y += boldH + S(4);
    return y;
}

const wchar_t kRamDataClass[] = L"RegiScopeRamData";

// Posiziona l'area scorrevole dentro il pannello RAM, sotto l'intestazione.
void LayoutRamData(HWND panel) {
    HWND data = FindWindowExW(panel, nullptr, kRamDataClass, nullptr);
    if (!data) return;
    RECT rc;
    GetClientRect(panel, &rc);
    HDC dc = GetDC(panel);
    Painter p{dc};
    int top = PaintRamHeader(p, rc, false);
    ReleaseDC(panel, dc);
    int left = rc.left + S(14), right = rc.right - S(4), bottom = rc.bottom - S(8);
    MoveWindow(data, left, top, std::max(0, right - left), std::max(0, bottom - top), TRUE);
}

void PaintRam(Painter &p, const RECT &rc) { PaintRamHeader(p, rc, true); }

void ScrollRamData(HWND hwnd, int pos) {
    SCROLLINFO si{sizeof(si), SIF_ALL};
    GetScrollInfo(hwnd, SB_VERT, &si);
    int maxPos = std::max(0, si.nMax - (int)si.nPage + 1);
    pos = std::clamp(pos, 0, maxPos);
    if (pos == si.nPos) return;
    si.fMask = SIF_POS;
    si.nPos = pos;
    SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
    InvalidateRect(hwnd, nullptr, FALSE);
}

LRESULT CALLBACK RamDataProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    static int wheelAccum = 0;
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_SIZE:
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        wheelAccum += GET_WHEEL_DELTA_WPARAM(wp);
        int notches = wheelAccum / WHEEL_DELTA;
        wheelAccum -= notches * WHEEL_DELTA;
        if (notches) ScrollRamData(hwnd, GetScrollPos(hwnd, SB_VERT) - notches * S(54));
        return 0;
    }
    case WM_VSCROLL: {
        SCROLLINFO si{sizeof(si), SIF_ALL};
        GetScrollInfo(hwnd, SB_VERT, &si);
        int pos = si.nPos, line = S(18);
        switch (LOWORD(wp)) {
        case SB_LINEUP: pos -= line; break;
        case SB_LINEDOWN: pos += line; break;
        case SB_PAGEUP: pos -= (int)si.nPage; break;
        case SB_PAGEDOWN: pos += (int)si.nPage; break;
        case SB_THUMBTRACK:
        case SB_THUMBPOSITION: pos = si.nTrackPos; break;
        case SB_TOP: pos = 0; break;
        case SB_BOTTOM: pos = si.nMax; break;
        }
        ScrollRamData(hwnd, pos);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, std::max<LONG>(1, rc.right), std::max<LONG>(1, rc.bottom));
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HGDIOBJ oldFont = SelectObject(mem, g_fonts.ui);
        SetBkMode(mem, TRANSPARENT);

        Painter p{mem};
        p.Fill(rc, theme::panel);
        int scroll = GetScrollPos(hwnd, SB_VERT);
        int end = PaintSnippetRows(p, 0, -scroll, rc.right - S(10), 0, rc.bottom);
        int contentH = end + scroll;

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldFont);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);

        // Aggiorna la barra (sempre visibile, disattivata se tutto ci sta: la larghezza non cambia).
        SCROLLINFO si{sizeof(si), SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL};
        si.nMin = 0;
        si.nMax = std::max(0, contentH - 1);
        si.nPage = (UINT)std::max<LONG>(1, rc.bottom);
        SetScrollInfo(hwnd, SB_VERT, &si, TRUE);
        if (GetScrollPos(hwnd, SB_VERT) != scroll) InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ---------------------------------------------------------------- CPU

struct RegDesc { const wchar_t *name; DWORD64 CONTEXT::*field; };
const RegDesc kRegs[] = {
    {L"RAX", &CONTEXT::Rax}, {L"RBX", &CONTEXT::Rbx}, {L"RCX", &CONTEXT::Rcx}, {L"RDX", &CONTEXT::Rdx},
    {L"RSI", &CONTEXT::Rsi}, {L"RDI", &CONTEXT::Rdi}, {L"RBP", &CONTEXT::Rbp}, {L"RSP", &CONTEXT::Rsp},
    {L"R8", &CONTEXT::R8},   {L"R9", &CONTEXT::R9},   {L"R10", &CONTEXT::R10}, {L"R11", &CONTEXT::R11},
    {L"R12", &CONTEXT::R12}, {L"R13", &CONTEXT::R13}, {L"R14", &CONTEXT::R14}, {L"R15", &CONTEXT::R15},
    {L"RIP", &CONTEXT::Rip},
};

struct FlagDesc { const wchar_t *name; int bit; };
const FlagDesc kFlags[] = {{L"CF", 0}, {L"PF", 2}, {L"AF", 4}, {L"ZF", 6}, {L"SF", 7}, {L"OF", 11}};

void PaintCpu(Painter &p, const RECT &rc) {
    int pad = S(14);
    int x = rc.left + pad, y = rc.top + pad, right = rc.right - pad, bottom = rc.bottom - pad;
    const CpuInfo &cpu = g_view.cpu;

    int w = p.Text(x, y, L"CPU", g_fonts.bold, theme::text);
    RECT brand{x + w + S(10), y + p.LineHeight(g_fonts.bold) - p.LineHeight(g_fonts.ui), right,
               y + p.LineHeight(g_fonts.bold)};
    p.TextIn(brand, cpu.brand.empty() ? L"modello sconosciuto" : cpu.brand, g_fonts.ui, theme::quiet);
    y += p.LineHeight(g_fonts.bold) + S(2);

    wchar_t buf[200];
    swprintf(buf, 200, L"%d core fisici, %d logici · L1d %u KB · L1i %u KB · L2 %u KB · L3 %u KB", cpu.cores,
             cpu.logical, cpu.l1d, cpu.l1i, cpu.l2, cpu.l3);
    p.TextIn(RECT{x, y, right, y + p.LineHeight(g_fonts.smallFont)}, buf, g_fonts.smallFont, theme::quiet);
    y += p.LineHeight(g_fonts.smallFont) + S(12);

    const Snapshot &s = g_view.cur;
    if (!s.valid) {
        RECT r{x, y, right, bottom};
        p.WrapText(r, L"Nessuna esecuzione in pausa. Avanti (F10) avvia lo snippet e si ferma alla prima "
                      L"istruzione di main; ogni pressione successiva esegue una sola istruzione.",
                   g_fonts.ui, theme::quiet);
        return;
    }

    // Istruzione corrente.
    RECT box{x, y, right, y + p.LineHeight(g_fonts.mono) * 2 + S(12)};
    p.RoundBox(box, theme::barTrack, theme::border, S(6));
    if (g_view.currentLine > 0)
        swprintf(buf, 200, L"Prossima istruzione · riga %d · passo %llu", g_view.currentLine, (unsigned long long)s.steps);
    else
        swprintf(buf, 200, L"Prossima istruzione · fuori dallo snippet · passo %llu", (unsigned long long)s.steps);
    p.Text(x + S(10), y + S(5), buf, g_fonts.smallFont, theme::quiet);
    swprintf(buf, 200, L"0x%llX   ", (unsigned long long)s.ctx.Rip);
    int aw = p.Text(x + S(10), y + S(5) + p.LineHeight(g_fonts.smallFont) + S(1), buf, g_fonts.mono, theme::quiet);
    p.Text(x + S(10) + aw, y + S(5) + p.LineHeight(g_fonts.smallFont) + S(1), g_view.instruction, g_fonts.mono,
           theme::text);
    y = box.bottom + S(10);

    // Registri a 64 bit.
    const Snapshot &pv = g_view.prev;
    int nameW = 0;
    for (const auto &r : kRegs) nameW = std::max(nameW, p.Width(r.name, g_fonts.mono));
    nameW += S(12);
    int hexW = p.Width(L" 0000000000000000", g_fonts.mono);
    int cell = FitCell(right - x - nameW - hexW, 64);
    int flagsH = p.LineHeight(g_fonts.smallFont) * 3 + S(28);
    int avail = bottom - y - flagsH;
    // Pannello basso: rimpicciolisci i bit così tutti i 17 registri restano visibili.
    cell = std::max(3, std::min(cell, avail / (int)(sizeof(kRegs) / sizeof(kRegs[0])) - S(2)));
    int rowH = std::clamp(avail / (int)(sizeof(kRegs) / sizeof(kRegs[0])), cell + S(2), cell + S(10));
    rowH = std::max(rowH, p.LineHeight(g_fonts.mono));
    int bitsW = BitRowWidth(cell, 64);
    for (const auto &r : kRegs) {
        if (y + rowH > bottom - flagsH) break;
        uint64_t v = s.ctx.*(r.field);
        uint64_t changed = pv.valid ? v ^ (pv.ctx.*(r.field)) : 0;
        if (r.field == &CONTEXT::Rip) changed = 0; // RIP cambia sempre: non evidenziarlo
        int ty = y + (rowH - p.LineHeight(g_fonts.mono)) / 2;
        p.Text(x, ty, r.name, g_fonts.mono, changed ? theme::accent : theme::text);
        DrawBits(p, x + nameW, y + (rowH - cell) / 2, cell, cell, v, changed, 64);
        p.Text(x + nameW + bitsW + S(8), ty, Hex64(v), g_fonts.mono, changed ? theme::accent : theme::text);
        y += rowH;
    }

    // Flag (registro RFLAGS).
    y += S(8);
    int fx = x;
    int boxW = S(44), boxH = p.LineHeight(g_fonts.smallFont) * 2 + S(6);
    for (const auto &f : kFlags) {
        bool on = (s.ctx.EFlags >> f.bit) & 1;
        bool ch = pv.valid && (((pv.ctx.EFlags ^ s.ctx.EFlags) >> f.bit) & 1);
        RECT b{fx, y, fx + boxW, y + boxH};
        p.RoundBox(b, ch ? theme::accentSoft : theme::panel, ch ? theme::accent : theme::border, S(6));
        RECT t1{b.left, b.top + S(2), b.right, b.top + S(2) + p.LineHeight(g_fonts.smallFont)};
        p.TextIn(t1, f.name, g_fonts.smallFont, theme::quiet, DT_CENTER);
        RECT t2{b.left, t1.bottom, b.right, b.bottom};
        p.TextIn(t2, on ? L"1" : L"0", g_fonts.mono, ch ? theme::accent : theme::text, DT_CENTER);
        fx += boxW + S(6);
    }
    y += boxH + S(6);
    p.TextIn(RECT{x, y, right, y + p.LineHeight(g_fonts.smallFont)},
             L"CF riporto · PF parità · AF riporto ausiliario · ZF risultato zero · SF segno · OF overflow",
             g_fonts.smallFont, theme::quiet);
}

// ---------------------------------------------------------------- finestre

LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_CREATE: {
        wchar_t cls[32];
        GetClassNameW(hwnd, cls, 32);
        if (wcscmp(cls, kRamClass) == 0)
            CreateWindowExW(0, kRamDataClass, L"", WS_CHILD | WS_VISIBLE | WS_VSCROLL, 0, 0, 10, 10, hwnd, nullptr,
                            ((LPCREATESTRUCTW)lp)->hInstance, nullptr);
        return 0;
    }
    case WM_SIZE:
        LayoutRamData(hwnd);
        InvalidateRect(hwnd, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL:
        if (HWND data = FindWindowExW(hwnd, nullptr, kRamDataClass, nullptr)) return SendMessageW(data, msg, wp, lp);
        break;
    case WM_PAINT: {
        // Il pannello RAM viene invalidato dall'esterno: ridisegna anche l'area dei dati.
        if (HWND data = FindWindowExW(hwnd, nullptr, kRamDataClass, nullptr)) InvalidateRect(data, nullptr, FALSE);
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        HGDIOBJ oldFont = SelectObject(mem, g_fonts.ui);
        SetBkMode(mem, TRANSPARENT);

        Painter p{mem};
        p.Fill(rc, theme::window);
        p.RoundBox(rc, theme::panel, theme::border, S(10));
        wchar_t cls[32];
        GetClassNameW(hwnd, cls, 32);
        if (wcscmp(cls, kRamClass) == 0) PaintRam(p, rc);
        else PaintCpu(p, rc);

        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldFont);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

void RegisterPanelClasses(HINSTANCE inst, const PanelFonts &fonts) {
    g_fonts = fonts;
    for (const wchar_t *name : {kRamClass, kCpuClass}) {
        WNDCLASSW wc{};
        wc.lpfnWndProc = PanelProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = name;
        RegisterClassW(&wc);
    }
    WNDCLASSW dc{};
    dc.lpfnWndProc = RamDataProc;
    dc.hInstance = inst;
    dc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    dc.lpszClassName = kRamDataClass;
    RegisterClassW(&dc);
}

HWND CreateRamPanel(HWND parent, int id) {
    return CreateWindowExW(0, kRamClass, L"", WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
}

HWND CreateCpuPanel(HWND parent, int id) {
    return CreateWindowExW(0, kCpuClass, L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, parent, (HMENU)(INT_PTR)id,
                           (HINSTANCE)GetWindowLongPtrW(parent, GWLP_HINSTANCE), nullptr);
}
