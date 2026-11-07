#pragma once
// Informazioni sull'hardware: banchi di RAM (WMI/SMBIOS) e CPU (CPUID).
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

struct MemModule {
    std::wstring locator;      // es. "DIMM A1" o "ChannelA-DIMM0"
    std::wstring bank;
    std::wstring manufacturer;
    std::wstring partNumber;
    uint64_t capacity = 0;     // byte
    uint32_t speed = 0;        // MT/s
    std::wstring type;         // "DDR4", "DDR5", ...
};

struct MemInfo {
    bool ok = false;
    std::wstring error;
    std::vector<MemModule> modules;
    int totalSlots = 0;        // slot sulla scheda madre (anche vuoti), 0 = sconosciuto
};

struct CpuInfo {
    std::wstring brand;
    int cores = 0;
    int logical = 0;
    uint32_t l1d = 0, l1i = 0, l2 = 0, l3 = 0; // KB per istanza
};

// Lenta (WMI): chiamarla da un thread di lavoro.
MemInfo QueryMemoryModules();
CpuInfo QueryCpu();
