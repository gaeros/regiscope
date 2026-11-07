#include "sysinfo.h"

#include <cstring>
#include <intrin.h>
#include <wbemidl.h>

namespace {

std::wstring VarString(const VARIANT &v) {
    if (v.vt == VT_BSTR && v.bstrVal) return v.bstrVal;
    return {};
}

uint64_t VarU64(const VARIANT &v) {
    switch (v.vt) {
    case VT_BSTR: return v.bstrVal ? _wcstoui64(v.bstrVal, nullptr, 10) : 0;
    case VT_I4: return (uint32_t)v.lVal;
    case VT_UI4: return v.ulVal;
    case VT_I2: return (uint16_t)v.iVal;
    case VT_UI2: return v.uiVal;
    case VT_I8: return (uint64_t)v.llVal;
    case VT_UI8: return v.ullVal;
    default: return 0;
    }
}

std::wstring Trim(std::wstring s) {
    while (!s.empty() && (s.back() == L' ' || s.back() == L'\0')) s.pop_back();
    size_t i = 0;
    while (i < s.size() && s[i] == L' ') ++i;
    return s.substr(i);
}

std::wstring MemoryTypeName(uint64_t smbiosType) {
    switch (smbiosType) {
    case 20: return L"DDR";
    case 21: return L"DDR2";
    case 24: return L"DDR3";
    case 26: return L"DDR4";
    case 27: return L"LPDDR";
    case 28: return L"LPDDR2";
    case 29: return L"LPDDR3";
    case 30: return L"LPDDR4";
    case 34: return L"DDR5";
    case 35: return L"LPDDR5";
    default: return L"";
    }
}

template <typename Fn>
void RunQuery(IWbemServices *svc, const wchar_t *query, Fn &&fn) {
    BSTR lang = SysAllocString(L"WQL");
    BSTR q = SysAllocString(query);
    IEnumWbemClassObject *en = nullptr;
    HRESULT hr = svc->ExecQuery(lang, q, WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY, nullptr, &en);
    SysFreeString(lang);
    SysFreeString(q);
    if (FAILED(hr) || !en) return;
    IWbemClassObject *obj = nullptr;
    ULONG got = 0;
    while (en->Next(WBEM_INFINITE, 1, &obj, &got) == S_OK && got) {
        fn(obj);
        obj->Release();
    }
    en->Release();
}

template <typename Fn>
void WithProp(IWbemClassObject *o, const wchar_t *name, Fn &&fn) {
    VARIANT v;
    VariantInit(&v);
    if (SUCCEEDED(o->Get(name, 0, &v, nullptr, nullptr))) fn(v);
    VariantClear(&v);
}

} // namespace

MemInfo QueryMemoryModules() {
    MemInfo info;
    HRESULT hrInit = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    IWbemLocator *loc = nullptr;
    IWbemServices *svc = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                                  reinterpret_cast<void **>(&loc));
    if (SUCCEEDED(hr)) {
        BSTR ns = SysAllocString(L"ROOT\\CIMV2");
        hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
        SysFreeString(ns);
    }
    if (SUCCEEDED(hr) && svc) {
        CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL,
                          RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);

        RunQuery(svc, L"SELECT * FROM Win32_PhysicalMemory", [&](IWbemClassObject *o) {
            MemModule m;
            WithProp(o, L"DeviceLocator", [&](VARIANT &v) { m.locator = Trim(VarString(v)); });
            WithProp(o, L"BankLabel", [&](VARIANT &v) { m.bank = Trim(VarString(v)); });
            WithProp(o, L"Manufacturer", [&](VARIANT &v) { m.manufacturer = Trim(VarString(v)); });
            WithProp(o, L"PartNumber", [&](VARIANT &v) { m.partNumber = Trim(VarString(v)); });
            WithProp(o, L"Capacity", [&](VARIANT &v) { m.capacity = VarU64(v); });
            WithProp(o, L"ConfiguredClockSpeed", [&](VARIANT &v) { m.speed = (uint32_t)VarU64(v); });
            if (!m.speed) WithProp(o, L"Speed", [&](VARIANT &v) { m.speed = (uint32_t)VarU64(v); });
            WithProp(o, L"SMBIOSMemoryType", [&](VARIANT &v) { m.type = MemoryTypeName(VarU64(v)); });
            info.modules.push_back(std::move(m));
        });
        RunQuery(svc, L"SELECT MemoryDevices FROM Win32_PhysicalMemoryArray", [&](IWbemClassObject *o) {
            WithProp(o, L"MemoryDevices", [&](VARIANT &v) { info.totalSlots += (int)VarU64(v); });
        });
        info.ok = true;
    } else {
        info.error = L"WMI non disponibile: impossibile leggere i banchi di RAM.";
    }
    if (svc) svc->Release();
    if (loc) loc->Release();
    if (SUCCEEDED(hrInit)) CoUninitialize();
    if ((int)info.modules.size() > info.totalSlots) info.totalSlots = (int)info.modules.size();
    return info;
}

CpuInfo QueryCpu() {
    CpuInfo info;

    int regs[4] = {};
    __cpuid(regs, 0x80000000);
    if ((unsigned)regs[0] >= 0x80000004u) {
        char brand[49] = {};
        for (int i = 0; i < 3; ++i) {
            __cpuid(regs, 0x80000002 + i);
            memcpy(brand + 16 * i, regs, sizeof(regs));
        }
        int n = MultiByteToWideChar(CP_ACP, 0, brand, -1, nullptr, 0);
        std::wstring w(n, L'\0');
        MultiByteToWideChar(CP_ACP, 0, brand, -1, &w[0], n);
        info.brand = Trim(w);
    }

    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> buf(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    if (GetLogicalProcessorInformation(buf.data(), &len)) {
        size_t count = len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION);
        for (size_t i = 0; i < count; ++i) {
            const auto &e = buf[i];
            if (e.Relationship == RelationProcessorCore) {
                ++info.cores;
                for (ULONG_PTR m = e.ProcessorMask; m; m &= m - 1) ++info.logical;
            } else if (e.Relationship == RelationCache) {
                uint32_t kb = e.Cache.Size / 1024;
                if (e.Cache.Level == 1 && e.Cache.Type == CacheData && !info.l1d) info.l1d = kb;
                if (e.Cache.Level == 1 && e.Cache.Type == CacheInstruction && !info.l1i) info.l1i = kb;
                if (e.Cache.Level == 2 && !info.l2) info.l2 = kb;
                if (e.Cache.Level == 3 && !info.l3) info.l3 = kb;
            }
        }
    }
    return info;
}
