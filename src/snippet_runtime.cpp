#include "snippet_runtime.h"
#include "messages.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

namespace {

HWND g_out = nullptr;
HANDLE g_heap = nullptr;
std::mutex g_mx;
std::vector<HeapBlock> g_blocks;

std::wstring Utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

void Emit(const std::string &text) {
    std::wstring w;
    for (wchar_t c : Utf8ToWide(text)) {
        if (c == L'\n') w += L'\r';
        w += c;
    }
    auto *msg = new std::wstring(std::move(w));
    if (!g_out || !PostMessageW(g_out, WM_APP_OUTPUT, 0, (LPARAM)msg)) delete msg;
}

int rt_vprintf(const char *fmt, va_list ap) {
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(nullptr, 0, fmt, ap2);
    va_end(ap2);
    if (n < 0) return n;
    std::string buf((size_t)n + 1, '\0');
    vsnprintf(&buf[0], buf.size(), fmt, ap);
    buf.resize((size_t)n);
    Emit(buf);
    return n;
}

int rt_printf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = rt_vprintf(fmt, ap);
    va_end(ap);
    return n;
}
int rt_puts(const char *s) { Emit(std::string(s) + "\n"); return 0; }
int rt_putchar(int c) { Emit(std::string(1, (char)c)); return c; }
int rt_sprintf(char *buf, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsprintf(buf, fmt, ap);
    va_end(ap);
    return n;
}
int rt_snprintf(char *buf, size_t size, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, size, fmt, ap);
    va_end(ap);
    return n;
}

void Track(void *p, size_t n) {
    std::lock_guard<std::mutex> lock(g_mx);
    HeapBlock b;
    b.addr = (uint64_t)p;
    b.size = n;
    g_blocks.push_back(std::move(b));
}

// Segna come liberato il blocco che inizia a p. Restituisce la sua dimensione, o -1.
long long Untrack(void *p) {
    std::lock_guard<std::mutex> lock(g_mx);
    for (auto it = g_blocks.rbegin(); it != g_blocks.rend(); ++it) {
        if (it->addr == (uint64_t)p && !it->freed) {
            it->freed = true;
            return (long long)it->size;
        }
    }
    return -1;
}

void *rt_malloc(size_t n) {
    void *p = HeapAlloc(g_heap, 0, n ? n : 1);
    if (p) Track(p, n);
    return p;
}
void *rt_calloc(size_t count, size_t n) {
    if (n && count > SIZE_MAX / n) return nullptr;
    size_t total = count * n;
    void *p = HeapAlloc(g_heap, HEAP_ZERO_MEMORY, total ? total : 1);
    if (p) Track(p, total);
    return p;
}
void rt_free(void *p) {
    if (!p) return;
    if (Untrack(p) < 0) {
        Emit("[RegiScope] free() su un puntatore non valido o gia' liberato: ignorato.\n");
        return;
    }
    HeapFree(g_heap, 0, p);
}
void *rt_realloc(void *p, size_t n) {
    if (!p) return rt_malloc(n);
    if (n == 0) { rt_free(p); return nullptr; }
    long long old = Untrack(p);
    if (old < 0) {
        Emit("[RegiScope] realloc() su un puntatore non valido.\n");
        return nullptr;
    }
    void *q = HeapAlloc(g_heap, 0, n);
    if (!q) return nullptr;
    memcpy(q, p, (size_t)old < n ? (size_t)old : n);
    HeapFree(g_heap, 0, p);
    Track(q, n);
    return q;
}

// Piccoli involucri: in C++ alcune di queste funzioni sono sovraccaricate
// e il loro indirizzo sarebbe ambiguo.
int rt_abs(int x) { return x < 0 ? -x : x; }
long rt_labs(long x) { return x < 0 ? -x : x; }
int rt_atoi(const char *s) { return atoi(s); }
unsigned g_seed = 1;
int rt_rand(void) { g_seed = g_seed * 1103515245u + 12345u; return (int)((g_seed >> 16) & 0x7fff); }
void rt_srand(unsigned s) { g_seed = s; }
void *rt_memset(void *d, int c, size_t n) { return memset(d, c, n); }
void *rt_memcpy(void *d, const void *s, size_t n) { return memcpy(d, s, n); }
void *rt_memmove(void *d, const void *s, size_t n) { return memmove(d, s, n); }
int rt_memcmp(const void *a, const void *b, size_t n) { return memcmp(a, b, n); }
size_t rt_strlen(const char *s) { return strlen(s); }
char *rt_strcpy(char *d, const char *s) { return strcpy(d, s); }
char *rt_strncpy(char *d, const char *s, size_t n) { return strncpy(d, s, n); }
int rt_strcmp(const char *a, const char *b) { return strcmp(a, b); }
int rt_strncmp(const char *a, const char *b, size_t n) { return strncmp(a, b, n); }
char *rt_strcat(char *d, const char *s) { return strcat(d, s); }
char *rt_strchr(const char *s, int c) { return (char *)strchr(s, c); }

} // namespace

namespace snippet_rt {

void Reset(HWND outputWindow) {
    std::lock_guard<std::mutex> lock(g_mx);
    g_out = outputWindow;
    if (g_heap) HeapDestroy(g_heap);
    g_heap = HeapCreate(0, 0, 0);
    g_blocks.clear();
    g_seed = 1;
}

std::vector<HeapBlock> SnapshotHeap(size_t maxBytes) {
    std::lock_guard<std::mutex> lock(g_mx);
    std::vector<HeapBlock> out = g_blocks;
    for (auto &b : out) {
        if (b.freed) continue;
        size_t n = b.size < maxBytes ? (size_t)b.size : maxBytes;
        b.bytes.assign((const uint8_t *)b.addr, (const uint8_t *)b.addr + n);
    }
    return out;
}

const std::vector<Symbol> &Symbols() {
    static const std::vector<Symbol> table = {
        {"printf", (const void *)&rt_printf},   {"puts", (const void *)&rt_puts},
        {"putchar", (const void *)&rt_putchar}, {"sprintf", (const void *)&rt_sprintf},
        {"snprintf", (const void *)&rt_snprintf},
        {"malloc", (const void *)&rt_malloc},   {"calloc", (const void *)&rt_calloc},
        {"realloc", (const void *)&rt_realloc}, {"free", (const void *)&rt_free},
        {"abs", (const void *)&rt_abs},         {"labs", (const void *)&rt_labs},
        {"atoi", (const void *)&rt_atoi},       {"rand", (const void *)&rt_rand},
        {"srand", (const void *)&rt_srand},
        {"memset", (const void *)&rt_memset},   {"memcpy", (const void *)&rt_memcpy},
        {"memmove", (const void *)&rt_memmove}, {"memcmp", (const void *)&rt_memcmp},
        {"strlen", (const void *)&rt_strlen},   {"strcpy", (const void *)&rt_strcpy},
        {"strncpy", (const void *)&rt_strncpy}, {"strcmp", (const void *)&rt_strcmp},
        {"strncmp", (const void *)&rt_strncmp}, {"strcat", (const void *)&rt_strcat},
        {"strchr", (const void *)&rt_strchr},
    };
    return table;
}

} // namespace snippet_rt
