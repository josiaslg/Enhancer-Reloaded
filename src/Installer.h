// SPDX-License-Identifier: BSD-2-Clause
// Copyright (c) 2026 josiaslg <josiaslg@bsd.com.br> - https://github.com/josiaslg/Enhancer-Reloaded
// Installer.h - self-install / uninstall of the Enhancer Reloaded audio component (APO), run elevated
// via "EnhancerReloaded.exe --install" / "--uninstall". Everything the installer changes is backed up in the
// install directory so that uninstall restores the machine exactly.
#pragma once
#include <windows.h>
#include <shellapi.h>
#include <sddl.h>
#include <aclapi.h>
#include <shobjidl.h>
#include <shlguid.h>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

namespace inst {

static const wchar_t* CLSID_STR    = L"{8E7C1D3A-5B4F-4E2A-9C6D-3F1A2B4C5D6E}";
static const wchar_t* INSTALL_DIR  = L"C:\\Program Files\\EnhancerReloaded";
static const wchar_t* OLD_DIR      = L"C:\\Program Files\\EnhancerAPO";      // earlier PowerShell-based install
static const wchar_t* LOG_DIR      = L"C:\\ProgramData\\EnhancerAPO";
static const wchar_t* PARAM_KEY    = L"SOFTWARE\\EnhancerAPO";
static const wchar_t* AUDIO_KEY    = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Audio";
static const wchar_t* RENDER_ROOT  = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\MMDevices\\Audio\\Render";
static const wchar_t* K_MODEFX     = L"{d04e05a6-594b-4fb6-a80d-01af5eed7d1d},6";   // PKEY_FX_ModeEffectClsid
static const wchar_t* K_MFXMODES   = L"{d3993a3f-99c2-4402-b5ec-a92a0367664b},6";   // PKEY_MFX_ProcessingModes_Supported_For_Streaming
static const wchar_t* K_DISABLEFX  = L"{1da5d803-d492-4edd-8c23-e0c0ffee7f0e},5";   // PKEY_AudioEndpoint_Disable_SysFx
static const wchar_t* K_NAME       = L"{a45c254e-df1c-4efd-8020-67d146a850e0},2";
static const wchar_t* MODE_DEFAULT = L"{C18E2F7E-933D-4965-B7D1-1EEF228D2AF3}";
static const wchar_t* NONE         = L"<none>";
static const wchar_t* UNINSTALL_KEY = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\EnhancerReloaded";

static std::wstring g_log;
static std::wstring logPath() { return std::wstring(INSTALL_DIR) + L"\\install.log"; }
// every step is appended to install.log immediately, so the (non-elevated) panel can show progress while we run
static void log(const std::wstring& s) {
    g_log += s + L"\r\n";
    HANDLE f = CreateFileW(logPath().c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) {   // install.log itself locked (e.g. by security software after a quarantine restore): log under ProgramData instead
        CreateDirectoryW(LOG_DIR, nullptr);
        f = CreateFileW((std::wstring(LOG_DIR) + L"\\install.log").c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    if (f != INVALID_HANDLE_VALUE) {
        std::wstring line = s + L"\r\n"; int n = WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), nullptr, 0, nullptr, nullptr);
        std::string a(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, line.data(), (int)line.size(), &a[0], n, nullptr, nullptr);
        DWORD w = 0; WriteFile(f, a.data(), (DWORD)a.size(), &w, nullptr); CloseHandle(f);
    }
}
static std::wstring installDllPath() { return std::wstring(INSTALL_DIR) + L"\\EnhancerAPO.dll"; }
static std::wstring installExePath() { return std::wstring(INSTALL_DIR) + L"\\EnhancerReloaded.exe"; }
static std::wstring thisExePath() { wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH); return exe; }
static bool runningFromInstallDir() { return _wcsnicmp(thisExePath().c_str(), INSTALL_DIR, wcslen(INSTALL_DIR)) == 0; }
static std::wstring startMenuShortcut() { wchar_t p[MAX_PATH]; ExpandEnvironmentStringsW(L"%ProgramData%\\Microsoft\\Windows\\Start Menu\\Programs\\Enhancer Reloaded.lnk", p, MAX_PATH); return p; }
static std::wstring appDataDir() { wchar_t p[MAX_PATH]; ExpandEnvironmentStringsW(L"%APPDATA%\\EnhancerReloaded", p, MAX_PATH); return p; }

