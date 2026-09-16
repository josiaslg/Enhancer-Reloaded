@echo off
rem Enhancer Reloaded - build everything with MSVC 2022 (x64) + Windows 10/11 SDK. Run from any prompt.
setlocal
set VCVARS="%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist %VCVARS% set VCVARS="%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul 2>&1 || (echo vcvars64.bat not found - install Visual Studio 2022 Build Tools with the Windows SDK & exit /b 1)
cd /d "%~dp0"
if not exist build mkdir build
echo === EnhancerAPO.dll (audio processing object)
cl /nologo /O2 /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE /LD EnhancerAPO.cpp /Fo:build\ /Fe:build\EnhancerAPO.dll /link /DEF:EnhancerAPO.def ole32.lib advapi32.lib audiobaseprocessingobject.lib legacy_stdio_definitions.lib || exit /b 1
copy /y build\EnhancerAPO.dll EnhancerAPO.dll >nul
echo === EnhancerReloaded.exe (panel + installer, embeds the DLL)
rc /nologo /fo build\EnhancerReloaded.res EnhancerReloaded.rc || exit /b 1
cl /nologo /O2 /EHsc /std:c++17 /W3 /DUNICODE /D_UNICODE EnhancerReloaded.cpp build\EnhancerReloaded.res /Fo:build\ /Fe:build\EnhancerReloaded.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib shell32.lib advapi32.lib ole32.lib comctl32.lib || exit /b 1
del EnhancerAPO.dll
echo === tools
for %%t in (test_apo test_dsp apodev apometer aporender) do cl /nologo /O2 /EHsc /std:c++17 /DUNICODE /D_UNICODE tools\%%t.cpp /I. /Fo:build\ /Fe:build\%%t.exe /link ole32.lib >nul || exit /b 1
if not exist ..\dist mkdir ..\dist
copy /y build\EnhancerReloaded.exe ..\dist\EnhancerReloaded.exe >nul
echo Done: build\EnhancerReloaded.exe (copied to ..\dist)
