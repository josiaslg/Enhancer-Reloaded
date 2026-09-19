// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// EnhancerAPO.cpp - Windows Audio Processing Object (MFX/SFX) wrapping EnhancerDSP.
// Registered via registry only (no INF, no signing): see register.ps1.
// Parameters are read from HKLM\SOFTWARE\EnhancerAPO by a low-priority polling thread,
// so a separate UI process can change them live.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define _CRT_SECURE_NO_WARNINGS
#include <windows.h>
#include <olectl.h>
#include <initguid.h>
#include <objbase.h>
#include <mmreg.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <atomic>
#include <cstdio>
#include <new>
#include "EnhancerDSP.h"

// {8E7C1D3A-5B4F-4E2A-9C6D-3F1A2B4C5D6E}
DEFINE_GUID(CLSID_EnhancerAPO, 0x8e7c1d3a, 0x5b4f, 0x4e2a, 0x9c, 0x6d, 0x3f, 0x1a, 0x2b, 0x4c, 0x5d, 0x6e);
static const wchar_t* CLSID_STR = L"{8E7C1D3A-5B4F-4E2A-9C6D-3F1A2B4C5D6E}";
static const wchar_t* PARAM_KEY = L"SOFTWARE\\EnhancerAPO";

static HMODULE g_hModule = nullptr;
static std::atomic<long> g_objCount{0};

static void logf(const char* fmt, ...) {
#ifdef ENHANCER_LOG
    char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
    FILE* f = fopen("C:\\ProgramData\\EnhancerAPO\\log.txt", "a"); if (f) { fputs(buf, f); fputc('\n', f); fclose(f); }
#else
    (void)fmt;
#endif
}

// ---------------------------------------------------------------- parameters
static EnhancerDSP::Params readParams() {
    EnhancerDSP::Params p; // defaults = "Normal" preset
    HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return p;
    auto rd = [&](const wchar_t* name, int& dst, int lo, int hi) {
        DWORD v = 0, sz = sizeof v, type = 0;
        if (RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&v, &sz) == ERROR_SUCCESS && type == REG_DWORD)
            dst = (int)std::min<DWORD>(std::max<DWORD>(v, lo), hi);
    };
    int b = p.boost ? 1 : 0, pw = p.power ? 1 : 0;
    rd(L"Volume", p.volume, 0, 100);       rd(L"HarmBass", p.harmBass, 0, 100);
    rd(L"HarmBassRange", p.harmBassRange, 0, 100); rd(L"DrumBass", p.drumBass, 0, 100);
    rd(L"DrumBassRange", p.drumBassRange, 0, 100); rd(L"Dry", p.dry, 0, 100);
    rd(L"HarmTreble", p.harmTreble, 0, 100); rd(L"HarmTrebleRange", p.harmTrebleRange, 0, 100);
    rd(L"Ambience", p.ambience, 0, 100);   rd(L"AmbienceRange", p.ambienceRange, 0, 100);
    rd(L"Haas", p.haas, 0, 100);           rd(L"HaasDelay", p.haasDelay, 1, 40);
    rd(L"Boost", b, 0, 1); rd(L"Power", pw, 0, 1);
    p.boost = b != 0; p.power = pw != 0;
    RegCloseKey(k);
    return p;
}
static bool sameParams(const EnhancerDSP::Params& a, const EnhancerDSP::Params& b) {
    return memcmp(&a, &b, sizeof a) == 0;
}

