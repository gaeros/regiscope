#include "devices.h"
#include "messages.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mmsystem.h>
#include <objbase.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>

namespace dev {
namespace {

HWND g_main = nullptr;
HWND g_window = nullptr;
std::mutex g_stateMx;
State g_state;

void Notify() {
    HWND w = g_window;
    if (w) PostMessageW(w, WM_APP_DEVICES_CHANGED, 0, 0);
}

void ShowWindowFromSnippet() {
    if (g_main) PostMessageW(g_main, WM_APP_SHOW_DEVICES, 0, 0);
}

std::string WideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

// Esegue f in un thread nuovo (con COM inizializzato) e aspetta che finisca.
void RunIsolated(const std::function<void()> &f) {
    struct Ctx { const std::function<void()> *f; };
    Ctx ctx{&f};
    HANDLE t = CreateThread(
        nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto *c = static_cast<Ctx *>(p);
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            (*c->f)();
            if (SUCCEEDED(hr)) CoUninitialize();
            return 0;
        },
        &ctx, 0, nullptr);
    if (!t) return;
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}

// Esegue f in un thread a parte senza aspettare (per i pulsanti della finestra).
void RunDetached(std::function<void()> f) {
    auto *heapF = new std::function<void()>(std::move(f));
    HANDLE t = CreateThread(
        nullptr, 0,
        [](LPVOID p) -> DWORD {
            auto *fn = static_cast<std::function<void()> *>(p);
            HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
            (*fn)();
            if (SUCCEEDED(hr)) CoUninitialize();
            delete fn;
            return 0;
        },
        heapF, 0, nullptr);
    if (t) CloseHandle(t);
    else delete heapF;
}

template <class T> void SafeRelease(T *&p) {
    if (p) p->Release();
    p = nullptr;
}

// ------------------------------------------------------------------ fotocamera
// Media Foundation: il "source reader" converte i fotogrammi della webcam in RGB32.

std::mutex g_camMx;
IMFSourceReader *g_reader = nullptr;
int g_camW = 0, g_camH = 0;
bool g_mfStarted = false;
int g_framesRead = 0;

bool CameraOpenLocked(std::wstring &err) {
    if (g_reader) return true;
    if (!g_mfStarted) {
        if (FAILED(MFStartup(MF_VERSION, MFSTARTUP_FULL))) {
            err = L"Media Foundation non disponibile su questo PC.";
            return false;
        }
        g_mfStarted = true;
    }
    IMFAttributes *attr = nullptr;
    IMFActivate **devices = nullptr;
    UINT32 count = 0;
    HRESULT hr = MFCreateAttributes(&attr, 1);
    if (SUCCEEDED(hr))
        hr = attr->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
    if (SUCCEEDED(hr)) hr = MFEnumDeviceSources(attr, &devices, &count);
    SafeRelease(attr);
    if (FAILED(hr) || count == 0) {
        err = L"Nessuna fotocamera trovata.";
        if (devices) CoTaskMemFree(devices);
        return false;
    }

    IMFMediaSource *source = nullptr;
    wchar_t *name = nullptr;
    UINT32 nameLen = 0;
    if (SUCCEEDED(devices[0]->GetAllocatedString(MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &nameLen))) {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.cameraName = name;
        CoTaskMemFree(name);
    }
    hr = devices[0]->ActivateObject(IID_PPV_ARGS(&source));
    for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
    CoTaskMemFree(devices);
    if (FAILED(hr)) {
        err = hr == E_ACCESSDENIED
                  ? L"Accesso alla fotocamera negato: Impostazioni → Privacy e sicurezza → Fotocamera."
                  : L"Impossibile aprire la fotocamera (forse è usata da un'altra app).";
        return false;
    }

    IMFAttributes *readerAttr = nullptr;
    MFCreateAttributes(&readerAttr, 1);
    if (readerAttr) readerAttr->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
    hr = MFCreateSourceReaderFromMediaSource(source, readerAttr, &g_reader);
    SafeRelease(readerAttr);
    source->Release();
    if (FAILED(hr)) {
        err = L"Impossibile leggere dalla fotocamera.";
        return false;
    }

    IMFMediaType *type = nullptr;
    MFCreateMediaType(&type);
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    hr = g_reader->SetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
    SafeRelease(type);
    IMFMediaType *current = nullptr;
    if (SUCCEEDED(hr)) hr = g_reader->GetCurrentMediaType((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current);
    UINT64 size = 0;
    if (SUCCEEDED(hr)) hr = current->GetUINT64(MF_MT_FRAME_SIZE, &size);
    SafeRelease(current);
    if (FAILED(hr)) {
        SafeRelease(g_reader);
        err = L"La fotocamera non fornisce immagini RGB.";
        return false;
    }
    g_camW = (int)(size >> 32);
    g_camH = (int)(size & 0xFFFFFFFF);
    g_framesRead = 0;
    return true;
}

void CameraCloseLocked() {
    SafeRelease(g_reader);
    g_camW = g_camH = 0;
}

// Legge un fotogramma (RGB, 3 byte per pixel, dall'alto in basso).
bool CameraGrabLocked(Image &out, std::wstring &err) {
    if (!CameraOpenLocked(err)) return false;
    // I primi fotogrammi di molte webcam sono scuri (l'esposizione si sta regolando).
    int skip = g_framesRead == 0 ? 5 : 0;
    for (int attempt = 0; attempt < 60; ++attempt) {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        IMFSample *sample = nullptr;
        HRESULT hr = g_reader->ReadSample((DWORD)MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample);
        if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
            SafeRelease(sample);
            CameraCloseLocked();
            err = L"La fotocamera ha smesso di rispondere.";
            return false;
        }
        if (!sample) continue;
        ++g_framesRead;
        if (skip > 0) {
            --skip;
            sample->Release();
            continue;
        }
        IMFMediaBuffer *buf = nullptr;
        hr = sample->ConvertToContiguousBuffer(&buf);
        sample->Release();
        if (FAILED(hr)) continue;

        BYTE *scan0 = nullptr;
        LONG pitch = 0;
        IMF2DBuffer *buf2d = nullptr;
        BYTE *raw = nullptr;
        bool locked2d = false;
        if (SUCCEEDED(buf->QueryInterface(IID_PPV_ARGS(&buf2d))) && SUCCEEDED(buf2d->Lock2D(&scan0, &pitch))) {
            locked2d = true;
        } else {
            DWORD maxLen = 0, curLen = 0;
            if (FAILED(buf->Lock(&raw, &maxLen, &curLen))) {
                SafeRelease(buf2d);
                buf->Release();
                continue;
            }
            // Senza pitch esplicito RGB32 è memorizzato dal basso verso l'alto.
            pitch = -(LONG)(g_camW * 4);
            scan0 = raw + (size_t)(g_camH - 1) * g_camW * 4;
        }
        out.w = g_camW;
        out.h = g_camH;
        out.rgb.resize((size_t)g_camW * g_camH * 3);
        for (int y = 0; y < g_camH; ++y) {
            const BYTE *row = scan0 + (ptrdiff_t)y * pitch;
            uint8_t *dst = &out.rgb[(size_t)y * g_camW * 3];
            for (int x = 0; x < g_camW; ++x) {
                dst[x * 3 + 0] = row[x * 4 + 2]; // la memoria contiene B G R X
                dst[x * 3 + 1] = row[x * 4 + 1];
                dst[x * 3 + 2] = row[x * 4 + 0];
            }
        }
        if (locked2d) buf2d->Unlock2D();
        else buf->Unlock();
        SafeRelease(buf2d);
        buf->Release();
        return true;
    }
    err = L"Nessun fotogramma ricevuto dalla fotocamera.";
    return false;
}

// Ridimensiona (pixel più vicino) un'immagine RGB.
void Resize(const Image &src, int w, int h, uint8_t *dst) {
    for (int y = 0; y < h; ++y) {
        int sy = (int)((long long)y * src.h / h);
        for (int x = 0; x < w; ++x) {
            int sx = (int)((long long)x * src.w / w);
            const uint8_t *p = &src.rgb[((size_t)sy * src.w + sx) * 3];
            uint8_t *q = dst + ((size_t)y * w + x) * 3;
            q[0] = p[0];
            q[1] = p[1];
            q[2] = p[2];
        }
    }
}

void SetCameraResult(const Image *img, const std::wstring &message) {
    std::lock_guard<std::mutex> lock(g_stateMx);
    if (img) g_state.image = *img;
    g_state.cameraMessage = message;
}

HANDLE g_previewThread = nullptr;
volatile LONG g_previewRun = 0;

DWORD WINAPI PreviewThread(LPVOID) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    while (g_previewRun) {
        Image img;
        std::wstring err;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(g_camMx);
            ok = CameraGrabLocked(img, err);
        }
        if (!ok) {
            SetCameraResult(nullptr, err);
            std::lock_guard<std::mutex> lock(g_stateMx);
            g_state.previewOn = false;
            InterlockedExchange(&g_previewRun, 0);
            Notify();
            break;
        }
        img.source = L"anteprima dal vivo";
        SetCameraResult(&img, L"");
        Notify();
        Sleep(30);
    }
    CoUninitialize();
    return 0;
}

