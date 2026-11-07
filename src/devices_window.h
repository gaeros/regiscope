#pragma once
// Finestra "Periferiche" (fotocamera, posizione, microfono, batteria, sensori).
#include <windows.h>

#include "panels.h"

// Crea la finestra la prima volta e la mostra.
void ShowDevicesWindow(HWND owner, HINSTANCE inst, const PanelFonts &fonts, HICON icon, bool activate = true);
bool DevicesWindowVisible();
