// RegiScope: scrivi uno snippet in C, eseguilo istruzione per istruzione
// e guarda registri della CPU e memoria bit per bit.
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <psapi.h>

#include <algorithm>
#include <memory>
#include <vector>
#include <string>

#include <Zydis/Zydis.h>

#include "messages.h"
#include "panels.h"
#include "runner.h"
#include "sysinfo.h"
#include "theme.h"

#ifdef _MSC_VER
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' "     \
                        "version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' " \
                        "language='*'\"")
#endif

namespace {

enum : int {
    IDC_EDITOR = 100, IDC_OUTPUT, IDC_RAM, IDC_CPU, IDC_STATUS,
    ID_OPEN = 200, ID_SAVE, ID_COMPILE, ID_RUN, ID_STEP, ID_STEPLINE, ID_STOP, ID_EXAMPLES,
    ID_EXAMPLE_FIRST = 300, // + indice dell'esercizio nel menu
};

constexpr UINT_PTR kMemTimer = 1;

const wchar_t kDefaultSnippet[] =
    L"/*\r\n"
    L" * Esempio di RegiScope\r\n"
    L" *\r\n"
    L" * Cosa fa: mette 5 in x e lo incrementa a 6, poi chiede al sistema\r\n"
    L" * un blocco di memoria per 4 interi (malloc) e ci scrive i quadrati\r\n"
    L" * di 0, 1, 2, 3 calcolati dalla funzione quadrato(). Infine stampa\r\n"
    L" * x e v[3], libera il blocco (free) e restituisce x.\r\n"
    L" *\r\n"
    L" * Cosa guardare con Avanti (F10) e Avanti riga (F11):\r\n"
    L" *  - CPU: RAX cambia quando x passa da 5 a 6 e quando quadrato()\r\n"
    L" *    restituisce il risultato; RSP e RIP seguono la chiamata.\r\n"
    L" *  - RAM: x e i (il contatore del ciclo) sono nei bit dello stack, il\r\n"
    L" *    blocco di malloc compare sotto \"Heap\" e si riempie a ogni giro.\r\n"
    L" */\r\n"
    L"#include <stdio.h>\r\n"
    L"#include <stdlib.h>\r\n"
    L"\r\n"
    L"int quadrato(int n) {\r\n"
    L"    return n * n;\r\n"
    L"}\r\n"
    L"\r\n"
    L"int main(void) {\r\n"
    L"    int x = 5;\r\n"
    L"    x = x + 1;\r\n"
    L"\r\n"
    L"    int *v = malloc(4 * sizeof(int));\r\n"
    L"    for (int i = 0; i < 4; i++)\r\n"
    L"        v[i] = quadrato(i);\r\n"
    L"\r\n"
    L"    printf(\"x = %d, v[3] = %d\\n\", x, v[3]);\r\n"
    L"    free(v);\r\n"
    L"    return x;\r\n"
    L"}\r\n";

struct App {
    HINSTANCE inst = nullptr;
    HWND main = nullptr, editor = nullptr, output = nullptr, ram = nullptr, cpu = nullptr, status = nullptr;
    HWND buttons[7] = {};
    HWND gutter = nullptr;
    HWND examplesButton = nullptr;
    std::vector<std::wstring> examplePaths;
    WNDPROC editorProc = nullptr;
    int lastFirstLine = -1, lastLineCount = -1;
    bool stepSession = false;   // passo passo in corso: pulsante Avanti verde
    bool lineStepping = false;  // "Avanti riga": avanza finché la riga cambia
    int lineStepFrom = 0;
    uint64_t lineStepCount = 0;
    PanelFonts fonts;
    HFONT editorFont = nullptr;
    HBRUSH windowBrush = nullptr;
    std::unique_ptr<Runner> runner;
    std::wstring exeDir;
    std::wstring currentFile;
} app;

int S(int v) { return MulDiv(v, app.fonts.dpi, 96); }

std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    std::wstring s(n + 1, L'\0');
    GetWindowTextW(h, &s[0], n + 1);
    s.resize(n);
    return s;
}

void AppendOutput(const std::wstring &text) {
    int len = GetWindowTextLengthW(app.output);
    SendMessageW(app.output, EM_SETSEL, len, len);
    SendMessageW(app.output, EM_REPLACESEL, FALSE, (LPARAM)text.c_str());
}

