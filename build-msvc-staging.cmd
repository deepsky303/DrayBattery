@echo off
setlocal
set "VS_VARS="
for %%V in (
  "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
  "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
) do if not defined VS_VARS if exist "%%~V" set "VS_VARS=%%~V"
if not defined VS_VARS (
  echo Visual Studio 2022 C++ tools not found.
  exit /b 2
)
call "%VS_VARS%" >nul
if errorlevel 1 exit /b 3
cd /d "%~dp0"
if not exist build mkdir build
if not exist build\staging mkdir build\staging
if not exist dist mkdir dist
rc /nologo /I "res" /fo "build\app.res" "res\app.rc"
if errorlevel 1 exit /b 4
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /W3 /DNOMINMAX /DUNICODE /D_UNICODE /D__FIReference_1_INT32=__FIReference_1_int /D_WIN32_WINNT=0x0A00 /DWINVER=0x0A00 /I "src" /Fo"build\\" /Fe"build\staging\DrayBattery.exe" src\*.cpp src\drivers\*.cpp "build\app.res" /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF hid.lib setupapi.lib cfgmgr32.lib ole32.lib comctl32.lib gdi32.lib runtimeobject.lib winhttp.lib shell32.lib user32.lib advapi32.lib winmm.lib
if errorlevel 1 exit /b 5
echo Build succeeded: build\staging\DrayBattery.exe
exit /b 0