// ---------------------------------------------------------------- small helpers
static bool fileExists(const std::wstring& p) { DWORD a = GetFileAttributesW(p.c_str()); return a != INVALID_FILE_ATTRIBUTES; }
static std::wstring readText(const std::wstring& p) {   // UTF-8 in
    std::ifstream f(p, std::ios::binary); std::string s((std::istreambuf_iterator<char>(f)), {}); if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0); std::wstring w(n, L'\0'); MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n); return w;
}
static void writeText(const std::wstring& p, const std::wstring& s) {   // UTF-8 out
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0, nullptr, nullptr); std::string a(n, '\0'); WideCharToMultiByte(CP_UTF8, 0, s.data(), (int)s.size(), &a[0], n, nullptr, nullptr);
    std::ofstream f(p, std::ios::binary); f.write(a.data(), a.size());
}
static std::vector<std::wstring> split(const std::wstring& s, wchar_t sep) { std::vector<std::wstring> o; size_t p = 0; while (p <= s.size()) { size_t e = s.find(sep, p); if (e == std::wstring::npos) e = s.size(); o.push_back(s.substr(p, e - p)); p = e + 1; } return o; }
static std::wstring iniValue(const std::wstring& text, const std::wstring& key) {   // "key=value" lines
    for (auto& l : split(text, L'\n')) { std::wstring t = l; while (!t.empty() && (t.back() == L'\r')) t.pop_back(); if (t.compare(0, key.size() + 1, key + L"=") == 0) return t.substr(key.size() + 1); }
    return L"";
}

static bool isAdmin() {
    BOOL a = FALSE; PSID sid = nullptr; SID_IDENTIFIER_AUTHORITY nt = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &sid)) { CheckTokenMembership(nullptr, sid, &a); FreeSid(sid); }
    return a == TRUE;
}

// ---------------------------------------------------------------- services
static bool serviceControl(const wchar_t* name, bool start) {
    SC_HANDLE scm = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT); if (!scm) return false;
    SC_HANDLE s = OpenServiceW(scm, name, SERVICE_START | SERVICE_STOP | SERVICE_QUERY_STATUS); if (!s) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS st{}; bool ok = true;
    if (start) { if (!StartServiceW(s, 0, nullptr) && GetLastError() != ERROR_SERVICE_ALREADY_RUNNING) ok = false; }
    else { if (!ControlService(s, SERVICE_CONTROL_STOP, &st) && GetLastError() != ERROR_SERVICE_NOT_ACTIVE) ok = false; }
    for (int i = 0; i < 100; i++) {   // wait up to 10 s
        if (!QueryServiceStatus(s, &st)) break;
        if (start ? st.dwCurrentState == SERVICE_RUNNING : st.dwCurrentState == SERVICE_STOPPED) break;
        Sleep(100);
    }
    CloseServiceHandle(s); CloseServiceHandle(scm); return ok;
}
static void stopAudio() { serviceControl(L"Audiosrv", false); serviceControl(L"AudioEndpointBuilder", false); Sleep(500); }
static void startAudio() { serviceControl(L"AudioEndpointBuilder", true); serviceControl(L"Audiosrv", true); }

// ---------------------------------------------------------------- security descriptors from SDDL
static bool setKeyDacl(HKEY root, const wchar_t* sub, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr; if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    HKEY k; bool ok = false;
    if (RegOpenKeyExW(root, sub, 0, WRITE_DAC | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) { ok = RegSetKeySecurity(k, DACL_SECURITY_INFORMATION, sd) == ERROR_SUCCESS; RegCloseKey(k); }
    LocalFree(sd); return ok;
}
static bool setDirDacl(const wchar_t* path, const wchar_t* sddl) {
    PSECURITY_DESCRIPTOR sd = nullptr; if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl, SDDL_REVISION_1, &sd, nullptr)) return false;
    bool ok = SetFileSecurityW(path, DACL_SECURITY_INFORMATION, sd) == TRUE; LocalFree(sd); return ok;
}