void ClearOutput() { SetWindowTextW(app.output, L""); }

void RefreshPanels() {
    InvalidateRect(app.ram, nullptr, FALSE);
    InvalidateRect(app.cpu, nullptr, FALSE);
}

// ------------------------------------------------------------- numeri di riga

int EditorLineHeight() {
    HDC dc = GetDC(app.editor);
    HGDIOBJ old = SelectObject(dc, app.editorFont);
    TEXTMETRICW tm{};
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    ReleaseDC(app.editor, dc);
    return tm.tmHeight;
}

int GutterWidth() {
    HDC dc = GetDC(app.main);
    HGDIOBJ old = SelectObject(dc, app.editorFont);
    SIZE sz{};
    GetTextExtentPoint32W(dc, L"9999", 4, &sz);
    SelectObject(dc, old);
    ReleaseDC(app.main, dc);
    return S(24) + sz.cx + S(10);
}

// Freccia verde che punta a destra, alta quanto una riga.
void DrawStepArrow(HDC dc, int x, int y, int lineH) {
    int h = lineH * 7 / 10, top = y + (lineH - h) / 2, mid = y + lineH / 2;
    int shaft = h / 3, head = h * 6 / 10, len = S(16);
    POINT pts[7] = {
        {x, mid - shaft / 2}, {x + len - head, mid - shaft / 2}, {x + len - head, top},
        {x + len, mid},       {x + len - head, top + h},       {x + len - head, mid + shaft / 2},
        {x, mid + shaft / 2},
    };
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, theme::stepDark);
    SetDCBrushColor(dc, theme::step);
    Polygon(dc, pts, 7);
}

LRESULT CALLBACK GutterProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1;
    if (msg != WM_PAINT) return DefWindowProcW(hwnd, msg, wp, lp);

    PAINTSTRUCT ps;
    HDC dc = BeginPaint(hwnd, &ps);
    RECT rc;
    GetClientRect(hwnd, &rc);
    HDC mem = CreateCompatibleDC(dc);
    HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(mem, bmp);
    SetDCBrushColor(mem, theme::gutter);
    FillRect(mem, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
    RECT edge{rc.right - 1, 0, rc.right, rc.bottom};
    SetDCBrushColor(mem, theme::border);
    FillRect(mem, &edge, (HBRUSH)GetStockObject(DC_BRUSH));

    HGDIOBJ oldFont = SelectObject(mem, app.editorFont);
    SetBkMode(mem, TRANSPARENT);
    int lineH = EditorLineHeight();
    POINT origin{0, 0}; // origine dell'area di testo dell'editor, vista dal margine
    MapWindowPoints(app.editor, hwnd, &origin, 1);
    RECT erc;
    GetClientRect(app.editor, &erc);
    int first = (int)SendMessageW(app.editor, EM_GETFIRSTVISIBLELINE, 0, 0);
    int count = (int)SendMessageW(app.editor, EM_GETLINECOUNT, 0, 0);
    int prevY = -lineH;
    for (int i = first; i < count; ++i) {
        int ch = (int)SendMessageW(app.editor, EM_LINEINDEX, i, 0);
        LRESULT pos = SendMessageW(app.editor, EM_POSFROMCHAR, ch, 0);
        int y = pos == -1 ? prevY + lineH : (short)HIWORD(pos);
        prevY = y;
        if (y > erc.bottom) break;
        bool current = app.stepSession && i + 1 == g_view.currentLine;
        std::wstring num = std::to_wstring(i + 1);
        RECT r{0, origin.y + y, rc.right - S(8), origin.y + y + lineH};
        SetTextColor(mem, current ? theme::stepDark : theme::quiet);
        DrawTextW(mem, num.c_str(), (int)num.size(), &r, DT_RIGHT | DT_SINGLELINE | DT_NOPREFIX);
        if (current) DrawStepArrow(mem, S(4), origin.y + y, lineH);
    }

    BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldFont);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
    return 0;
}

