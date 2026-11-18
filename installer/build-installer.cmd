@echo off
setlocal
cd /d "%~dp0"
set "ISCC=%LOCALAPPDATA%\Programs\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not exist "%ISCC%" (
  echo Inno Setup 6 compiler not found.
  exit /b 2
)
if not exist "..\dist\DrayBattery.exe" (
  call "..\build-msvc.cmd"
  if errorlevel 1 exit /b 3
)
"%ISCC%" "DrayBattery.iss"
if errorlevel 1 exit /b 4
echo Installer ready in dist.
exit /b 0
