#include "runner.h"
#include "devices.h"
#include "messages.h"

#include <cstring>
#include <libtcc.h>

#include "tcc_lines.h"

namespace {

constexpr DWORD kTrapFlag = 0x100;
Runner *g_runner = nullptr;
PVOID g_veh = nullptr;

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

void TccError(void *opaque, const char *msg) {
    auto *log = static_cast<std::wstring *>(opaque);
    *log += Utf8ToWide(msg);
    *log += L"\r\n";
}

// Il thread dello snippet salta qui quando va fermato o va in errore.
[[noreturn]] void AbortThunk() {
    Runner *r = g_runner;
    if (r) r->FinishFromThread(-1, r->TakeAbortMessage());
    ExitThread(1);
}

DWORD WINAPI SnippetThread(LPVOID param) {
    auto *r = static_cast<Runner *>(param);
    int rc = r->CallEntry();
    r->FinishFromThread(rc, L"");
    return 0;
}

std::wstring DescribeFault(const EXCEPTION_RECORD *er, uint64_t rip) {
    wchar_t buf[256];
    switch (er->ExceptionCode) {
    case EXCEPTION_ACCESS_VIOLATION: {
        const wchar_t *op = er->ExceptionInformation[0] == 1 ? L"scrittura" : L"lettura";
        swprintf(buf, 256, L"Errore: violazione di accesso (%ls all'indirizzo 0x%llX) all'istruzione 0x%llX.",
                 op, (unsigned long long)er->ExceptionInformation[1], (unsigned long long)rip);
        break;
    }
    case EXCEPTION_INT_DIVIDE_BY_ZERO:
        swprintf(buf, 256, L"Errore: divisione intera per zero all'istruzione 0x%llX.", (unsigned long long)rip);
        break;
    case EXCEPTION_STACK_OVERFLOW:
        swprintf(buf, 256, L"Errore: stack esaurito (ricorsione troppo profonda?).");
        break;
    default:
        swprintf(buf, 256, L"Errore: eccezione 0x%08lX all'istruzione 0x%llX.",
                 (unsigned long)er->ExceptionCode, (unsigned long long)rip);
    }
    return buf;
}

// Gestore di eccezioni vettoriale: riceve il "single step" dopo ogni istruzione.
LONG CALLBACK StepHandler(EXCEPTION_POINTERS *ep) {
    Runner *r = g_runner;
    if (!r || GetCurrentThreadId() != r->ThreadId()) return EXCEPTION_CONTINUE_SEARCH;

    CONTEXT *c = ep->ContextRecord;
    DWORD code = ep->ExceptionRecord->ExceptionCode;

    if (code == EXCEPTION_SINGLE_STEP) {
        c->Dr0 = 0;
        c->Dr6 = 0;
        c->Dr7 = 0;
        c->EFlags &= ~kTrapFlag;
        if (r->StopRequested() && r->InImage(c->Rip)) {
            r->RedirectToAbort(c, L"Esecuzione fermata.");
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        if (!r->InImage(c->Rip)) {
            // Siamo entrati in una funzione dell'app (es. printf): la lasciamo
            // girare liberamente e ci fermiamo di nuovo all'indirizzo di ritorno,
            // con un breakpoint hardware (DR0).
            uint64_t ret = *reinterpret_cast<uint64_t *>(c->Rsp);
            if (r->InImage(ret)) {
                c->Dr0 = ret;
                c->Dr7 = 1; // abilita DR0, tipo "esecuzione"
            }
            return EXCEPTION_CONTINUE_EXECUTION;
        }
        r->PauseHere(c);
        return EXCEPTION_CONTINUE_EXECUTION;
    }

    // Solo gli errori veri (severità "error"): gli altri avvisi passano oltre.
    if ((code & 0xC0000000u) == 0xC0000000u) {
        r->RedirectToAbort(c, DescribeFault(ep->ExceptionRecord, c->Rip));
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

Runner::Runner(HWND notify, std::wstring exeDir) : notify_(notify), exeDir_(std::move(exeDir)) {
    g_runner = this;
    cmdEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    g_veh = AddVectoredExceptionHandler(1, StepHandler);
}

Runner::~Runner() {
    if (thread_) {
        Stop();
        if (WaitForSingleObject(thread_, 1000) == WAIT_TIMEOUT) TerminateThread(thread_, 1);
        CloseHandle(thread_);
        thread_ = nullptr;
    }
    if (g_veh) RemoveVectoredExceptionHandler(g_veh);
    g_runner = nullptr;
    FreeCompiled();
    CloseHandle(cmdEvent_);
}

void Runner::FreeCompiled() {
    if (tcc_) tcc_delete(tcc_);
    tcc_ = nullptr;
    regiscope_free_lines(lines_);
    lines_ = nullptr;
    textBase_ = 0;
    entry_ = nullptr;
    imgLo_ = imgHi_ = 0;
}

bool Runner::Compile(const std::string &source, std::wstring &log) {
    if (thread_) return false; // c'è ancora uno snippet in esecuzione
    FreeCompiled();
    log.clear();

    const std::string includeDir = WideToUtf8(exeDir_ + L"\\snippet_include");
    const std::string runtimeDir = WideToUtf8(exeDir_ + L"\\snippet_runtime");

    tcc_ = tcc_new();
    if (!tcc_) { log = L"Impossibile inizializzare TinyCC.\r\n"; return false; }
    tcc_set_error_func(tcc_, &log, TccError);
    tcc_set_lib_path(tcc_, runtimeDir.c_str());
    tcc_set_options(tcc_, "-nostdinc -nostdlib -g");
    tcc_set_output_type(tcc_, TCC_OUTPUT_MEMORY);
    tcc_add_sysinclude_path(tcc_, includeDir.c_str());

    bool ok = tcc_compile_string(tcc_, source.c_str()) != -1;
    // Funzioni di supporto che il codice generato da TinyCC può chiamare
    // (conversioni a 64 bit, __chkstk per array grandi sullo stack).
    if (ok) ok = tcc_add_file(tcc_, (runtimeDir + "\\libtcc1.c").c_str()) != -1;
    if (ok) ok = tcc_add_file(tcc_, (runtimeDir + "\\chkstk.S").c_str()) != -1;
    if (ok) {
        for (const auto &sym : snippet_rt::Symbols()) tcc_add_symbol(tcc_, sym.name, sym.addr);
        for (const auto &sym : dev::Symbols()) tcc_add_symbol(tcc_, sym.name, sym.addr);
        lines_ = regiscope_capture_lines(tcc_);
        ok = tcc_relocate(tcc_) >= 0;
        if (ok) textBase_ = regiscope_finish_lines(lines_, tcc_);
    }
    if (ok) {
        entry_ = reinterpret_cast<int (*)()>(tcc_get_symbol(tcc_, "main"));
        if (!entry_) { log += L"Manca la funzione main.\r\n"; ok = false; }
    }
    if (!ok) { FreeCompiled(); state_ = State::Idle; return false; }

    // L'area di memoria che TinyCC ha riservato per il codice dello snippet:
    // il passo passo si ferma solo dentro quest'area.
    MEMORY_BASIC_INFORMATION mbi{};
    VirtualQuery(reinterpret_cast<void *>(entry_), &mbi, sizeof(mbi));
    auto base = reinterpret_cast<uint8_t *>(mbi.AllocationBase);
    uint8_t *p = base;
    while (VirtualQuery(p, &mbi, sizeof(mbi)) && mbi.AllocationBase == base) p += mbi.RegionSize;
    imgLo_ = reinterpret_cast<uint64_t>(base);
    imgHi_ = reinterpret_cast<uint64_t>(p);

    state_ = State::Ready;
    return true;
}

bool Runner::Start(bool stepping) {
    if (!entry_ || thread_) return false;
    snippet_rt::Reset(notify_);
    {
        std::lock_guard<std::mutex> lock(snapMx_);
        cur_ = Snapshot{};
        prev_ = Snapshot{};
    }
    steps_ = 0;
    stopRequested_ = false;
    paused_ = false;
    cmd_ = Cmd::None;
    ResetEvent(cmdEvent_);

    thread_ = CreateThread(nullptr, 1 << 20, SnippetThread, this, CREATE_SUSPENDED, &tid_);
    if (!thread_) return false;

    if (stepping) {
        // Breakpoint hardware sulla prima istruzione di main.
        CONTEXT c{};
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        GetThreadContext(thread_, &c);
        c.Dr0 = reinterpret_cast<DWORD64>(entry_);
        c.Dr7 = 1;
        c.ContextFlags = CONTEXT_DEBUG_REGISTERS;
        SetThreadContext(thread_, &c);
    }
    state_ = State::Running;
    ResumeThread(thread_);
    return true;
}

void Runner::PauseHere(CONTEXT *c) {
    Snapshot s;
    s.valid = true;
    s.ctx = *c;
    s.steps = ++steps_;

    uint64_t codeEnd = imgHi_ - c->Rip;
    s.codeLen = codeEnd < sizeof(s.code) ? (size_t)codeEnd : sizeof(s.code);
    memcpy(s.code, reinterpret_cast<const void *>(c->Rip), s.codeLen);

    // Stack: da RSP fino a RBP (il frame della funzione corrente), al massimo 256 byte.
    ULONG_PTR low = 0, high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    uint64_t len = 64;
    if (c->Rbp > c->Rsp && c->Rbp - c->Rsp <= 240) len = c->Rbp - c->Rsp + 16;
    if (len > 256) len = 256;
    if (c->Rsp + len > high) len = high > c->Rsp ? high - c->Rsp : 0;
    s.stackBase = c->Rsp;
    s.stack.assign(reinterpret_cast<const uint8_t *>(c->Rsp), reinterpret_cast<const uint8_t *>(c->Rsp) + len);

    s.heap = snippet_rt::SnapshotHeap(32);

    {
        std::lock_guard<std::mutex> lock(snapMx_);
        prev_ = std::move(cur_);
        cur_ = std::move(s);
    }
    paused_ = true;
    state_ = State::Paused;
    PostMessageW(notify_, WM_APP_SNAPSHOT, 0, 0);

    WaitForSingleObject(cmdEvent_, INFINITE);
    paused_ = false;

    Cmd cmd = cmd_.exchange(Cmd::None);
    if (cmd == Cmd::Stop || stopRequested_) {
        RedirectToAbort(c, L"Esecuzione fermata.");
        return;
    }
    state_ = State::Running;
    if (cmd == Cmd::Step) c->EFlags |= kTrapFlag;
}

void Runner::RedirectToAbort(CONTEXT *c, const std::wstring &message) {
    {
        std::lock_guard<std::mutex> lock(abortMx_);
        abortMsg_ = message;
    }
    c->Rip = reinterpret_cast<DWORD64>(&AbortThunk);
    c->Rsp = ((c->Rsp - 0x400) & ~0xFull) - 8; // allineamento come dopo una call
    c->EFlags &= ~kTrapFlag;
    c->Dr7 = 0;
}

std::wstring Runner::TakeAbortMessage() {
    std::lock_guard<std::mutex> lock(abortMx_);
    return abortMsg_;
}

void Runner::FinishFromThread(int code, const std::wstring &message) {
    state_ = State::Finished;
    auto *msg = new std::wstring(message);
    if (!PostMessageW(notify_, WM_APP_FINISHED, (WPARAM)code, (LPARAM)msg)) delete msg;
}

void Runner::StepOnce() {
    if (!paused_) return;
    cmd_ = Cmd::Step;
    SetEvent(cmdEvent_);
}

void Runner::ContinueRun() {
    if (!paused_) return;
    cmd_ = Cmd::Continue;
    SetEvent(cmdEvent_);
}

void Runner::Stop() {
    if (!thread_) return;
    stopRequested_ = true;
    if (paused_) {
        cmd_ = Cmd::Stop;
        SetEvent(cmdEvent_);
        return;
    }
    // In corsa: sospendi il thread e, se è dentro il codice dello snippet,
    // spostalo su AbortThunk. Se è dentro una funzione dell'app riprova poco dopo.
    for (int i = 0; i < 50 && state_ == State::Running; ++i) {
        SuspendThread(thread_);
        CONTEXT c{};
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_DEBUG_REGISTERS;
        if (GetThreadContext(thread_, &c) && InImage(c.Rip)) {
            RedirectToAbort(&c, L"Esecuzione fermata.");
            SetThreadContext(thread_, &c);
            ResumeThread(thread_);
            return;
        }
        ResumeThread(thread_);
        Sleep(20);
    }
}

void Runner::Join() {
    if (!thread_) return;
    WaitForSingleObject(thread_, 2000);
    CloseHandle(thread_);
    thread_ = nullptr;
    tid_ = 0;
}

int Runner::LineForPc(uint64_t pc) const {
    if (!lines_ || !InImage(pc)) return 0;
    char file[100] = {};
    int line = regiscope_line_for_pc(lines_, textBase_, pc, file, sizeof(file));
    return strcmp(file, "<string>") == 0 ? line : 0;
}

std::string Runner::LinesDebug(uint64_t pc) const {
    char buf[400] = {};
    regiscope_lines_debug(lines_, textBase_, pc, buf, sizeof(buf));
    char extra[120];
    snprintf(extra, sizeof(extra), " img=%llx-%llx", (unsigned long long)imgLo_, (unsigned long long)imgHi_);
    return std::string(buf) + extra;
}

void Runner::TakeSnapshots(Snapshot &cur, Snapshot &prev) {
    std::lock_guard<std::mutex> lock(snapMx_);
    cur = cur_;
    prev = prev_;
}