void StopPreview() {
    if (!g_previewThread) return;
    InterlockedExchange(&g_previewRun, 0);
    WaitForSingleObject(g_previewThread, 3000);
    CloseHandle(g_previewThread);
    g_previewThread = nullptr;
    std::lock_guard<std::mutex> lock(g_stateMx);
    g_state.previewOn = false;
}

// ------------------------------------------------------------------ WinRT senza intestazioni
// Posizione e sensori sono API "WinRT". Le chiamiamo direttamente dalla tabella
// dei metodi (vtable) degli oggetti, così il codice compila con qualsiasi compilatore.

struct ComBase {
    HMODULE dll = nullptr;
    HRESULT(WINAPI *activateInstance)(void *, void **) = nullptr;
    HRESULT(WINAPI *getFactory)(void *, REFIID, void **) = nullptr;
    HRESULT(WINAPI *createString)(const wchar_t *, UINT32, void **) = nullptr;
    HRESULT(WINAPI *deleteString)(void *) = nullptr;
    bool ok = false;
};

ComBase &Rt() {
    static ComBase c = [] {
        ComBase b;
        b.dll = LoadLibraryW(L"combase.dll");
        if (b.dll) {
            b.activateInstance = (decltype(b.activateInstance))GetProcAddress(b.dll, "RoActivateInstance");
            b.getFactory = (decltype(b.getFactory))GetProcAddress(b.dll, "RoGetActivationFactory");
            b.createString = (decltype(b.createString))GetProcAddress(b.dll, "WindowsCreateString");
            b.deleteString = (decltype(b.deleteString))GetProcAddress(b.dll, "WindowsDeleteString");
            b.ok = b.activateInstance && b.getFactory && b.createString && b.deleteString;
        }
        return b;
    }();
    return c;
}