// ---------------------------------------------------------------- the APO
class EnhancerAPO : public IAudioProcessingObject,
                    public IAudioProcessingObjectRT,
                    public IAudioProcessingObjectConfiguration,
                    public IAudioSystemEffects {
public:
    // The audio engine creates APOs through COM aggregation (a controlling outer IUnknown), so the object
    // exposes a non-delegating IUnknown ("nd") and every interface's IUnknown methods delegate to `outer`.
    // When not aggregated, outer == &nd and everything is handled internally.
    struct NonDelegating : IUnknown {
        EnhancerAPO* o = nullptr;
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { return o->InternalQI(riid, ppv); }
        STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&o->refs); }
        STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&o->refs); if (!r) delete o; return r; }
    } nd;
    IUnknown* outer;

    explicit EnhancerAPO(IUnknown* pOuter = nullptr) { nd.o = this; outer = pOuter ? pOuter : &nd; g_objCount++; InitializeSRWLock(&lock); }
    virtual ~EnhancerAPO() { stopPoll(); g_objCount--; }

    HRESULT InternalQI(REFIID riid, void** ppv) {
        if (!ppv) return E_POINTER;
        if (riid == __uuidof(IUnknown)) *ppv = static_cast<IUnknown*>(&nd);
        else if (riid == __uuidof(IAudioProcessingObject)) *ppv = static_cast<IAudioProcessingObject*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectRT)) *ppv = static_cast<IAudioProcessingObjectRT*>(this);
        else if (riid == __uuidof(IAudioProcessingObjectConfiguration)) *ppv = static_cast<IAudioProcessingObjectConfiguration*>(this);
        else if (riid == __uuidof(IAudioSystemEffects)) *ppv = static_cast<IAudioSystemEffects*>(this);
        else { *ppv = nullptr; return E_NOINTERFACE; }
        ((IUnknown*)*ppv)->AddRef();
        return S_OK;
    }
    // delegating IUnknown (all interfaces)
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { return outer->QueryInterface(riid, ppv); }
    STDMETHODIMP_(ULONG) AddRef() override { return outer->AddRef(); }
    STDMETHODIMP_(ULONG) Release() override { return outer->Release(); }

    // IAudioProcessingObject
    STDMETHODIMP Reset() override { if (locked) dsp.prepare(fs, nch); return S_OK; }
    STDMETHODIMP GetLatency(HNSTIME* pTime) override {
        if (!pTime) return E_POINTER;
        *pTime = (HNSTIME)(dsp.latencySamples() * 10000000.0 / (fs > 0 ? fs : 48000.0));
        return S_OK;
    }
    STDMETHODIMP GetRegistrationProperties(APO_REG_PROPERTIES** ppRegProps) override {
        if (!ppRegProps) return E_POINTER;
        size_t sz = sizeof(APO_REG_PROPERTIES) + 3 * sizeof(IID);
        auto* p = (APO_REG_PROPERTIES*)CoTaskMemAlloc(sz);
        if (!p) return E_OUTOFMEMORY;
        memset(p, 0, sz);
        p->clsid = CLSID_EnhancerAPO;
        p->Flags = (APO_FLAG)(APO_FLAG_INPLACE | APO_FLAG_SAMPLESPERFRAME_MUST_MATCH | APO_FLAG_FRAMESPERSECOND_MUST_MATCH | APO_FLAG_BITSPERSAMPLE_MUST_MATCH);
        wcscpy_s(p->szFriendlyName, L"Enhancer Reloaded");
        wcscpy_s(p->szCopyrightInfo, L"(c) 2026 josiaslg, BSD-2-Clause. Algorithm: Enhancer 0.17 by Adrian Iosca");
        p->u32MajorVersion = 1; p->u32MinorVersion = 0;
        p->u32MinInputConnections = 1; p->u32MaxInputConnections = 1;
        p->u32MinOutputConnections = 1; p->u32MaxOutputConnections = 1;
        p->u32MaxInstances = (UINT32)-1;
        p->u32NumAPOInterfaces = 3;
        p->iidAPOInterfaceList[0] = __uuidof(IAudioProcessingObject);
        p->iidAPOInterfaceList[1] = __uuidof(IAudioProcessingObjectRT);
        p->iidAPOInterfaceList[2] = __uuidof(IAudioProcessingObjectConfiguration);
        *ppRegProps = p;
        return S_OK;
    }
    STDMETHODIMP Initialize(UINT32 cbDataSize, BYTE* pbyData) override {
        if (cbDataSize < sizeof(APOInitBaseStruct) || !pbyData) return E_INVALIDARG;
        auto* base = (APOInitBaseStruct*)pbyData;
        if (base->clsid != CLSID_EnhancerAPO) return E_INVALIDARG;
        logf("Initialize cb=%u", cbDataSize);
        initialized = true;
        return S_OK;
    }
    STDMETHODIMP IsInputFormatSupported(IAudioMediaType* pOpposite, IAudioMediaType* pRequested, IAudioMediaType** ppSupported) override {
        HRESULT hr = checkFormat(pOpposite, pRequested, ppSupported);
        logf("IsInputFormatSupported opp=%p req=%p -> %08lx", (void*)pOpposite, (void*)pRequested, hr);
        return hr;
    }
    STDMETHODIMP IsOutputFormatSupported(IAudioMediaType* pOpposite, IAudioMediaType* pRequested, IAudioMediaType** ppSupported) override {
        HRESULT hr = checkFormat(pOpposite, pRequested, ppSupported);
        logf("IsOutputFormatSupported opp=%p req=%p -> %08lx", (void*)pOpposite, (void*)pRequested, hr);
        return hr;
    }
    STDMETHODIMP GetInputChannelCount(UINT32* pu32ChannelCount) override {
        if (!pu32ChannelCount) return E_POINTER;
        *pu32ChannelCount = nch; return S_OK;
    }

    // IAudioProcessingObjectConfiguration
    STDMETHODIMP LockForProcess(UINT32 nIn, APO_CONNECTION_DESCRIPTOR** ppIn, UINT32 nOut, APO_CONNECTION_DESCRIPTOR** ppOut) override {
        if (nIn != 1 || nOut != 1 || !ppIn || !ppOut || !ppIn[0] || !ppOut[0]) return E_INVALIDARG;
        const WAVEFORMATEX* wf = ppIn[0]->pFormat ? ppIn[0]->pFormat->GetAudioFormat() : nullptr;
        if (!wf || !isFloat32(wf)) return APOERR_FORMAT_NOT_SUPPORTED;
        fs = wf->nSamplesPerSec; nch = wf->nChannels;
        logf("LockForProcess fs=%u nch=%u maxFrames=%u", (unsigned)fs, nch, ppIn[0]->u32MaxFrameCount);
        current = readParams();
        dsp.prepare(fs, nch);
        dsp.setParams(current);
        locked = true;
        startPoll();
        return S_OK;
    }
    STDMETHODIMP UnlockForProcess() override { locked = false; stopPoll(); return S_OK; }

    // IAudioProcessingObjectRT
    STDMETHODIMP_(void) APOProcess(UINT32 nIn, APO_CONNECTION_PROPERTY** ppIn, UINT32 nOut, APO_CONNECTION_PROPERTY** ppOut) override {
        if (nIn < 1 || nOut < 1 || !ppIn[0] || !ppOut[0]) return;
        APO_CONNECTION_PROPERTY* in = ppIn[0]; APO_CONNECTION_PROPERTY* out = ppOut[0];
        const float* src = (const float*)in->pBuffer; float* dst = (float*)out->pBuffer;
        UINT32 frames = in->u32ValidFrameCount;
        if (pendingVersion.load(std::memory_order_acquire) != appliedVersion && TryAcquireSRWLockShared(&lock)) {
            dsp.setParams(pending); appliedVersion = pendingVersion.load(std::memory_order_relaxed);
            ReleaseSRWLockShared(&lock);
        }
        float inPk = 0.f;
        if (processCalls < 10 || (processCalls % 2000) == 0) for (UINT32 i = 0; i < frames * nch; i++) inPk = std::max(inPk, std::fabs(src[i]));
        if (in->u32BufferFlags == BUFFER_SILENT) {
            memset(dst, 0, (size_t)frames * nch * sizeof(float));
            dsp.process(dst, dst, (int)frames);
        } else {
            dsp.process(src, dst, (int)frames);
        }
        out->u32ValidFrameCount = frames;
        out->u32BufferFlags = BUFFER_VALID;
        if (processCalls < 10 || (processCalls % 2000) == 0) {
            float outPk = 0.f; for (UINT32 i = 0; i < frames * nch; i++) outPk = std::max(outPk, std::fabs(dst[i]));
            logf("APOProcess #%lu frames=%u inFlags=%u in=%p out=%p inPk=%.4f outPk=%.4f power=%d vol=%d", (unsigned long)processCalls, frames, in->u32BufferFlags, (void*)src, (void*)dst, inPk, outPk, dsp.getParams().power, dsp.getParams().volume);
        }
        processCalls++;
    }
    STDMETHODIMP_(UINT32) CalcInputFrames(UINT32 n) override { return n; }
    STDMETHODIMP_(UINT32) CalcOutputFrames(UINT32 n) override { return n; }

