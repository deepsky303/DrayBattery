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
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I "src" /Fe"build\rapoo_protocol_test.exe" tests\rapoo_protocol_test.cpp
if errorlevel 1 exit /b 3
"build\rapoo_protocol_test.exe"
if errorlevel 1 exit /b 4
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I "src" /Fe"build\rapoo_driver_test.exe" tests\rapoo_driver_test.cpp src\drivers\rapoo.cpp
if errorlevel 1 exit /b 5
"build\rapoo_driver_test.exe"
if errorlevel 1 exit /b 6
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0A00 /I "src" /Fe"build\palette_test.exe" tests\palette_test.cpp src\palette.cpp /link user32.lib gdi32.lib
if errorlevel 1 exit /b 7
"build\palette_test.exe"
if errorlevel 1 exit /b 8
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /I "src" /Fe"build\poll_schedule_test.exe" tests\poll_schedule_test.cpp
if errorlevel 1 exit /b 9
"build\poll_schedule_test.exe"
if errorlevel 1 exit /b 10
cl /nologo /std:c++17 /utf-8 /EHsc /MT /O2 /I "src" /Fe"build\logical_identity_test.exe" tests\logical_identity_test.cpp
if errorlevel 1 exit /b 11
"build\logical_identity_test.exe"
if errorlevel 1 exit /b 12
echo ALL_TESTS_PASSED