// ---------------------------------------------------------------- registry helpers
static bool regGetDword(HKEY root, const wchar_t* sub, const wchar_t* name, DWORD& out) {
    HKEY k; if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    DWORD sz = sizeof out, type = 0; bool ok = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)&out, &sz) == ERROR_SUCCESS && type == REG_DWORD; RegCloseKey(k); return ok;
}
static bool regGetString(HKEY root, const wchar_t* sub, const wchar_t* name, std::wstring& out) {
    HKEY k; if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[2048]; DWORD sz = sizeof buf, type = 0; bool ok = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_SZ; RegCloseKey(k);
    if (ok) out = buf; return ok;
}
static bool regGetMulti(HKEY root, const wchar_t* sub, const wchar_t* name, std::wstring& joined) {   // items joined by ';'
    HKEY k; if (RegOpenKeyExW(root, sub, 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &k) != ERROR_SUCCESS) return false;
    wchar_t buf[4096]; DWORD sz = sizeof buf, type = 0; bool ok = RegQueryValueExW(k, name, nullptr, &type, (BYTE*)buf, &sz) == ERROR_SUCCESS && type == REG_MULTI_SZ; RegCloseKey(k);
    if (!ok) return false; joined.clear();
    for (wchar_t* p = buf; *p; p += wcslen(p) + 1) { if (!joined.empty()) joined += L';'; joined += p; }
    return true;
}
static HKEY openSetValue(HKEY root, const std::wstring& sub) { HKEY k; return RegOpenKeyExW(root, sub.c_str(), 0, KEY_SET_VALUE | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS ? k : nullptr; }
static void setMulti(HKEY k, const wchar_t* name, const std::wstring& joined) {
    std::wstring blob; for (auto& it : split(joined, L';')) if (!it.empty()) { blob += it; blob.push_back(L'\0'); } blob.push_back(L'\0');
    RegSetValueExW(k, name, 0, REG_MULTI_SZ, (const BYTE*)blob.c_str(), (DWORD)(blob.size() * sizeof(wchar_t)));
}

// ---------------------------------------------------------------- old PowerShell-install backups (JSON) -> migrated
static std::wstring jsonValue(const std::wstring& j, const std::wstring& key) {   // returns NONE, a string, a number, or "a;b" for arrays
    size_t p = j.find(L"\"" + key + L"\""); if (p == std::wstring::npos) return L"";
    p = j.find(L':', p); if (p == std::wstring::npos) return L""; p++;
    while (p < j.size() && iswspace(j[p])) p++;
    if (j.compare(p, 4, L"null") == 0) return NONE;
    if (j[p] == L'"') { size_t e = j.find(L'"', p + 1); return j.substr(p + 1, e - p - 1); }
    if (j[p] == L'[') { std::wstring out; size_t e = j.find(L']', p); std::wstring body = j.substr(p + 1, e - p - 1); size_t q = 0;
        while ((q = body.find(L'"', q)) != std::wstring::npos) { size_t e2 = body.find(L'"', q + 1); if (!out.empty()) out += L';'; out += body.substr(q + 1, e2 - q - 1); q = e2 + 1; } return out; }
    size_t e = p; while (e < j.size() && (iswdigit(j[e]) || j[e] == L'-')) e++; return j.substr(p, e - p);
}

// ---------------------------------------------------------------- endpoints
struct Endpoint { std::wstring guid, name; };
static std::vector<Endpoint> activeEndpoints() {
    std::vector<Endpoint> out; HKEY root;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, RENDER_ROOT, 0, KEY_READ | KEY_WOW64_64KEY, &root) != ERROR_SUCCESS) return out;
    for (DWORD i = 0;; i++) {
        wchar_t sub[128]; DWORD n = 128; if (RegEnumKeyExW(root, i, sub, &n, nullptr, nullptr, nullptr, nullptr) != ERROR_SUCCESS) break;
        std::wstring base = std::wstring(RENDER_ROOT) + L"\\" + sub; DWORD state = 0;
        if (!regGetDword(HKEY_LOCAL_MACHINE, base.c_str(), L"DeviceState", state) || state != 1) continue;
        HKEY fx; if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, (base + L"\\FxProperties").c_str(), 0, KEY_QUERY_VALUE | KEY_WOW64_64KEY, &fx) != ERROR_SUCCESS) continue; RegCloseKey(fx);
        Endpoint e; e.guid = sub; regGetString(HKEY_LOCAL_MACHINE, (base + L"\\Properties").c_str(), K_NAME, e.name); out.push_back(e);
    }
    RegCloseKey(root); return out;
}

// ---------------------------------------------------------------- setup lock: install and uninstall never overlap
static HANDLE g_setupLock = nullptr;
static bool acquireSetupLock() {
    g_setupLock = CreateMutexW(nullptr, FALSE, L"Global\\EnhancerReloaded.setup");
    if (!g_setupLock) return false;
    DWORD r = WaitForSingleObject(g_setupLock, 90000);   // wait for a running install/uninstall to finish
    return r == WAIT_OBJECT_0 || r == WAIT_ABANDONED;
}
static void releaseSetupLock() { if (g_setupLock) { ReleaseMutex(g_setupLock); CloseHandle(g_setupLock); g_setupLock = nullptr; } }

