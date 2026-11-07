#pragma once
// Pannelli disegnati a mano: RAM (sopra) e CPU (sotto).
#include <windows.h>
#include <string>

#include "runner.h"
#include "sysinfo.h"

// Tutto ciò che i pannelli mostrano. Vive nel thread dell'interfaccia.
struct ViewState {
    Snapshot cur, prev;
    std::wstring instruction;  // istruzione corrente disassemblata
    int currentLine = 0;       // riga di C corrente (0 = fuori dallo snippet)
    MemInfo mem;
    bool memReady = false;
    CpuInfo cpu;
    MEMORYSTATUSEX memStatus{};
    uint64_t appWorkingSet = 0;
};

extern ViewState g_view;

struct PanelFonts {
    HFONT ui = nullptr;
    HFONT bold = nullptr;
    HFONT mono = nullptr;
    HFONT smallFont = nullptr;
    int dpi = 96;
};

void RegisterPanelClasses(HINSTANCE inst, const PanelFonts &fonts);
HWND CreateRamPanel(HWND parent, int id);
HWND CreateCpuPanel(HWND parent, int id);
