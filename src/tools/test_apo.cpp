// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// test_apo.cpp - loads EnhancerAPO.dll without registering it, drives it like the audio engine would.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <objbase.h>
#include <mmreg.h>
#include <ks.h>
#include <ksmedia.h>
#include <audioenginebaseapo.h>
#include <audiomediatype.h>
#include <cstdio>
#include <cmath>
#include <vector>

DEFINE_GUID(CLSID_EnhancerAPO, 0x8e7c1d3a, 0x5b4f, 0x4e2a, 0x9c, 0x6d, 0x3f, 0x1a, 0x2b, 0x4c, 0x5d, 0x6e);

struct FakeMediaType : IAudioMediaType {
    WAVEFORMATEXTENSIBLE wfx{}; LONG refs = 1;
    FakeMediaType(int rate, int ch) {
        wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE; wfx.Format.nChannels = (WORD)ch; wfx.Format.nSamplesPerSec = rate;
        wfx.Format.wBitsPerSample = 32; wfx.Format.nBlockAlign = (WORD)(4 * ch); wfx.Format.nAvgBytesPerSec = rate * 4 * ch;
        wfx.Format.cbSize = 22; wfx.Samples.wValidBitsPerSample = 32; wfx.dwChannelMask = ch == 2 ? 3 : 4; wfx.SubFormat = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT;
    }
    STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override { if (riid == __uuidof(IUnknown) || riid == __uuidof(IAudioMediaType)) { *ppv = this; AddRef(); return S_OK; } *ppv = nullptr; return E_NOINTERFACE; }
    STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
    STDMETHODIMP_(ULONG) Release() override { return --refs; }
    STDMETHODIMP IsCompressedFormat(BOOL* p) override { *p = FALSE; return S_OK; }
    STDMETHODIMP IsEqual(IAudioMediaType*, DWORD* f) override { *f = 0; return S_OK; }
    const WAVEFORMATEX* STDMETHODCALLTYPE GetAudioFormat() override { return &wfx.Format; }
    STDMETHODIMP GetUncompressedAudioFormat(UNCOMPRESSEDAUDIOFORMAT* f) override {
        f->guidFormatType = KSDATAFORMAT_SUBTYPE_IEEE_FLOAT; f->dwSamplesPerFrame = wfx.Format.nChannels; f->dwBytesPerSampleContainer = 4; f->dwValidBitsPerSample = 32; f->fFramesPerSecond = (FLOAT)wfx.Format.nSamplesPerSec; f->dwChannelMask = wfx.dwChannelMask; return S_OK; }
};