// L'editor è un controllo EDIT standard: dopo ogni messaggio controlliamo
// se lo scorrimento o il numero di righe sono cambiati e ridisegniamo il margine.
LRESULT CALLBACK EditorSubclass(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    LRESULT r = CallWindowProcW(app.editorProc, hwnd, msg, wp, lp);
    if (msg == WM_NCDESTROY) return r;
    int first = (int)CallWindowProcW(app.editorProc, hwnd, EM_GETFIRSTVISIBLELINE, 0, 0);
    int count = (int)CallWindowProcW(app.editorProc, hwnd, EM_GETLINECOUNT, 0, 0);
    if (first != app.lastFirstLine || count != app.lastLineCount) {
        app.lastFirstLine = first;
        app.lastLineCount = count;
        if (app.gutter) InvalidateRect(app.gutter, nullptr, FALSE);
    }
    return r;
}

// Scorre l'editor perché la riga (da 1) sia visibile.
void EnsureLineVisible(int line) {
    if (line <= 0) return;
    RECT rc;
    GetClientRect(app.editor, &rc);
    int visible = std::max(1, (int)(rc.bottom / std::max(1, EditorLineHeight())));
    int first = (int)SendMessageW(app.editor, EM_GETFIRSTVISIBLELINE, 0, 0);
    int target = line - 1;
    if (target < first || target >= first + visible)
        SendMessageW(app.editor, EM_LINESCROLL, 0, target - first - visible / 3);
}

void SetStepSession(bool on) {
    app.stepSession = on;
    if (!on) {
        app.lineStepping = false;
        g_view.currentLine = 0;
    }
    InvalidateRect(app.buttons[4], nullptr, FALSE);
    InvalidateRect(app.gutter, nullptr, FALSE);
}