private:
    static bool isFloat32(const WAVEFORMATEX* wf) {
        if (wf->wBitsPerSample != 32) return false;
        if (wf->wFormatTag == WAVE_FORMAT_IEEE_FLOAT) return true;
        if (wf->wFormatTag == WAVE_FORMAT_EXTENSIBLE && wf->cbSize >= 22)
            return ((const WAVEFORMATEXTENSIBLE*)wf)->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
        return false;
    }
    static bool acceptable(IAudioMediaType* mt) {
        if (!mt) return false;
        const WAVEFORMATEX* wf = mt->GetAudioFormat();
        return wf && isFloat32(wf) && wf->nChannels >= 1 && wf->nChannels <= 16;
    }
    // All three parameters are optional (see baseaudioprocessingobject.h). Rules:
    //  - requested format acceptable            -> S_OK, *sup = requested
    //  - no requested format, opposite ok       -> S_OK, *sup = opposite (in-place APO: formats must match)
    //  - requested not ok but opposite ok       -> S_FALSE, *sup = opposite (suggestion)
    //  - nothing usable                         -> APOERR_FORMAT_NOT_SUPPORTED
    HRESULT checkFormat(IAudioMediaType* opposite, IAudioMediaType* req, IAudioMediaType** sup) {
        if (sup) *sup = nullptr;
        if (req && acceptable(req)) { if (sup) { req->AddRef(); *sup = req; } return S_OK; }
        if (acceptable(opposite)) { if (sup) { opposite->AddRef(); *sup = opposite; } return req ? S_FALSE : S_OK; }
        return APOERR_FORMAT_NOT_SUPPORTED;
    }
    void startPoll() {
        if (pollThread) return;
        stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        pollThread = CreateThread(nullptr, 0, &EnhancerAPO::pollProc, this, 0, nullptr);
    }
    void stopPoll() {
        if (!pollThread) return;
        SetEvent(stopEvent); WaitForSingleObject(pollThread, 2000);
        CloseHandle(pollThread); CloseHandle(stopEvent); pollThread = nullptr; stopEvent = nullptr;
    }
    static DWORD WINAPI pollProc(LPVOID ctx) {
        auto* self = (EnhancerAPO*)ctx;
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
        DWORD lastAg = 0xFFFFFFFF; int tick = 0;
        while (WaitForSingleObject(self->stopEvent, 100) == WAIT_TIMEOUT) {
            if ((tick++ % 3) == 0) {   // params every ~300 ms
                EnhancerDSP::Params p = readParams();
                if (!sameParams(p, self->current)) {
                    AcquireSRWLockExclusive(&self->lock);
                    self->pending = p; self->current = p;
                    self->pendingVersion.fetch_add(1, std::memory_order_release);
                    ReleaseSRWLockExclusive(&self->lock);
                    logf("params changed vol=%d hb=%d tr=%d amb=%d boost=%d power=%d", p.volume, p.harmBass, p.harmTreble, p.ambience, p.boost, p.power);
                }
            }
            // publish the automatic gain (x10000) so the UI can draw the "max" indicator; only on change
            DWORD ag = (DWORD)std::lround(self->dsp.getAutoGain() * 10000.0);
            if (ag != lastAg) {
                HKEY k;
                if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
                    RegSetValueExW(k, L"AutoGain", 0, REG_DWORD, (const BYTE*)&ag, sizeof ag); RegCloseKey(k); lastAg = ag;
                }
            }
        }
        return 0;
    }

    LONG refs = 1;
    unsigned long processCalls = 0;
    bool initialized = false, locked = false;
    double fs = 48000; UINT32 nch = 2;
    EnhancerDSP dsp;
    EnhancerDSP::Params current, pending;
    std::atomic<long> pendingVersion{0}; long appliedVersion = 0;
    SRWLOCK lock;
    HANDLE pollThread = nullptr, stopEvent = nullptr;
};