// ---------------------------------------------------------------- installed?
static bool isInstalled() {
    HKEY k; bool apo = RegOpenKeyExW(HKEY_CLASSES_ROOT, (std::wstring(L"AudioEngine\\AudioProcessingObjects\\") + CLSID_STR).c_str(), 0, KEY_READ, &k) == ERROR_SUCCESS; if (apo) RegCloseKey(k);
    bool params = RegOpenKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS; if (params) RegCloseKey(k);
    if (!params) return false;
    std::wstring srv; regGetString(HKEY_CLASSES_ROOT, (std::wstring(L"CLSID\\") + CLSID_STR + L"\\InprocServer32").c_str(), nullptr, srv);
    if (!apo || !fileExists(srv) || _wcsicmp(srv.c_str(), installDllPath().c_str()) != 0) return false;   // an older install elsewhere counts as "not installed": we take it over (with its backups)
    for (auto& e : activeEndpoints()) { std::wstring v; if (regGetString(HKEY_LOCAL_MACHINE, (std::wstring(RENDER_ROOT) + L"\\" + e.guid + L"\\FxProperties").c_str(), K_MODEFX, v) && _wcsicmp(v.c_str(), CLSID_STR) == 0) return true; }
    return false;
}

// true when the installed DLL is byte-identical to the one embedded in this exe (otherwise an update is due)
static bool installedDllMatches(HINSTANCE inst, int dllResourceId) {
    HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(dllResourceId), RT_RCDATA); if (!r) return true;
    HGLOBAL hg = LoadResource(inst, r); const char* data = (const char*)LockResource(hg); DWORD size = SizeofResource(inst, r);
    std::ifstream f(installDllPath(), std::ios::binary); if (!f) return false;
    std::string cur((std::istreambuf_iterator<char>(f)), {});
    return cur.size() == size && memcmp(cur.data(), data, size) == 0;
}

// true when the installed program file is byte-identical to the exe we are running from
static bool installedExeMatches() {
    std::ifstream a(thisExePath(), std::ios::binary), b(installExePath(), std::ios::binary); if (!a || !b) return false;
    std::string sa((std::istreambuf_iterator<char>(a)), {}), sb((std::istreambuf_iterator<char>(b)), {});
    return sa == sb;
}
// ask a running panel to close (it powers the effect off on the way out) and wait for it
static void closeRunningPanel() {
    HWND w = FindWindowW(L"EnhancerSkinWnd", nullptr); if (!w) return;
    DWORD pid = 0; GetWindowThreadProcessId(w, &pid);
    PostMessageW(w, WM_CLOSE, 0, 0);
    HANDLE p = pid ? OpenProcess(SYNCHRONIZE, FALSE, pid) : nullptr;
    if (p) { WaitForSingleObject(p, 5000); CloseHandle(p); } else Sleep(1500);
}