// Chiama il metodo numero 'slot' della vtable (0-2 IUnknown, 3-5 IInspectable).
template <class... A> HRESULT VCall(void *obj, int slot, A... args) {
    using Fn = HRESULT(STDMETHODCALLTYPE *)(void *, A...);
    return reinterpret_cast<Fn>((*reinterpret_cast<void ***>(obj))[slot])(obj, args...);
}

void Rel(void *&p) {
    if (p) reinterpret_cast<IUnknown *>(p)->Release();
    p = nullptr;
}

struct HStr {
    void *h = nullptr;
    explicit HStr(const wchar_t *s) { Rt().createString(s, (UINT32)wcslen(s), &h); }
    ~HStr() {
        if (h) Rt().deleteString(h);
    }
};

const IID kIGeolocator = {0xa9c3bf62, 0x4524, 0x4989, {0x8a, 0xa9, 0xde, 0x01, 0x9d, 0x2e, 0x55, 0x1f}};
const IID kIAsyncInfo = {0x00000036, 0x0000, 0x0000, {0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
const IID kILightSensorStatics = {0x45db8c84, 0xc3a8, 0x471e, {0x9a, 0x53, 0x64, 0x57, 0xfa, 0xd8, 0x7c, 0x0e}};
const IID kIAccelerometerStatics = {0xa5e28b74, 0x5a87, 0x4a2d, {0xbe, 0xcc, 0x0f, 0x90, 0x6e, 0xa0, 0x61, 0xdd}};

Position ReadPositionNow(int timeoutMs) {
    Position p;
    if (!Rt().ok) {
        p.message = L"Servizi di posizione non disponibili su questo sistema.";
        return p;
    }
    void *insp = nullptr, *geo = nullptr, *op = nullptr, *info = nullptr, *pos = nullptr, *coord = nullptr;
    HStr cls(L"Windows.Devices.Geolocation.Geolocator");
    HRESULT hr = Rt().activateInstance(cls.h, &insp);
    if (SUCCEEDED(hr)) hr = reinterpret_cast<IUnknown *>(insp)->QueryInterface(kIGeolocator, &geo);
    if (SUCCEEDED(hr)) hr = VCall(geo, 13, &op); // GetGeopositionAsync
    if (SUCCEEDED(hr)) hr = reinterpret_cast<IUnknown *>(op)->QueryInterface(kIAsyncInfo, &info);
    if (FAILED(hr)) {
        p.message = L"Servizi di posizione non disponibili su questo sistema.";
    } else {
        int status = 0; // 0 in corso, 1 completata, 2 annullata, 3 errore
        for (int waited = 0; waited < timeoutMs; waited += 100) {
            VCall(info, 7, &status); // get_Status
            if (status != 0) break;
            Sleep(100);
        }
        if (status == 1 && SUCCEEDED(VCall(op, 8, &pos)) && pos && SUCCEEDED(VCall(pos, 6, &coord)) && coord) {
            VCall(coord, 6, &p.lat);      // Latitude
            VCall(coord, 7, &p.lon);      // Longitude
            VCall(coord, 9, &p.accuracy); // Accuracy (metri)
            p.valid = true;
        } else if (status == 0) {
            VCall(info, 9); // Cancel
            p.message = L"Nessuna risposta dal servizio di posizione (tempo scaduto).";
        } else {
            HRESULT code = S_OK;
            VCall(info, 8, &code); // get_ErrorCode
            if (code == E_ACCESSDENIED)
                p.message = L"Accesso alla posizione negato: Impostazioni → Privacy e sicurezza → Posizione "
                            L"(attiva anche \"Consenti alle app desktop di accedere alla posizione\").";
            else
                p.message = L"Posizione non disponibile (servizio di localizzazione spento?).";
        }
    }
    Rel(coord);
    Rel(pos);
    Rel(info);
    Rel(op);
    Rel(geo);
    Rel(insp);
    return p;
}

void *SensorDefault(const wchar_t *className, const IID &staticsIid) {
    if (!Rt().ok) return nullptr;
    void *statics = nullptr, *sensor = nullptr;
    HStr cls(className);
    if (SUCCEEDED(Rt().getFactory(cls.h, staticsIid, &statics)) && statics) VCall(statics, 6, &sensor); // GetDefault
    Rel(statics);
    return sensor;
}

Sensors ReadSensorsNow() {
    Sensors s;
    s.valid = true;
    void *light = SensorDefault(L"Windows.Devices.Sensors.LightSensor", kILightSensorStatics);
    if (light) {
        void *reading = nullptr;
        if (SUCCEEDED(VCall(light, 6, &reading)) && reading) { // GetCurrentReading
            float lux = 0;
            VCall(reading, 7, &lux); // IlluminanceInLux
            s.lux = lux;
            s.hasLight = true;
        }
        Rel(reading);
        Rel(light);
    }
    void *acc = SensorDefault(L"Windows.Devices.Sensors.Accelerometer", kIAccelerometerStatics);
    if (acc) {
        void *reading = nullptr;
        if (SUCCEEDED(VCall(acc, 6, &reading)) && reading) {
            VCall(reading, 7, &s.ax);
            VCall(reading, 8, &s.ay);
            VCall(reading, 9, &s.az);
            s.hasAccel = true;
        }
        Rel(reading);
        Rel(acc);
    }
    if (!s.hasLight && !s.hasAccel) s.message = L"Questo PC non ha sensori di luce o di movimento.";
    return s;
}

Battery ReadBatteryNow() {
    Battery b;
    SYSTEM_POWER_STATUS ps{};
    if (!GetSystemPowerStatus(&ps)) return b;
    b.valid = true;
    b.hasBattery = !(ps.BatteryFlag & 128) && ps.BatteryFlag != 255;
    b.percent = ps.BatteryLifePercent == 255 ? -1 : ps.BatteryLifePercent;
    b.charging = (ps.BatteryFlag & 8) != 0;
    b.onAc = ps.ACLineStatus == 1;
    return b;
}

// ------------------------------------------------------------------ microfono

Audio RecordNow(int count, int rate) {
    Audio a;
    a.rate = rate;
    if (waveInGetNumDevs() == 0) {
        a.message = L"Nessun microfono trovato.";
        return a;
    }
    WAVEFORMATEX fmt{};
    fmt.wFormatTag = WAVE_FORMAT_PCM;
    fmt.nChannels = 1;
    fmt.nSamplesPerSec = (DWORD)rate;
    fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 2;
    fmt.nAvgBytesPerSec = (DWORD)rate * 2;
    HANDLE done = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HWAVEIN in = nullptr;
    MMRESULT r = waveInOpen(&in, WAVE_MAPPER, &fmt, (DWORD_PTR)done, 0, CALLBACK_EVENT);
    if (r != MMSYSERR_NOERROR) {
        CloseHandle(done);
        a.message = r == MMSYSERR_ALLOCATED ? L"Il microfono è già in uso." : L"Impossibile aprire il microfono "
                                                                              L"(controlla Impostazioni → Privacy "
                                                                              L"e sicurezza → Microfono).";
        return a;
    }
    a.samples.assign((size_t)count, 0);
    WAVEHDR hdr{};
    hdr.lpData = reinterpret_cast<LPSTR>(a.samples.data());
    hdr.dwBufferLength = (DWORD)(count * 2);
    waveInPrepareHeader(in, &hdr, sizeof(hdr));
    waveInAddBuffer(in, &hdr, sizeof(hdr));
    waveInStart(in);
    DWORD limit = (DWORD)((long long)count * 1000 / rate) + 3000;
    DWORD start = GetTickCount();
    while (!(hdr.dwFlags & WHDR_DONE) && GetTickCount() - start < limit) WaitForSingleObject(done, 200);
    waveInReset(in);
    waveInUnprepareHeader(in, &hdr, sizeof(hdr));
    waveInClose(in);
    CloseHandle(done);
    size_t got = hdr.dwBytesRecorded / 2;
    if (got == 0) {
        a.samples.clear();
        a.message = L"Il microfono non ha registrato nulla (permesso negato?).";
        return a;
    }
    a.samples.resize(got);
    a.valid = true;
    return a;
}

// ------------------------------------------------------------------ altoparlante
// Costruisce un file WAV in memoria e lo fa suonare a Windows (PlaySound).

std::mutex g_playMx;
std::vector<uint8_t> g_wav;

void PlaySamples(const int16_t *samples, size_t count, int rate) {
    std::lock_guard<std::mutex> lock(g_playMx);
    PlaySoundW(nullptr, nullptr, 0); // ferma il suono precedente prima di liberarne i dati
    uint32_t dataBytes = (uint32_t)(count * 2);
    g_wav.assign(44 + dataBytes, 0);
    uint8_t *h = g_wav.data();
    auto put32 = [](uint8_t *p, uint32_t v) { memcpy(p, &v, 4); };
    auto put16 = [](uint8_t *p, uint16_t v) { memcpy(p, &v, 2); };
    memcpy(h, "RIFF", 4);
    put32(h + 4, 36 + dataBytes);
    memcpy(h + 8, "WAVEfmt ", 8);
    put32(h + 16, 16);
    put16(h + 20, 1);                 // PCM
    put16(h + 22, 1);                 // mono
    put32(h + 24, (uint32_t)rate);
    put32(h + 28, (uint32_t)rate * 2);
    put16(h + 32, 2);
    put16(h + 34, 16);
    memcpy(h + 36, "data", 4);
    put32(h + 40, dataBytes);
    if (count) memcpy(h + 44, samples, dataBytes);
    PlaySoundW(reinterpret_cast<LPCWSTR>(g_wav.data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
}

void MarkPlaying(size_t count, int rate) {
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.playing = true;
    }
    Notify();
    DWORD ms = (DWORD)((long long)count * 1000 / rate) + 200;
    RunDetached([ms] {
        Sleep(ms);
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.playing = false;
        Notify();
    });
}

// ------------------------------------------------------------------ funzioni per gli snippet

void SnippetSay(const std::wstring &msg) { snippet_rt::Print("[periferiche] " + WideToUtf8(msg) + "\n"); }

int rt_camera_apri() {
    ShowWindowFromSnippet();
    bool ok = false;
    std::wstring err;
    RunIsolated([&] {
        std::lock_guard<std::mutex> lock(g_camMx);
        ok = CameraOpenLocked(err);
    });
    if (!ok) {
        SnippetSay(err);
        SetCameraResult(nullptr, err);
        Notify();
    }
    return ok ? 1 : 0;
}

int rt_camera_scatta(unsigned char *rgb, int w, int h) {
    ShowWindowFromSnippet();
    if (!rgb || w <= 0 || h <= 0 || (long long)w * h > 4096LL * 4096) {
        SnippetSay(L"camera_scatta: dimensioni non valide.");
        return 0;
    }
    Image full;
    bool ok = false;
    std::wstring err;
    RunIsolated([&] {
        std::lock_guard<std::mutex> lock(g_camMx);
        ok = CameraGrabLocked(full, err);
    });
    if (!ok) {
        SnippetSay(err);
        SetCameraResult(nullptr, err);
        Notify();
        return 0;
    }
    Resize(full, w, h, rgb);
    Image shown;
    shown.w = w;
    shown.h = h;
    shown.rgb.assign(rgb, rgb + (size_t)w * h * 3);
    wchar_t src[96];
    swprintf(src, 96, L"foto dello snippet · %d×%d pixel", w, h);
    shown.source = src;
    SetCameraResult(&shown, L"");
    Notify();
    return 1;
}

void rt_camera_chiudi() {
    RunIsolated([&] {
        std::lock_guard<std::mutex> lock(g_camMx);
        if (!g_previewRun) CameraCloseLocked();
    });
}

void rt_mostra_immagine(const unsigned char *rgb, int w, int h) {
    ShowWindowFromSnippet();
    if (!rgb || w <= 0 || h <= 0 || (long long)w * h > 4096LL * 4096) {
        SnippetSay(L"mostra_immagine: dimensioni non valide.");
        return;
    }
    Image img;
    img.w = w;
    img.h = h;
    img.rgb.assign(rgb, rgb + (size_t)w * h * 3);
    wchar_t src[96];
    swprintf(src, 96, L"immagine dello snippet · %d×%d pixel", w, h);
    img.source = src;
    SetCameraResult(&img, L"");
    Notify();
}

int rt_posizione_leggi(double *lat, double *lon, double *precisione) {
    ShowWindowFromSnippet();
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.positionBusy = true;
    }
    Notify();
    Position p;
    RunIsolated([&] { p = ReadPositionNow(15000); });
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.position = p;
        g_state.positionBusy = false;
    }
    Notify();
    if (!p.valid) {
        SnippetSay(p.message);
        return 0;
    }
    if (lat) *lat = p.lat;
    if (lon) *lon = p.lon;
    if (precisione) *precisione = p.accuracy;
    return 1;
}

int rt_microfono_registra(short *campioni, int n) {
    ShowWindowFromSnippet();
    if (!campioni || n <= 0 || n > 16000 * 30) {
        SnippetSay(L"microfono_registra: numero di campioni non valido (massimo 30 secondi).");
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.audioBusy = true;
        g_state.recordStart = GetTickCount();
        g_state.recordMs = (int)((long long)n * 1000 / 16000);
    }
    Notify();
    Audio a;
    RunIsolated([&] { a = RecordNow(n, 16000); });
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.audio = a;
        g_state.audioBusy = false;
    }
    Notify();
    if (!a.valid) {
        SnippetSay(a.message);
        return 0;
    }
    memset(campioni, 0, (size_t)n * sizeof(short));
    memcpy(campioni, a.samples.data(), a.samples.size() * sizeof(short));
    return (int)a.samples.size();
}