// ---------------------------------------------------------------- class factory
class Factory : public IClassFactory {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IClassFactory)) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return (ULONG)InterlockedIncrement(&refs); }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = (ULONG)InterlockedDecrement(&refs); if (!r) delete this; return r; }
    STDMETHODIMP CreateInstance(IUnknown* outer, REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        *ppv = nullptr;
        if (outer && riid != __uuidof(IUnknown)) return CLASS_E_NOAGGREGATION;  // COM rule: aggregation only via IUnknown
        auto* o = new (std::nothrow) EnhancerAPO(outer); if (!o) return E_OUTOFMEMORY;
        HRESULT hr = o->nd.QueryInterface(riid, ppv);   // non-delegating QI
        o->nd.Release();                                 // drop the construction reference
        logf("CreateInstance outer=%p riid=%08lx -> %08lx", (void*)outer, riid.Data1, hr);
        return hr;
    }
    STDMETHODIMP LockServer(BOOL) override { return S_OK; }
private:
    LONG refs = 1;
};

extern "C" {
BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_hModule = h; DisableThreadLibraryCalls(h); }
    return TRUE;
}
STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, void** ppv) {
    if (rclsid != CLSID_EnhancerAPO) return CLASS_E_CLASSNOTAVAILABLE;
    auto* f = new (std::nothrow) Factory(); if (!f) return E_OUTOFMEMORY;
    HRESULT hr = f->QueryInterface(riid, ppv); f->Release(); return hr;
}
STDAPI DllCanUnloadNow() { return g_objCount.load() == 0 ? S_OK : S_FALSE; }