// Pulsanti disegnati a mano: stile chiaro, Avanti verde durante il passo passo.
void DrawToolbarButton(const DRAWITEMSTRUCT *di) {
    HDC dc = di->hDC;
    RECT rc = di->rcItem;
    bool disabledState = (di->itemState & ODS_DISABLED) != 0;
    bool pressed = (di->itemState & ODS_SELECTED) != 0;
    bool green = di->CtlID == ID_STEP && app.stepSession && !disabledState;

    SetDCBrushColor(dc, theme::window);
    FillRect(dc, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
    COLORREF fill = green ? (pressed ? theme::stepDark : theme::step) : (pressed ? theme::buttonDown : theme::panel);
    COLORREF line = green ? theme::stepDark : theme::border;
    SelectObject(dc, GetStockObject(DC_PEN));
    SelectObject(dc, GetStockObject(DC_BRUSH));
    SetDCPenColor(dc, line);
    SetDCBrushColor(dc, fill);
    RoundRect(dc, rc.left, rc.top, rc.right, rc.bottom, S(8), S(8));

    wchar_t label[64];
    GetWindowTextW(di->hwndItem, label, 64);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, disabledState ? theme::disabled : green ? RGB(0xFF, 0xFF, 0xFF) : theme::text);
    HGDIOBJ old = SelectObject(dc, green ? app.fonts.bold : app.fonts.ui);
    RECT textRc = rc;
    bool dropdown = di->CtlID == ID_EXAMPLES;
    if (dropdown) textRc.right -= S(14);
    DrawTextW(dc, label, -1, &textRc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    if (dropdown) { // triangolino del menu a tendina
        int cx = rc.right - S(16), cy = (rc.top + rc.bottom) / 2;
        POINT tri[3] = {{cx - S(4), cy - S(2)}, {cx + S(4), cy - S(2)}, {cx, cy + S(3)}};
        COLORREF c = disabledState ? theme::disabled : theme::text;
        SetDCPenColor(dc, c);
        SetDCBrushColor(dc, c);
        Polygon(dc, tri, 3);
    }
    SelectObject(dc, old);
    if ((di->itemState & ODS_FOCUS) && !green) {
        RECT f = rc;
        InflateRect(&f, -S(3), -S(3));
        DrawFocusRect(dc, &f);
    }
}

void UpdateUi() {
    using St = Runner::State;
    St st = app.runner->GetState();
    bool busy = st == St::Running || st == St::Paused;
    EnableWindow(app.buttons[0], !busy);                         // Apri
    EnableWindow(app.examplesButton, !busy);                     // Esempi
    EnableWindow(app.buttons[2], !busy);                         // Compila
    EnableWindow(app.buttons[3], st != St::Running);             // Esegui
    EnableWindow(app.buttons[4], st != St::Running);             // Avanti
    EnableWindow(app.buttons[5], st != St::Running);             // Avanti riga
    EnableWindow(app.buttons[6], busy);                          // Ferma
    SendMessageW(app.editor, EM_SETREADONLY, busy, 0);

    std::wstring s;
    switch (st) {
    case St::Idle: s = L"Pronto"; break;
    case St::Ready: s = L"Compilato"; break;
    case St::Paused:
        s = L"In pausa · ";
        if (g_view.currentLine > 0) s += L"riga " + std::to_wstring(g_view.currentLine) + L" · ";
        s += L"passo " + std::to_wstring(g_view.cur.steps);
        break;
    case St::Running: s = L"In esecuzione…"; break;
    case St::Finished: s = L"Terminato"; break;
    }
    SetWindowTextW(app.status, s.c_str());
    InvalidateRect(app.status, nullptr, TRUE);
}

// Testo di stato in alto a destra: una sola riga, allineato a destra con un
// margine dal bordo, e "…" se la finestra è troppo stretta.
void DrawStatus(const DRAWITEMSTRUCT *di) {
    RECT rc = di->rcItem;
    SetDCBrushColor(di->hDC, theme::window);
    FillRect(di->hDC, &rc, (HBRUSH)GetStockObject(DC_BRUSH));
    wchar_t text[128];
    GetWindowTextW(di->hwndItem, text, 128);
    rc.right -= S(10);
    SetBkMode(di->hDC, TRANSPARENT);
    SetTextColor(di->hDC, RGB(0, 0, 0));
    HGDIOBJ old = SelectObject(di->hDC, app.fonts.ui);
    DrawTextW(di->hDC, text, -1, &rc, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS | DT_NOCLIP);
    SelectObject(di->hDC, old);
}

// ------------------------------------------------------------- comandi

bool CompileEditor() {
    std::wstring src = GetText(app.editor);
    std::wstring clean;
    clean.reserve(src.size());
    for (wchar_t c : src)
        if (c != L'\r') clean += c;
    ClearOutput();
    std::wstring log;
    bool ok = app.runner->Compile(WideToUtf8(clean), log);
    std::wstring shown;
    for (wchar_t c : log) {
        if (c == L'\n' && (shown.empty() || shown.back() != L'\r')) shown += L'\r';
        shown += c;
    }
    AppendOutput(ok ? L"Compilazione riuscita.\r\n" : L"Errori di compilazione:\r\n" + shown);
    UpdateUi();
    return ok;
}

void StartSnippet(bool stepping) {
    using St = Runner::State;
    St st = app.runner->GetState();
    if (st == St::Paused) {
        if (stepping) {
            app.runner->StepOnce();
        } else {
            SetStepSession(false);
            app.runner->ContinueRun();
        }
        UpdateUi();
        return;
    }
    if (st == St::Running) return;
    if (!CompileEditor()) return;
    g_view.cur = Snapshot{};
    g_view.prev = Snapshot{};
    g_view.instruction.clear();
    RefreshPanels();
    AppendOutput(stepping ? L"Avvio passo passo: fermo alla prima istruzione di main.\r\n" : L"Esecuzione…\r\n");
    SetStepSession(stepping);
    if (!app.runner->Start(stepping)) {
        AppendOutput(L"Impossibile avviare lo snippet.\r\n");
        SetStepSession(false);
    }
    UpdateUi();
}

// Avanti riga: esegue istruzioni finché si arriva a un'altra riga dello snippet.
void StepLine() {
    if (app.runner->GetState() != Runner::State::Paused) {
        StartSnippet(true); // al primo colpo si ferma su main, come Avanti
        return;
    }
    app.lineStepping = true;
    app.lineStepFrom = g_view.currentLine;
    app.lineStepCount = 0;
    app.runner->StepOnce();
    UpdateUi();
}

// Mette il testo (UTF-8, con \n) nell'editor, convertendo i ritorni a capo.
void LoadSourceText(std::string data) {
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB &&
        (unsigned char)data[2] == 0xBF)
        data.erase(0, 3);
    std::wstring text;
    for (wchar_t c : Utf8ToWide(data)) {
        if (c == L'\r') continue;
        if (c == L'\n') text += L'\r';
        text += c;
    }
    SetWindowTextW(app.editor, text.c_str());
    SendMessageW(app.editor, EM_SETSEL, 0, 0);
    SendMessageW(app.editor, EM_SCROLLCARET, 0, 0);
}