// ---------------------------------------------------------------- install (elevated)
static void flushLog() { /* lines are appended as they happen (see log()) */ }
static int fail(int code, const std::wstring& why, bool restartAudio) {
    log(L"FAILED (code " + std::to_wstring(code) + L"): " + why + L" [GetLastError=" + std::to_wstring(GetLastError()) + L"]");
    flushLog(); if (restartAudio) startAudio(); return code;
}
static int runInstall(HINSTANCE inst, int dllResourceId) {
    CreateDirectoryW(INSTALL_DIR, nullptr); DeleteFileW(logPath().c_str());
    g_log.clear(); log(L"Preparing the installation...");
    if (!isAdmin()) return fail(2, L"not elevated", false);
    if (!acquireSetupLock()) return fail(8, L"another setup operation is still running", false);
    struct Unlock { ~Unlock() { releaseSetupLock(); } } unlock;
    CreateDirectoryW(LOG_DIR, nullptr); setDirDacl(LOG_DIR, L"D:(A;OICI;FA;;;WD)(A;OICI;FA;;;BA)(A;OICI;FA;;;SY)");
    // 1. extract the embedded APO DLL. audiodg/Audiosrv keep the installed DLL loaded: stop the audio stack
    //    and retry for up to 15 s (the processes exit asynchronously after the services report "stopped").
    HRSRC r = FindResourceW(inst, MAKEINTRESOURCEW(dllResourceId), RT_RCDATA); if (!r) return fail(3, L"embedded DLL resource missing", false);
    HGLOBAL hg = LoadResource(inst, r); const void* data = LockResource(hg); DWORD size = SizeofResource(inst, r);
    bool audioWasStopped = false;
    {
        std::wstring dll = installDllPath();
        HANDLE f = CreateFileW(dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
            DWORD firstErr = GetLastError(); log(L"Stopping the Windows audio service (component in use, error " + std::to_wstring(firstErr) + L")...");
            stopAudio(); audioWasStopped = true;
            for (int i = 0; i < 30 && f == INVALID_HANDLE_VALUE; i++) {
                Sleep(500);
                // rename the old file out of the way if it is still mapped (allowed even while loaded), then create the new one
                if (i == 10) { std::wstring old = dll + L".old"; DeleteFileW(old.c_str()); MoveFileExW(dll.c_str(), old.c_str(), MOVEFILE_REPLACE_EXISTING); MoveFileExW(old.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT); }
                f = CreateFileW(dll.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            }
        }
        if (f == INVALID_HANDLE_VALUE) return fail(4, L"cannot write " + dll, true);
        DWORD w = 0; BOOL ok = WriteFile(f, data, size, &w, nullptr); CloseHandle(f);
        if (!ok || w != size) return fail(4, L"short write of " + dll, true);
        log(L"Audio component copied (" + std::to_wstring(size) + L" bytes)");
    }
    // 2. COM class + AudioEngine\AudioProcessingObjects (DllRegisterServer calls RegisterAPO)
    {
        HMODULE m = LoadLibraryW(installDllPath().c_str()); if (!m) return fail(5, L"cannot load the installed DLL", audioWasStopped);
        auto reg = (HRESULT(WINAPI*)())GetProcAddress(m, "DllRegisterServer"); HRESULT hr = reg ? reg() : E_FAIL; FreeLibrary(m);
        if (FAILED(hr)) { wchar_t h[16]; swprintf_s(h, L"%08lx", hr); return fail(6, std::wstring(L"DllRegisterServer failed hr=") + h, audioWasStopped); }
        log(L"Audio component registered with the audio engine");
    }
    // 3. allow an unsigned APO inside audiodg (backup the previous value once)
    {
        std::wstring bk = std::wstring(INSTALL_DIR) + L"\\backup-audiodg.txt";
        if (!fileExists(bk)) {
            DWORD prev = 0; std::wstring v = regGetDword(HKEY_LOCAL_MACHINE, AUDIO_KEY, L"DisableProtectedAudioDG", prev) ? std::to_wstring(prev) : NONE;
            std::wstring oldJson = std::wstring(OLD_DIR) + L"\\backup-audiodg.json";
            if (fileExists(oldJson)) { std::wstring ov = jsonValue(readText(oldJson), L"DisableProtectedAudioDG"); if (!ov.empty()) v = ov; }
            writeText(bk, L"Prev=" + v + L"\r\n");
        }
        HKEY k = openSetValue(HKEY_LOCAL_MACHINE, AUDIO_KEY); if (k) { DWORD one = 1; RegSetValueExW(k, L"DisableProtectedAudioDG", 0, REG_DWORD, (const BYTE*)&one, sizeof one); RegCloseKey(k); }
    }
    // 4. parameter key (neutral defaults) writable by Users, so the panel needs no elevation
    {
        HKEY k; DWORD disp = 0;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, PARAM_KEY, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, &disp) == ERROR_SUCCESS) {
            if (disp == REG_CREATED_NEW_KEY) {
                struct { const wchar_t* n; DWORD v; } d[] = {{L"Power", 1}, {L"Boost", 0}, {L"Volume", 50}, {L"HarmBass", 0}, {L"HarmBassRange", 50}, {L"DrumBass", 0}, {L"DrumBassRange", 50}, {L"Dry", 100}, {L"HarmTreble", 0}, {L"HarmTrebleRange", 50}, {L"Ambience", 0}, {L"AmbienceRange", 50}, {L"Haas", 0}, {L"HaasDelay", 15}};
                for (auto& x : d) RegSetValueExW(k, x.n, 0, REG_DWORD, (const BYTE*)&x.v, sizeof x.v);
            }
            RegCloseKey(k);
        }
        setKeyDacl(HKEY_LOCAL_MACHINE, PARAM_KEY, L"D:(A;OICI;KA;;;BA)(A;OICI;KA;;;SY)(A;OICI;KA;;;BU)");
    }
    // 5. every active render endpoint: backup (or migrate the old JSON backup), then point the MFX slot at us
    int count = 0;
    for (auto& e : activeEndpoints()) {
        std::wstring fx = std::wstring(RENDER_ROOT) + L"\\" + e.guid + L"\\FxProperties";
        std::wstring bk = std::wstring(INSTALL_DIR) + L"\\backup-" + e.guid + L".txt";
        // a backup file placed next to the exe we were started from (e.g. recovered from an earlier install) wins
        { std::wstring exeDir = thisExePath(); exeDir = exeDir.substr(0, exeDir.find_last_of(L'\\')); std::wstring seed = exeDir + L"\\backup-" + e.guid + L".txt";
          if (!fileExists(bk) && fileExists(seed) && _wcsicmp(exeDir.c_str(), INSTALL_DIR) != 0) { CopyFileW(seed.c_str(), bk.c_str(), FALSE); log(L"backup seeded from " + seed); } }
        if (!fileExists(bk)) {
            std::wstring mode, modes, dis; DWORD d = 0;
            mode = regGetString(HKEY_LOCAL_MACHINE, fx.c_str(), K_MODEFX, mode) ? mode : NONE;
            modes = regGetMulti(HKEY_LOCAL_MACHINE, fx.c_str(), K_MFXMODES, modes) ? modes : NONE;
            dis = regGetDword(HKEY_LOCAL_MACHINE, fx.c_str(), K_DISABLEFX, d) ? std::to_wstring(d) : NONE;
            // migration: if the slot already holds our CLSID from the old PowerShell install, take the real original from its JSON backup
            if (_wcsicmp(mode.c_str(), CLSID_STR) == 0) {
                for (auto old : {std::wstring(OLD_DIR) + L"\\backup-" + e.guid + L".json", std::wstring(OLD_DIR) + L"\\backup.json"}) {
                    if (!fileExists(old)) continue; std::wstring j = readText(old);
                    if (old.find(L"backup.json") != std::wstring::npos && jsonValue(j, L"EndpointId") != e.guid) continue;
                    std::wstring a = jsonValue(j, K_MODEFX), b = jsonValue(j, K_MFXMODES), c = jsonValue(j, K_DISABLEFX);
                    if (!a.empty()) mode = a; if (!b.empty()) modes = b; if (!c.empty()) dis = c;
                    log(L"migrated backup from " + old); break;
                }
                if (_wcsicmp(mode.c_str(), CLSID_STR) == 0) mode = NONE;   // no original known: uninstall will clear the slot
            }
            writeText(bk, L"Name=" + e.name + L"\r\nModeEffect=" + mode + L"\r\nModes=" + modes + L"\r\nDisableSysFx=" + dis + L"\r\n");
        }
        HKEY k = openSetValue(HKEY_LOCAL_MACHINE, fx); if (!k) { log(L"cannot open " + fx); continue; }
        RegSetValueExW(k, K_MODEFX, 0, REG_SZ, (const BYTE*)CLSID_STR, (DWORD)((wcslen(CLSID_STR) + 1) * sizeof(wchar_t)));
        setMulti(k, K_MFXMODES, MODE_DEFAULT);
        DWORD zero = 0; RegSetValueExW(k, K_DISABLEFX, 0, REG_DWORD, (const BYTE*)&zero, sizeof zero);
        RegCloseKey(k); count++; log(L"Enabled on: " + e.name);
    }
    // 5b. the program itself: copy this exe into the install dir (unless we already run from there), Start Menu
    //     shortcut, and re-point an existing "Start with Windows" entry to the installed copy
    if (!runningFromInstallDir()) {
        BOOL ok = CopyFileW(thisExePath().c_str(), installExePath().c_str(), FALSE);
        if (!ok) { closeRunningPanel(); for (int i = 0; i < 10 && !ok; i++) { Sleep(500); ok = CopyFileW(thisExePath().c_str(), installExePath().c_str(), FALSE); } }
        if (ok) log(L"program copied to " + installExePath());
        else log(L"warning: could not copy the program (" + std::to_wstring(GetLastError()) + L")");
    }
    {
        CoInitialize(nullptr);
        IShellLinkW* sl = nullptr;
        if (SUCCEEDED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_IShellLinkW, (void**)&sl))) {
            sl->SetPath(installExePath().c_str()); sl->SetWorkingDirectory(INSTALL_DIR); sl->SetDescription(L"Enhancer Reloaded");
            IPersistFile* pf = nullptr;
            if (SUCCEEDED(sl->QueryInterface(IID_IPersistFile, (void**)&pf))) { if (SUCCEEDED(pf->Save(startMenuShortcut().c_str(), TRUE))) log(L"Start Menu shortcut created"); pf->Release(); }
            sl->Release();
        }
        CoUninitialize();
        HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_READ | KEY_SET_VALUE, &k) == ERROR_SUCCESS) {
            wchar_t buf[MAX_PATH * 2]; DWORD sz = sizeof buf;
            if (RegQueryValueExW(k, L"EnhancerAPO", nullptr, nullptr, (BYTE*)buf, &sz) == ERROR_SUCCESS) { std::wstring cmd = L"\"" + installExePath() + L"\" --tray"; RegSetValueExW(k, L"EnhancerAPO", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t))); }
            RegCloseKey(k);
        }
    }
    // 5c. "Apps & Features" entry so Windows can uninstall us like any other program
    {
        HKEY k;
        if (RegCreateKeyExW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY, 0, nullptr, 0, KEY_WRITE | KEY_WOW64_64KEY, nullptr, &k, nullptr) == ERROR_SUCCESS) {
            auto s = [&](const wchar_t* n, const std::wstring& v) { RegSetValueExW(k, n, 0, REG_SZ, (const BYTE*)v.c_str(), (DWORD)((v.size() + 1) * sizeof(wchar_t))); };
            auto d = [&](const wchar_t* n, DWORD v) { RegSetValueExW(k, n, 0, REG_DWORD, (const BYTE*)&v, sizeof v); };
            s(L"DisplayName", L"Enhancer Reloaded"); s(L"DisplayVersion", L"1.2.0"); s(L"Publisher", L"josiaslg");
            s(L"URLInfoAbout", L"https://github.com/josiaslg/Enhancer-Reloaded"); s(L"InstallLocation", INSTALL_DIR); s(L"DisplayIcon", installExePath());
            s(L"UninstallString", L"\"" + installExePath() + L"\" --uninstall-ui"); s(L"QuietUninstallString", L"\"" + installExePath() + L"\" --uninstall");
            d(L"NoModify", 1); d(L"NoRepair", 1); d(L"EstimatedSize", 900);
            RegCloseKey(k); log(L"Apps & Features entry created");
        }
    }
    // 6. retire the old PowerShell-based install directory if present (same CLSID, now re-pointed to the new DLL)
    if (fileExists(OLD_DIR)) {   // only the files the old install had created there
        DeleteFileW((std::wstring(OLD_DIR) + L"\\EnhancerAPO.dll").c_str());
        WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((std::wstring(OLD_DIR) + L"\\backup*.json").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) { do { DeleteFileW((std::wstring(OLD_DIR) + L"\\" + fd.cFileName).c_str()); } while (FindNextFileW(h, &fd)); FindClose(h); }
        RemoveDirectoryW(OLD_DIR);
    }
    // 7. restart the audio stack so the endpoint graphs are rebuilt with the APO
    log(L"Restarting the Windows audio service...");
    stopAudio(); startAudio();
    if (count == 0) return fail(7, L"no active render endpoint with an FxProperties key", false);
    log(L"Done (" + std::to_wstring(count) + L" outputs)"); flushLog();
    return 0;
}

