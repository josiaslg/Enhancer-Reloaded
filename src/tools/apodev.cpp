// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// apodev.cpp - lists render endpoints (id, name, default flag) for register.ps1
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <cstdio>
int wmain() {
    CoInitialize(nullptr);
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&en))) return 1;
    IMMDevice* def = nullptr; LPWSTR defId = nullptr;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(eRender, eMultimedia, &def))) def->GetId(&defId);
    IMMDeviceCollection* col = nullptr; en->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &col);
    UINT n = 0; col->GetCount(&n);
    for (UINT i = 0; i < n; i++) {
        IMMDevice* d = nullptr; col->Item(i, &d); LPWSTR id = nullptr; d->GetId(&id);
        IPropertyStore* ps = nullptr; d->OpenPropertyStore(STGM_READ, &ps);
        PROPVARIANT pv; PropVariantInit(&pv); ps->GetValue(PKEY_Device_FriendlyName, &pv);
        wprintf(L"%s\t%s\t%s\n", (defId && wcscmp(id, defId) == 0) ? L"DEFAULT" : L"-", id, pv.pwszVal ? pv.pwszVal : L"?");
        PropVariantClear(&pv); ps->Release(); CoTaskMemFree(id); d->Release();
    }
    if (defId) CoTaskMemFree(defId);
    return 0;
}
