// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// aporender.cpp - opens a render endpoint in shared mode, plays 1 s of 440 Hz and reports every HRESULT.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <endpointvolume.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
#include <cmath>
#define CHK(x) do { HRESULT _hr = (x); wprintf(L"%-40s -> %08lx\n", L#x, _hr); if (FAILED(_hr)) return 1; } while (0)
int wmain(int argc, wchar_t** argv) {
    CoInitialize(nullptr);
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr;
    CHK(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en));
    if (argc > 1) CHK(en->GetDevice(argv[1], &dev)); else CHK(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev));
    IPropertyStore* ps = nullptr; PROPVARIANT pv; PropVariantInit(&pv);
    if (SUCCEEDED(dev->OpenPropertyStore(STGM_READ, &ps)) && SUCCEEDED(ps->GetValue(PKEY_Device_FriendlyName, &pv))) wprintf(L"device: %s\n", pv.pwszVal);
    DWORD st = 0; dev->GetState(&st); wprintf(L"state: %lu (1=active)\n", st);
    IAudioClient* ac = nullptr; WAVEFORMATEX* wf = nullptr;
    CHK(dev->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, (void**)&ac));
    CHK(ac->GetMixFormat(&wf));
    wprintf(L"mix format: %u Hz, %u ch, %u bits, tag %u\n", wf->nSamplesPerSec, wf->nChannels, wf->wBitsPerSample, wf->wFormatTag);
    CHK(ac->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, wf, nullptr));
    REFERENCE_TIME lat = 0; ac->GetStreamLatency(&lat); wprintf(L"stream latency: %.2f ms\n", lat / 10000.0);
    UINT32 bufFrames = 0; CHK(ac->GetBufferSize(&bufFrames));
    IAudioRenderClient* rc = nullptr; CHK(ac->GetService(__uuidof(IAudioRenderClient), (void**)&rc));
    IAudioMeterInformation* meter = nullptr; dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&meter);
    // fill first buffer then start
    int nch = wf->nChannels; double phase = 0; float pk = 0;
    auto fill = [&](UINT32 frames) { BYTE* p = nullptr; if (FAILED(rc->GetBuffer(frames, &p))) return false; float* f = (float*)p;
        for (UINT32 i = 0; i < frames; i++) { float v = (float)(0.25 * sin(phase)); phase += 2 * 3.14159265 * 440 / wf->nSamplesPerSec; for (int c = 0; c < nch; c++) f[i * nch + c] = v; }
        return SUCCEEDED(rc->ReleaseBuffer(frames, 0)); };
    fill(bufFrames);
    CHK(ac->Start());
    for (int t = 0; t < 1200; t += 20) {
        Sleep(20); UINT32 pad = 0; ac->GetCurrentPadding(&pad); UINT32 avail = bufFrames - pad; if (avail > 0) fill(avail);
        if (meter) { float p = 0; if (SUCCEEDED(meter->GetPeakValue(&p)) && p > pk) pk = p; }
    }
    ac->Stop();
    wprintf(L"endpoint meter peak during playback: %.4f\n", pk);
    return 0;
}