bool ReadWholeFile(const std::wstring &path, std::string &data) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    GetFileSizeEx(f, &size);
    data.assign((size_t)size.QuadPart, '\0');
    DWORD read = 0;
    ReadFile(f, &data[0], (DWORD)data.size(), &read, nullptr);
    CloseHandle(f);
    data.resize(read);
    return true;
}

// Titolo di un esercizio: la seconda riga del commento iniziale, es. "01 · Variabili ...".
std::wstring ExampleTitle(const std::wstring &path, const std::wstring &fallback) {
    std::string data;
    if (!ReadWholeFile(path, data)) return fallback;
    size_t a = data.find('\n');
    if (a == std::string::npos) return fallback;
    size_t b = data.find('\n', a + 1);
    std::string line = data.substr(a + 1, b == std::string::npos ? std::string::npos : b - a - 1);
    size_t start = line.find_first_not_of(" *\t");
    if (start == std::string::npos) return fallback;
    line = line.substr(start);
    while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
    std::wstring title = Utf8ToWide(line);
    std::wstring menuText;
    for (wchar_t c : title) {
        if (c == L'&') menuText += L'&'; // nei menu & va raddoppiata
        menuText += c;
    }
    return menuText.empty() ? fallback : menuText;
}

// Pulsante "Esempi": menu con gli esercizi della cartella esempi accanto all'eseguibile.
void ShowExamplesMenu() {
    using St = Runner::State;
    St st = app.runner->GetState();
    if (st == St::Running || st == St::Paused) return;

    app.examplePaths.clear();
    std::wstring dir = app.exeDir + L"\\esempi\\";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.c").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            app.examplePaths.push_back(dir + fd.cFileName);
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    std::sort(app.examplePaths.begin(), app.examplePaths.end());

    HMENU menu = CreatePopupMenu();
    if (app.examplePaths.empty()) {
        AppendMenuW(menu, MF_STRING | MF_GRAYED, 0, L"Nessun esercizio trovato nella cartella esempi");
    }
    for (size_t i = 0; i < app.examplePaths.size(); ++i) {
        const std::wstring &p = app.examplePaths[i];
        std::wstring name = p.substr(p.find_last_of(L"\\") + 1);
        AppendMenuW(menu, MF_STRING, ID_EXAMPLE_FIRST + i, ExampleTitle(p, name).c_str());
    }
    RECT rc;
    GetWindowRect(app.examplesButton, &rc);
    TrackPopupMenu(menu, TPM_LEFTALIGN | TPM_TOPALIGN, rc.left, rc.bottom + S(2), 0, app.main, nullptr);
    DestroyMenu(menu);
}

void LoadExample(size_t index) {
    if (index >= app.examplePaths.size()) return;
    std::string data;
    if (!ReadWholeFile(app.examplePaths[index], data)) {
        MessageBoxW(app.main, L"Impossibile aprire l'esercizio.", L"RegiScope", MB_ICONERROR);
        return;
    }
    LoadSourceText(data);
    app.currentFile.clear(); // Salva chiederà dove salvare: l'esercizio originale resta intatto
    const std::wstring &p = app.examplePaths[index];
    SetWindowTextW(app.main, (L"RegiScope · esercizio " + p.substr(p.find_last_of(L"\\") + 1)).c_str());
    ClearOutput();
    AppendOutput(L"Esercizio caricato. Leggi il commento iniziale, poi premi Avanti (F10).\r\n");
}

void OpenFile() {
    wchar_t path[MAX_PATH] = L"";
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = app.main;
    ofn.lpstrFilter = L"File C (*.c)\0*.c\0Tutti i file (*.*)\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (!GetOpenFileNameW(&ofn)) return;

    std::string data;
    if (!ReadWholeFile(path, data)) {
        MessageBoxW(app.main, L"Impossibile aprire il file.", L"RegiScope", MB_ICONERROR);
        return;
    }
    LoadSourceText(data);
    app.currentFile = path;
    SetWindowTextW(app.main, (L"RegiScope · " + app.currentFile).c_str());
}