static LONG setKeyValue(HKEY root, const wchar_t* sub, const wchar_t* name, const wchar_t* val) {
    HKEY k; LONG r = RegCreateKeyExW(root, sub, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, nullptr);
    if (r != ERROR_SUCCESS) return r;
    r = RegSetValueExW(k, name, 0, REG_SZ, (const BYTE*)val, (DWORD)((wcslen(val) + 1) * sizeof(wchar_t)));
    RegCloseKey(k); return r;
}
// The audio engine only loads APOs listed under HKCR\AudioEngine\AudioProcessingObjects\{CLSID};
// RegisterAPO (SDK audiobaseprocessingobject.lib) writes that key from APO_REG_PROPERTIES.
STDAPI DllRegisterServer() {
    wchar_t path[MAX_PATH]; GetModuleFileNameW(g_hModule, path, MAX_PATH);
    wchar_t sub[128]; swprintf_s(sub, L"CLSID\\%s", CLSID_STR);
    if (setKeyValue(HKEY_CLASSES_ROOT, sub, nullptr, L"Enhancer APO") != ERROR_SUCCESS) return SELFREG_E_CLASS;
    wchar_t sub2[160]; swprintf_s(sub2, L"CLSID\\%s\\InprocServer32", CLSID_STR);
    if (setKeyValue(HKEY_CLASSES_ROOT, sub2, nullptr, path) != ERROR_SUCCESS) return SELFREG_E_CLASS;
    if (setKeyValue(HKEY_CLASSES_ROOT, sub2, L"ThreadingModel", L"Both") != ERROR_SUCCESS) return SELFREG_E_CLASS;

    EnhancerAPO tmp; APO_REG_PROPERTIES* props = nullptr;
    HRESULT hr = tmp.GetRegistrationProperties(&props);
    if (FAILED(hr)) return hr;
    hr = RegisterAPO(props);
    CoTaskMemFree(props);
    return SUCCEEDED(hr) ? S_OK : hr;
}
STDAPI DllUnregisterServer() {
    UnregisterAPO(CLSID_EnhancerAPO);
    wchar_t sub[128]; swprintf_s(sub, L"CLSID\\%s", CLSID_STR);
    RegDeleteTreeW(HKEY_CLASSES_ROOT, sub);
    return S_OK;
}
}