void rt_altoparlante_suona(const short *campioni, int n) {
    ShowWindowFromSnippet();
    if (!campioni || n <= 0 || n > 16000 * 60) {
        SnippetSay(L"altoparlante_suona: numero di campioni non valido (massimo 60 secondi).");
        return;
    }
    PlaySamples(reinterpret_cast<const int16_t *>(campioni), (size_t)n, 16000);
    MarkPlaying((size_t)n, 16000);
}

int rt_batteria_leggi(int *percentuale, int *in_carica) {
    ShowWindowFromSnippet();
    Battery b = ReadBatteryNow();
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.battery = b;
    }
    Notify();
    if (!b.valid || !b.hasBattery) {
        SnippetSay(L"Questo PC non ha una batteria (è collegato alla corrente).");
        if (percentuale) *percentuale = -1;
        if (in_carica) *in_carica = b.onAc ? 1 : 0;
        return 0;
    }
    if (percentuale) *percentuale = b.percent;
    if (in_carica) *in_carica = b.charging || b.onAc ? 1 : 0;
    return 1;
}

Sensors SensorsForSnippet() {
    ShowWindowFromSnippet();
    Sensors s;
    RunIsolated([&] { s = ReadSensorsNow(); });
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.sensors = s;
    }
    Notify();
    return s;
}

