#pragma once
// Tema chiaro: sfondo bianco, testo scuro, un solo colore d'accento per i bit cambiati.
#include <windows.h>

namespace theme {
constexpr COLORREF window     = RGB(0xF4, 0xF4, 0xF2);
constexpr COLORREF panel      = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF border     = RGB(0xD6, 0xD6, 0xD0);
constexpr COLORREF text       = RGB(0x1F, 0x1F, 0x1F);
constexpr COLORREF quiet      = RGB(0x6B, 0x6B, 0x6B);
constexpr COLORREF bitOne     = RGB(0x3C, 0x40, 0x43);
constexpr COLORREF bitZero    = RGB(0xFF, 0xFF, 0xFF);
constexpr COLORREF bitGrid    = RGB(0xCF, 0xD3, 0xD8);
constexpr COLORREF accent     = RGB(0x2F, 0x6F, 0xDE);
constexpr COLORREF accentSoft = RGB(0xDC, 0xE8, 0xFB);
constexpr COLORREF barTrack   = RGB(0xEC, 0xEC, 0xE8);
constexpr COLORREF gutter     = RGB(0xF3, 0xF3, 0xF0);
constexpr COLORREF step       = RGB(0x2E, 0x9D, 0x4F); // verde: passo passo attivo
constexpr COLORREF stepDark   = RGB(0x23, 0x7D, 0x3E);
constexpr COLORREF buttonDown = RGB(0xE6, 0xE6, 0xE2);
constexpr COLORREF disabled   = RGB(0xA8, 0xA8, 0xA8);
} // namespace theme