int main() {
    HMODULE h = LoadLibraryW(L"EnhancerAPO.dll");
    if (!h) { printf("LoadLibrary failed %lu\n", GetLastError()); return 1; }
    auto gco = (HRESULT(WINAPI*)(REFCLSID, REFIID, void**))GetProcAddress(h, "DllGetClassObject");
    IClassFactory* cf = nullptr; HRESULT hr = gco(CLSID_EnhancerAPO, __uuidof(IClassFactory), (void**)&cf);
    printf("DllGetClassObject hr=%08lx\n", hr); if (FAILED(hr)) return 1;
    IAudioProcessingObject* apo = nullptr; hr = cf->CreateInstance(nullptr, __uuidof(IAudioProcessingObject), (void**)&apo);
    printf("CreateInstance hr=%08lx\n", hr); if (FAILED(hr)) return 1;
    IAudioProcessingObjectRT* rt = nullptr; IAudioProcessingObjectConfiguration* cfg = nullptr; IAudioSystemEffects* fx = nullptr;
    printf("QI RT=%08lx CFG=%08lx FX=%08lx\n", apo->QueryInterface(__uuidof(IAudioProcessingObjectRT), (void**)&rt), apo->QueryInterface(__uuidof(IAudioProcessingObjectConfiguration), (void**)&cfg), apo->QueryInterface(__uuidof(IAudioSystemEffects), (void**)&fx));
    APO_REG_PROPERTIES* rp = nullptr; hr = apo->GetRegistrationProperties(&rp); wprintf(L"RegProps hr=%08lx name=%s flags=%x\n", hr, rp ? rp->szFriendlyName : L"?", rp ? rp->Flags : 0); if (rp) CoTaskMemFree(rp);
    APOInitSystemEffects2 init{}; init.APOInit.cbSize = sizeof init; init.APOInit.clsid = CLSID_EnhancerAPO; init.AudioProcessingMode = AUDIO_SIGNALPROCESSINGMODE_DEFAULT;
    hr = apo->Initialize(sizeof init, (BYTE*)&init); printf("Initialize hr=%08lx\n", hr);
    FakeMediaType mt(48000, 2); IAudioMediaType* sup = nullptr;
    hr = apo->IsInputFormatSupported(nullptr, &mt, &sup); printf("IsInputFormatSupported hr=%08lx sup=%p\n", hr, (void*)sup); if (sup) sup->Release();
    APO_CONNECTION_DESCRIPTOR din{}, dout{}; din.Type = APO_CONNECTION_BUFFER_TYPE_EXTERNAL; din.u32MaxFrameCount = 480; din.pFormat = &mt; dout = din;
    APO_CONNECTION_DESCRIPTOR* pin = &din; APO_CONNECTION_DESCRIPTOR* pout = &dout;
    hr = cfg->LockForProcess(1, &pin, 1, &pout); printf("LockForProcess hr=%08lx\n", hr); if (FAILED(hr)) return 1;
    HNSTIME lat = 0; apo->GetLatency(&lat); printf("latency = %lld hns (%.2f ms)\n", (long long)lat, lat / 10000.0);
    // process 2 s of a 40 Hz + 1 kHz stereo tone in 480-frame blocks and measure peak/rms
    const int N = 48000 * 2, B = 480; std::vector<float> buf(B * 2);
    double sumIn = 0, sumOut = 0; float pk = 0;
    for (int off = 0; off < N; off += B) {
        for (int i = 0; i < B; i++) { double t = (off + i) / 48000.0; float v = (float)(0.2 * sin(2 * 3.14159265 * 40 * t) + 0.1 * sin(2 * 3.14159265 * 1000 * t)); buf[2 * i] = v; buf[2 * i + 1] = v; sumIn += v * v; }
        APO_CONNECTION_PROPERTY cin{}, cout{}; cin.pBuffer = (UINT_PTR)buf.data(); cin.u32ValidFrameCount = B; cin.u32BufferFlags = BUFFER_VALID; cout = cin;
        APO_CONNECTION_PROPERTY* pi = &cin; APO_CONNECTION_PROPERTY* po = &cout;
        rt->APOProcess(1, &pi, 1, &po);
        if (off >= N / 2) for (int i = 0; i < B * 2; i++) { sumOut += buf[i] * buf[i]; pk = std::max(pk, fabsf(buf[i])); }
    }
    printf("rms in=%.4f  rms out(2nd half)=%.4f  peak out=%.4f  (defaults: Normal preset)\n", sqrt(sumIn / N), sqrt(sumOut / (N)), pk);
    cfg->UnlockForProcess(); rt->Release(); cfg->Release(); fx->Release(); apo->Release(); cf->Release();
    // --- aggregation test (the audio engine creates APOs this way)
    struct Outer : IUnknown {
        IUnknown* inner = nullptr; LONG refs = 1;
        STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override {
            if (riid == __uuidof(IUnknown)) { *ppv = this; AddRef(); return S_OK; }
            return inner ? inner->QueryInterface(riid, ppv) : E_NOINTERFACE;
        }
        STDMETHODIMP_(ULONG) AddRef() override { return ++refs; }
        STDMETHODIMP_(ULONG) Release() override { return --refs; }
    } outer;
    hr = gco(CLSID_EnhancerAPO, __uuidof(IClassFactory), (void**)&cf);
    hr = cf->CreateInstance(&outer, __uuidof(IUnknown), (void**)&outer.inner);
    printf("aggregated CreateInstance hr=%08lx inner=%p\n", hr, (void*)outer.inner);
    if (SUCCEEDED(hr)) {
        IAudioProcessingObject* a2 = nullptr; hr = outer.QueryInterface(__uuidof(IAudioProcessingObject), (void**)&a2);
        printf("QI via outer hr=%08lx outer.refs=%ld\n", hr, outer.refs);
        HNSTIME l2 = 0; a2->GetLatency(&l2); printf("aggregated GetLatency=%lld\n", (long long)l2);
        a2->Release(); printf("after Release outer.refs=%ld\n", outer.refs);
        outer.inner->Release();
    }
    cf->Release();
    auto cun = (HRESULT(WINAPI*)())GetProcAddress(h, "DllCanUnloadNow"); printf("DllCanUnloadNow=%08lx\n", cun());
    return 0;
}