int rt_luce_leggi(double *lux) {
    Sensors s = SensorsForSnippet();
    if (!s.hasLight) {
        SnippetSay(L"Questo PC non ha un sensore di luce.");
        return 0;
    }
    if (lux) *lux = s.lux;
    return 1;
}

int rt_accelerometro_leggi(double *x, double *y, double *z) {
    Sensors s = SensorsForSnippet();
    if (!s.hasAccel) {
        SnippetSay(L"Questo PC non ha un accelerometro.");
        return 0;
    }
    if (x) *x = s.ax;
    if (y) *y = s.ay;
    if (z) *z = s.az;
    return 1;
}

} // namespace

void Init(HWND main) { g_main = main; }

void SetWindow(HWND notify) { g_window = notify; }

void Shutdown() {
    StopPreview();
    std::lock_guard<std::mutex> lock(g_camMx);
    CameraCloseLocked();
}

State GetState() {
    std::lock_guard<std::mutex> lock(g_stateMx);
    return g_state;
}

void TogglePreview() {
    if (g_previewThread && !g_previewRun) StopPreview(); // si era fermata da sola (errore)
    if (g_previewThread) {
        StopPreview();
        RunDetached([] {
            std::lock_guard<std::mutex> lock(g_camMx);
            CameraCloseLocked();
        });
        Notify();
        return;
    }
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.previewOn = true;
        g_state.cameraMessage = L"Accensione della fotocamera…";
    }
    InterlockedExchange(&g_previewRun, 1);
    g_previewThread = CreateThread(nullptr, 0, PreviewThread, nullptr, 0, nullptr);
    Notify();
}

