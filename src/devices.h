#pragma once
// Periferiche del PC: fotocamera, posizione, microfono, batteria e sensori.
// Le usano sia la finestra "Periferiche" sia gli snippet (camera_scatta, ...).
// Ogni operazione sull'hardware gira in un thread a parte, così le eccezioni
// interne di Windows non disturbano il passo passo dello snippet.
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>

#include "snippet_runtime.h"

namespace dev {

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> rgb; // 3 byte per pixel: rosso, verde, blu
    std::wstring source;      // da dove arriva (anteprima, snippet, ...)
};

struct Position {
    bool valid = false;
    double lat = 0, lon = 0, accuracy = 0; // gradi, gradi, metri
    std::wstring message;                  // errore o stato
};

struct Audio {
    bool valid = false;
    int rate = 16000;
    std::vector<int16_t> samples;
    std::wstring message;
};

struct Battery {
    bool valid = false;
    bool hasBattery = false;
    int percent = -1;
    bool charging = false;
    bool onAc = false;
};

struct Sensors {
    bool valid = false;
    bool hasLight = false;
    double lux = 0;
    bool hasAccel = false;
    double ax = 0, ay = 0, az = 0; // in g (1 g = gravità terrestre)
    std::wstring message;
};

// Stato mostrato nella finestra Periferiche.
struct State {
    Image image;
    std::wstring cameraName;
    std::wstring cameraMessage;
    bool previewOn = false;
    Position position;
    bool positionBusy = false;
    Audio audio;
    bool audioBusy = false;
    DWORD recordStart = 0; // GetTickCount() all'inizio della registrazione
    int recordMs = 0;      // durata prevista
    bool playing = false;
    Battery battery;
    Sensors sensors;
};

// notify riceve WM_APP_DEVICES_CHANGED quando lo stato cambia,
// main riceve WM_APP_SHOW_DEVICES quando uno snippet usa una periferica.
void Init(HWND main);
void SetWindow(HWND notify);
void Shutdown();

State GetState();

// Azioni (non bloccanti) chiamate dai pulsanti della finestra.
void TogglePreview();
void TakePhotoAsync();
void UpdatePositionAsync();
void RecordAsync(int milliseconds);
void PlayLastRecording();
void UpdateBatteryAndSensorsAsync();

// Funzioni C offerte agli snippet.
const std::vector<snippet_rt::Symbol> &Symbols();

} // namespace dev