// ---------------------------------------------------------------- uninstall (elevated)
static int runUninstall() {
    if (!isAdmin()) return 2;
    if (!acquireSetupLock()) return 8;
    struct Unlock { ~Unlock() { releaseSetupLock(); } } unlock;
    DeleteFileW(logPath().c_str()); g_log.clear();
    log(L"Stopping the Windows audio service...");
    stopAudio();
    log(L"Restoring the original settings of every output...");
    // endpoints
    WIN32_FIND_DATAW fd; HANDLE h = FindFirstFileW((std::wstring(INSTALL_DIR) + L"\\backup-{*.txt").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            std::wstring file = std::wstring(INSTALL_DIR) + L"\\" + fd.cFileName; std::wstring t = readText(file);
            std::wstring guid = std::wstring(fd.cFileName).substr(7); guid = guid.substr(0, guid.size() - 4);
            HKEY k = openSetValue(HKEY_LOCAL_MACHINE, std::wstring(RENDER_ROOT) + L"\\" + guid + L"\\FxProperties");
            if (k) {
                std::wstring mode = iniValue(t, L"ModeEffect"), modes = iniValue(t, L"Modes"), dis = iniValue(t, L"DisableSysFx");
                if (mode == NONE) RegDeleteValueW(k, K_MODEFX); else RegSetValueExW(k, K_MODEFX, 0, REG_SZ, (const BYTE*)mode.c_str(), (DWORD)((mode.size() + 1) * sizeof(wchar_t)));
                if (modes == NONE) RegDeleteValueW(k, K_MFXMODES); else setMulti(k, K_MFXMODES, modes);
                if (dis == NONE) RegDeleteValueW(k, K_DISABLEFX); else { DWORD d = _wtoi(dis.c_str()); RegSetValueExW(k, K_DISABLEFX, 0, REG_DWORD, (const BYTE*)&d, sizeof d); }
                RegCloseKey(k);
            }
            DeleteFileW(file.c_str());
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    // DisableProtectedAudioDG
    {
        std::wstring bk = std::wstring(INSTALL_DIR) + L"\\backup-audiodg.txt";
        if (fileExists(bk)) { std::wstring prev = iniValue(readText(bk), L"Prev"); HKEY k = openSetValue(HKEY_LOCAL_MACHINE, AUDIO_KEY);
            if (k) { if (prev == NONE || prev.empty()) RegDeleteValueW(k, L"DisableProtectedAudioDG"); else { DWORD d = _wtoi(prev.c_str()); RegSetValueExW(k, L"DisableProtectedAudioDG", 0, REG_DWORD, (const BYTE*)&d, sizeof d); } RegCloseKey(k); }
            DeleteFileW(bk.c_str()); }
    }
    // COM / APO registration, then files
    { HMODULE m = LoadLibraryW(installDllPath().c_str()); if (m) { auto un = (HRESULT(WINAPI*)())GetProcAddress(m, "DllUnregisterServer"); if (un) un(); FreeLibrary(m); } }
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (std::wstring(L"AudioEngine\\AudioProcessingObjects\\") + CLSID_STR).c_str());
    RegDeleteTreeW(HKEY_CLASSES_ROOT, (std::wstring(L"CLSID\\") + CLSID_STR).c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, PARAM_KEY);
    log(L"Removing the installed files...");
    for (int i = 0; i < 40 && !DeleteFileW(installDllPath().c_str()); i++) Sleep(250);   // audio is still stopped here
    DeleteFileW((installDllPath() + L".old").c_str());
    log(L"Restarting the Windows audio service...");
    startAudio();
    DeleteFileW((std::wstring(INSTALL_DIR) + L"\\install.log").c_str());
    DeleteFileW(startMenuShortcut().c_str());
    RegDeleteTreeW(HKEY_LOCAL_MACHINE, UNINSTALL_KEY);
    { HKEY k; if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0, KEY_SET_VALUE, &k) == ERROR_SUCCESS) { RegDeleteValueW(k, L"EnhancerAPO"); RegCloseKey(k); } }
    { std::wstring ad = appDataDir(); DeleteFileW((ad + L"\\EnhancerReloaded.ini").c_str()); RemoveDirectoryW(ad.c_str()); }
    // the installed exe may be this very process (or its parent panel): delete it a few seconds later from a
    // detached, still elevated cmd, then remove the (now empty) directory
    std::wstring cmd = L"cmd.exe /c ping 127.0.0.1 -n 4 >nul & del /f /q \"" + installExePath() + L"\" & rmdir \"" + std::wstring(INSTALL_DIR) + L"\"";
    STARTUPINFOW si{}; si.cb = sizeof si; PROCESS_INFORMATION pi{};
    if (CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_NO_WINDOW | DETACHED_PROCESS, nullptr, nullptr, &si, &pi)) { CloseHandle(pi.hThread); CloseHandle(pi.hProcess); }
    return 0;
}

// ---------------------------------------------------------------- run ourselves elevated with an argument and wait
static bool elevate(HWND owner, const wchar_t* arg, DWORD* exitCode) {
    wchar_t exe[MAX_PATH]; GetModuleFileNameW(nullptr, exe, MAX_PATH);
    SHELLEXECUTEINFOW sei{}; sei.cbSize = sizeof sei; sei.fMask = SEE_MASK_NOCLOSEPROCESS; sei.hwnd = owner; sei.lpVerb = L"runas"; sei.lpFile = exe; sei.lpParameters = arg; sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) return false;   // UAC refused
    WaitForSingleObject(sei.hProcess, INFINITE); DWORD code = 1; GetExitCodeProcess(sei.hProcess, &code); CloseHandle(sei.hProcess);
    if (exitCode) *exitCode = code; return true;
}

} // namespace inst
