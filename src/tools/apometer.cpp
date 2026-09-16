// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// apometer.cpp - prints the peak level of the default render endpoint for N ms (post-APO, what the device gets)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <cstdio>
#include <cstdlib>
int wmain(int argc, wchar_t** argv) {
    int ms = argc > 1 ? _wtoi(argv[1]) : 2000;
    CoInitialize(nullptr);
    IMMDeviceEnumerator* en = nullptr; IMMDevice* dev = nullptr; IAudioMeterInformation* meter = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return 1;
    if (FAILED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &dev))) { printf("no default endpoint\n"); return 1; }
    if (FAILED(dev->Activate(__uuidof(IAudioMeterInformation), CLSCTX_ALL, nullptr, (void**)&meter))) { printf("no meter\n"); return 1; }
    float mx = 0; int n = 0;
    for (int t = 0; t < ms; t += 50) { float p = 0; if (SUCCEEDED(meter->GetPeakValue(&p))) { if (p > mx) mx = p; if (p > 0.001f) n++; } Sleep(50); }
    printf("peak max=%.4f  active_polls=%d/%d\n", mx, n, ms / 50);
    return 0;
}
