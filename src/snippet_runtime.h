#pragma once
// Funzioni di libreria offerte agli snippet (printf, malloc, ...).
// printf manda il testo al pannello Output; malloc/free vengono tracciati.
#include <windows.h>
#include <cstdint>
#include <vector>

struct HeapBlock {
    uint64_t addr = 0;
    uint64_t size = 0;
    bool freed = false;
    std::vector<uint8_t> bytes; // primi byte del blocco al momento della fotografia
};

namespace snippet_rt {

struct Symbol { const char *name; const void *addr; };

// Da chiamare prima di ogni esecuzione: svuota l'heap dello snippet.
void Reset(HWND outputWindow);

// Copia lo stato dei blocchi malloc (al massimo maxBytes byte per blocco).
std::vector<HeapBlock> SnapshotHeap(size_t maxBytes);

// Tabella nome -> funzione da registrare in TinyCC.
const std::vector<Symbol> &Symbols();

} // namespace snippet_rt