void TakePhotoAsync() {
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.cameraMessage = L"Scatto…";
    }
    Notify();
    RunDetached([] {
        Image img;
        std::wstring err;
        bool ok;
        {
            std::lock_guard<std::mutex> lock(g_camMx);
            ok = CameraGrabLocked(img, err);
            if (!g_previewRun) CameraCloseLocked();
        }
        if (ok) {
            wchar_t src[96];
            swprintf(src, 96, L"foto scattata · %d×%d pixel", img.w, img.h);
            img.source = src;
        }
        SetCameraResult(ok ? &img : nullptr, ok ? L"" : err);
        Notify();
    });
}

void UpdatePositionAsync() {
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        if (g_state.positionBusy) return;
        g_state.positionBusy = true;
    }
    Notify();
    RunDetached([] {
        Position p = ReadPositionNow(15000);
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.position = p;
        g_state.positionBusy = false;
        Notify();
    });
}

void RecordAsync(int milliseconds) {
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        if (g_state.audioBusy) return;
        g_state.audioBusy = true;
        g_state.recordStart = GetTickCount();
        g_state.recordMs = milliseconds;
    }
    Notify();
    RunDetached([milliseconds] {
        Audio a = RecordNow(16000 * milliseconds / 1000, 16000);
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.audio = a;
        g_state.audioBusy = false;
        Notify();
    });
}