void SaveFile() {
    wchar_t path[MAX_PATH] = L"";
    if (!app.currentFile.empty()) wcsncpy(path, app.currentFile.c_str(), MAX_PATH - 1);
    OPENFILENAMEW ofn{sizeof(ofn)};
    ofn.hwndOwner = app.main;
    ofn.lpstrFilter = L"File C (*.c)\0*.c\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrDefExt = L"c";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    if (!GetSaveFileNameW(&ofn)) return;

    std::wstring text;
    for (wchar_t c : GetText(app.editor))
        if (c != L'\r') text += c;
    std::string data = WideToUtf8(text);
    HANDLE f = CreateFileW(path, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) { MessageBoxW(app.main, L"Impossibile salvare il file.", L"RegiScope", MB_ICONERROR); return; }
    DWORD written = 0;
    WriteFile(f, data.data(), (DWORD)data.size(), &written, nullptr);
    CloseHandle(f);
    app.currentFile = path;
    SetWindowTextW(app.main, (L"RegiScope · " + app.currentFile).c_str());
}

// ------------------------------------------------------------- aggiornamenti

void OnSnapshot() {
    if (app.lineStepping) {
        Snapshot probe, unused;
        app.runner->TakeSnapshots(probe, unused);
        int line = app.runner->LineForPc(probe.ctx.Rip);
        if ((line == 0 || line == app.lineStepFrom) && ++app.lineStepCount < 200000) {
            app.runner->StepOnce(); // stessa riga (o fuori dallo snippet): continua
            return;
        }
        app.lineStepping = false;
    }
    app.runner->TakeSnapshots(g_view.cur, g_view.prev);
    g_view.currentLine = app.runner->LineForPc(g_view.cur.ctx.Rip);
    EnsureLineVisible(g_view.currentLine);
    InvalidateRect(app.gutter, nullptr, FALSE);
    g_view.instruction.clear();
    if (g_view.cur.valid && g_view.cur.codeLen) {
        ZydisDisassembledInstruction ins;
        if (ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64, g_view.cur.ctx.Rip, g_view.cur.code,
                                                g_view.cur.codeLen, &ins)))
            g_view.instruction = Utf8ToWide(ins.text);
        else
            g_view.instruction = L"(istruzione non riconosciuta)";
    }
    RefreshPanels();
    UpdateUi();
}

void OnFinished(int code, std::wstring *message) {
    app.runner->Join();
    SetStepSession(false);
    if (message && !message->empty()) AppendOutput(L"\r\n" + *message + L"\r\n");
    else AppendOutput(L"\r\nProgramma terminato: main ha restituito " + std::to_wstring(code) + L".\r\n");
    delete message;
    UpdateUi();
}

void UpdateMemoryStatus() {
    g_view.memStatus.dwLength = sizeof(g_view.memStatus);
    GlobalMemoryStatusEx(&g_view.memStatus);
    PROCESS_MEMORY_COUNTERS pmc{sizeof(pmc)};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) g_view.appWorkingSet = pmc.WorkingSetSize;
    InvalidateRect(app.ram, nullptr, FALSE);
}

DWORD WINAPI MemInfoThread(LPVOID hwnd) {
    auto *info = new MemInfo(QueryMemoryModules());
    if (!PostMessageW((HWND)hwnd, WM_APP_MEMINFO, 0, (LPARAM)info)) delete info;
    return 0;
}

// ------------------------------------------------------------- layout

void Layout(int w, int h) {
    int m = S(8);
    int barH = S(30);
    int x = m;
    const int widths[7] = {S(64), S(64), S(76), S(76), S(76), S(96), S(70)};
    for (int i = 0; i < 7; ++i) {
        if (i == 2) { // dopo Apri e Salva: Esempi, poi i comandi di esecuzione
            MoveWindow(app.examplesButton, x, m, S(84), barH, TRUE);
            x += S(84) + S(6) + S(12);
        }
        MoveWindow(app.buttons[i], x, m, widths[i], barH, TRUE);
        x += widths[i] + S(6);
    }
    MoveWindow(app.status, x + S(8), m, std::max(0, w - x - S(8) - S(12)), barH, TRUE);

    int top = m + barH + m;
    int leftW = (w - 3 * m) * 2 / 5;
    int availH = h - top - m;
    int edH = availH * 7 / 10;
    int gw = GutterWidth();
    MoveWindow(app.gutter, m, top, gw, edH, TRUE);
    MoveWindow(app.editor, m + gw, top, leftW - gw, edH, TRUE);
    MoveWindow(app.output, m, top + edH + m, leftW, availH - edH - m, TRUE);

    int rx = m + leftW + m, rw = w - rx - m;
    int ramH = (availH - m) * 9 / 20;
    MoveWindow(app.ram, rx, top, rw, ramH, TRUE);
    MoveWindow(app.cpu, rx, top + ramH + m, rw, availH - ramH - m, TRUE);
}

