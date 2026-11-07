#pragma once
// Compila uno snippet C con TinyCC e lo esegue in un thread separato,
// fermandolo dopo ogni istruzione macchina (trap flag della CPU).
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "snippet_runtime.h"

struct TCCState;
struct RegiScopeLines;

// Fotografia dello stato dopo un'istruzione.
struct Snapshot {
    bool valid = false;
    CONTEXT ctx{};            // registri della CPU
    uint8_t code[16]{};       // byte della prossima istruzione (per il disassemblatore)
    size_t codeLen = 0;
    uint64_t steps = 0;       // istruzioni eseguite finora
    uint64_t stackBase = 0;   // indirizzo del primo byte di stack copiato (= RSP)
    std::vector<uint8_t> stack;
    std::vector<HeapBlock> heap;
};

class Runner {
public:
    enum class State { Idle, Ready, Paused, Running, Finished };

    Runner(HWND notify, std::wstring exeDir);
    ~Runner();

    // Compila il sorgente. In caso di errore 'log' contiene i messaggi di TinyCC.
    bool Compile(const std::string &source, std::wstring &log);

    // Avvia lo snippet compilato. stepping = fermati alla prima istruzione di main.
    bool Start(bool stepping);
    void StepOnce();      // da Paused: esegui una istruzione
    void ContinueRun();   // da Paused: prosegui senza fermarti
    void Stop();          // ferma l'esecuzione (in pausa o in corsa)
    void Join();          // dopo WM_APP_FINISHED: chiude il thread

    State GetState() const { return state_.load(); }
    // Riga dello snippet (da 1) che contiene l'indirizzo, o 0 se fuori dallo snippet.
    int LineForPc(uint64_t pc) const;
    std::string LinesDebug(uint64_t pc) const;
    void TakeSnapshots(Snapshot &cur, Snapshot &prev);

    // Usati dal gestore di eccezioni e dal thread dello snippet.
    DWORD ThreadId() const { return tid_; }
    bool InImage(uint64_t addr) const { return addr >= imgLo_ && addr < imgHi_; }
    void PauseHere(CONTEXT *c);
    void RedirectToAbort(CONTEXT *c, const std::wstring &message);
    void FinishFromThread(int code, const std::wstring &message);
    std::wstring TakeAbortMessage();
    bool StopRequested() const { return stopRequested_.load(); }
    int CallEntry() { return entry_(); }

private:
    enum class Cmd { None, Step, Continue, Stop };

    void FreeCompiled();

    HWND notify_;
    std::wstring exeDir_;
    TCCState *tcc_ = nullptr;
    RegiScopeLines *lines_ = nullptr;
    uint64_t textBase_ = 0;
    int (*entry_)() = nullptr;
    uint64_t imgLo_ = 0, imgHi_ = 0;

    HANDLE thread_ = nullptr;
    DWORD tid_ = 0;
    HANDLE cmdEvent_ = nullptr;
    std::atomic<Cmd> cmd_{Cmd::None};
    std::atomic<State> state_{State::Idle};
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> paused_{false};
    uint64_t steps_ = 0;

    std::mutex snapMx_;
    Snapshot cur_, prev_;
    std::mutex abortMx_;
    std::wstring abortMsg_;
};