void PlayLastRecording() {
    Audio a;
    {
        std::lock_guard<std::mutex> lock(g_stateMx);
        if (!g_state.audio.valid || g_state.audioBusy) return;
        a = g_state.audio;
    }
    PlaySamples(a.samples.data(), a.samples.size(), a.rate);
    MarkPlaying(a.samples.size(), a.rate);
}

void UpdateBatteryAndSensorsAsync() {
    RunDetached([] {
        Battery b = ReadBatteryNow();
        Sensors s = ReadSensorsNow();
        std::lock_guard<std::mutex> lock(g_stateMx);
        g_state.battery = b;
        g_state.sensors = s;
        Notify();
    });
}

const std::vector<snippet_rt::Symbol> &Symbols() {
    static const std::vector<snippet_rt::Symbol> table = {
        {"camera_apri", (const void *)&rt_camera_apri},
        {"camera_scatta", (const void *)&rt_camera_scatta},
        {"camera_chiudi", (const void *)&rt_camera_chiudi},
        {"mostra_immagine", (const void *)&rt_mostra_immagine},
        {"posizione_leggi", (const void *)&rt_posizione_leggi},
        {"microfono_registra", (const void *)&rt_microfono_registra},
        {"altoparlante_suona", (const void *)&rt_altoparlante_suona},
        {"batteria_leggi", (const void *)&rt_batteria_leggi},
        {"luce_leggi", (const void *)&rt_luce_leggi},
        {"accelerometro_leggi", (const void *)&rt_accelerometro_leggi},
    };
    return table;
}

} // namespace dev