HFONT MakeFont(const wchar_t *face, int pt, int weight) {
    return CreateFontW(-MulDiv(pt, app.fonts.dpi, 72), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
}

void CreateChildren(HWND hwnd) {
    const wchar_t *labels[7] = {L"Apri", L"Salva", L"Compila", L"Esegui", L"Avanti", L"Avanti riga", L"Ferma"};
    for (int i = 0; i < 7; ++i) {
        app.buttons[i] = CreateWindowExW(0, L"BUTTON", labels[i], WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10, 10,
                                         hwnd, (HMENU)(INT_PTR)(ID_OPEN + i), app.inst, nullptr);
        SendMessageW(app.buttons[i], WM_SETFONT, (WPARAM)app.fonts.ui, TRUE);
    }
    app.examplesButton = CreateWindowExW(0, L"BUTTON", L"Esempi", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 10,
                                         10, hwnd, (HMENU)(INT_PTR)ID_EXAMPLES, app.inst, nullptr);
    app.status = CreateWindowExW(0, L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_OWNERDRAW, 0, 0, 10, 10, hwnd,
                                 (HMENU)(INT_PTR)IDC_STATUS, app.inst, nullptr);
    SendMessageW(app.status, WM_SETFONT, (WPARAM)app.fonts.ui, TRUE);

    app.editor = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", kDefaultSnippet,
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_HSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                     ES_AUTOHSCROLL | ES_WANTRETURN | ES_NOHIDESEL,
                                 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_EDITOR, app.inst, nullptr);
    SendMessageW(app.editor, WM_SETFONT, (WPARAM)app.editorFont, TRUE);
    SendMessageW(app.editor, EM_SETLIMITTEXT, 1 << 20, 0);
    int tabStop = 16; // 4 caratteri
    SendMessageW(app.editor, EM_SETTABSTOPS, 1, (LPARAM)&tabStop);
    app.editorProc = (WNDPROC)SetWindowLongPtrW(app.editor, GWLP_WNDPROC, (LONG_PTR)EditorSubclass);
    app.gutter = CreateWindowExW(0, L"RegiScopeGutter", L"", WS_CHILD | WS_VISIBLE, 0, 0, 10, 10, hwnd, nullptr,
                                 app.inst, nullptr);

    app.output = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
                                 0, 0, 10, 10, hwnd, (HMENU)(INT_PTR)IDC_OUTPUT, app.inst, nullptr);
    SendMessageW(app.output, WM_SETFONT, (WPARAM)app.fonts.mono, TRUE);

    app.ram = CreateRamPanel(hwnd, IDC_RAM);
    app.cpu = CreateCpuPanel(hwnd, IDC_CPU);
}

