#pragma once
#include <windows.h>

// Messaggi che i thread di lavoro inviano alla finestra principale.
constexpr UINT WM_APP_OUTPUT   = WM_APP + 1; // lParam = std::wstring* (lo libera chi riceve)
constexpr UINT WM_APP_SNAPSHOT = WM_APP + 2; // nuova fotografia dei registri disponibile
constexpr UINT WM_APP_FINISHED = WM_APP + 3; // wParam = codice di uscita, lParam = std::wstring*
constexpr UINT WM_APP_MEMINFO  = WM_APP + 4; // lParam = MemInfo* (lo libera chi riceve)
constexpr UINT WM_APP_DEVICES_CHANGED = WM_APP + 5; // la finestra Periferiche deve ridisegnarsi
constexpr UINT WM_APP_SHOW_DEVICES    = WM_APP + 6; // uno snippet usa una periferica: mostra la finestra