LRESULT CALLBACK MainProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        app.main = hwnd;
        CreateChildren(hwnd);
        app.runner = std::make_unique<Runner>(hwnd, app.exeDir);
        g_view.cpu = QueryCpu();
        UpdateMemoryStatus();
        SetTimer(hwnd, kMemTimer, 1000, nullptr);
        CloseHandle(CreateThread(nullptr, 0, MemInfoThread, hwnd, 0, nullptr));
        AppendOutput(L"Benvenuto in RegiScope. F10 = Avanti (una istruzione), F11 = Avanti riga, F5 = Esegui, Maiusc+F5 = Ferma.\r\n");
        UpdateUi();
        return 0;
    case WM_SIZE:
        Layout(LOWORD(lp), HIWORD(lp));
        return 0;
    case WM_GETMINMAXINFO: {
        auto *mm = reinterpret_cast<MINMAXINFO *>(lp);
        mm->ptMinTrackSize = {S(960), S(600)};
        return 0;
    }
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == app.status) {
            SetBkColor((HDC)wp, theme::window);
            SetTextColor((HDC)wp, theme::quiet);
            return (LRESULT)app.windowBrush;
        }
        break;
    case WM_DRAWITEM: {
        auto *di = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
        if (di->CtlType == ODT_BUTTON) {
            DrawToolbarButton(di);
            return TRUE;
        }
        if (di->hwndItem == app.status) {
            DrawStatus(di);
            return TRUE;
        }
        break;
    }
    case WM_TIMER:
        if (wp == kMemTimer) UpdateMemoryStatus();
        return 0;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_OPEN: OpenFile(); return 0;
        case ID_SAVE: SaveFile(); return 0;
        case ID_COMPILE:
            if (app.runner->GetState() != Runner::State::Running && app.runner->GetState() != Runner::State::Paused)
                CompileEditor();
            return 0;
        case ID_RUN: StartSnippet(false); return 0;
        case ID_STEP: StartSnippet(true); return 0;
        case ID_STEPLINE: StepLine(); return 0;
        case ID_EXAMPLES: ShowExamplesMenu(); return 0;
        case ID_STOP: app.runner->Stop(); return 0;
        }
        if (LOWORD(wp) >= ID_EXAMPLE_FIRST && LOWORD(wp) < ID_EXAMPLE_FIRST + 100) {
            LoadExample(LOWORD(wp) - ID_EXAMPLE_FIRST);
            return 0;
        }
        break;
    case WM_APP_OUTPUT: {
        auto *text = reinterpret_cast<std::wstring *>(lp);
        AppendOutput(*text);
        delete text;
        return 0;
    }
    case WM_APP_SNAPSHOT:
        OnSnapshot();
        return 0;
    case WM_APP_FINISHED:
        OnFinished((int)wp, reinterpret_cast<std::wstring *>(lp));
        return 0;
    case WM_APP_MEMINFO: {
        auto *info = reinterpret_cast<MemInfo *>(lp);
        g_view.mem = std::move(*info);
        g_view.memReady = true;
        delete info;
        InvalidateRect(app.ram, nullptr, FALSE);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, kMemTimer);
        app.runner.reset(); // ferma lo snippet se è ancora in esecuzione
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

} // namespace

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    INITCOMMONCONTROLSEX icc{sizeof(icc), ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&icc);

    app.inst = inst;
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    app.exeDir = exe;
    app.exeDir = app.exeDir.substr(0, app.exeDir.find_last_of(L"\\/"));

    HDC screen = GetDC(nullptr);
    app.fonts.dpi = GetDeviceCaps(screen, LOGPIXELSY);
    ReleaseDC(nullptr, screen);
    app.fonts.ui = MakeFont(L"Segoe UI", 9, FW_NORMAL);
    app.fonts.smallFont = MakeFont(L"Segoe UI", 8, FW_NORMAL);
    app.fonts.bold = MakeFont(L"Segoe UI Semibold", 10, FW_SEMIBOLD);
    app.fonts.mono = MakeFont(L"Consolas", 9, FW_NORMAL);
    app.editorFont = MakeFont(L"Consolas", 11, FW_NORMAL);
    app.windowBrush = CreateSolidBrush(theme::window);
    RegisterPanelClasses(inst, app.fonts);
    WNDCLASSW gc{};
    gc.lpfnWndProc = GutterProc;
    gc.hInstance = inst;
    gc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    gc.lpszClassName = L"RegiScopeGutter";
    RegisterClassW(&gc);

    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1)); // res/regiscope.ico
    wc.hbrBackground = app.windowBrush;
    wc.lpszClassName = L"RegiScopeMain";
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"RegiScope", WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
                                S(1360), S(880), nullptr, nullptr, inst, nullptr);
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    ACCEL keys[] = {
        {FVIRTKEY | FCONTROL, 'O', ID_OPEN}, {FVIRTKEY | FCONTROL, 'S', ID_SAVE},
        {FVIRTKEY, VK_F7, ID_COMPILE},      {FVIRTKEY, VK_F5, ID_RUN},
        {FVIRTKEY, VK_F10, ID_STEP},        {FVIRTKEY | FSHIFT, VK_F5, ID_STOP},
        {FVIRTKEY, VK_F11, ID_STEPLINE},
    };
    HACCEL accel = CreateAcceleratorTableW(keys, (int)(sizeof(keys) / sizeof(keys[0])));

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        if (TranslateAcceleratorW(hwnd, accel, &m)) continue;
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    DestroyAcceleratorTable(accel);
    return 0;
}
