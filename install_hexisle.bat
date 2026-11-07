@echo off
rem Cosmic Osmo's Hex Isle - installer-free setup for Windows 10/11
rem Run as Administrator from the folder containing the disc contents.
rem Usage: install_hexisle.bat [CDKEY ^| path\to\registry-fix.reg]
rem   With no argument you are asked for the CD key printed in your game case.
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo This script must be run as Administrator.
  pause & exit /b 1
)

set "SRC=%~dp0Cosmic Osmo's Hex Isle.app\Contents\Resources\transgaming\c_drive\Cosmic Osmo's Hex Isle"
set "DST=C:\Cosmo"
set "PS=powershell -NoProfile -ExecutionPolicy Bypass -File"

if not exist "%SRC%\plasma.exe" (
  echo Could not find plasma.exe under "%SRC%"
  pause & exit /b 1
)

echo [1/4] Copying game files to %DST% ...
robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NP
if errorlevel 8 ( echo Copy failed. & pause & exit /b 1 )
rem VC++ 7.1 runtime that plasma.exe imports (msvcrt.dll is a system KnownDLL, not needed)
copy /y "msvcp71.dll" "%DST%\" >nul
copy /y "msvcr71.dll" "%DST%\" >nul

echo [2/4] CD key ...
if /i "%~x1"==".reg" (
  %PS% "%~dp0tools\set_cdkey.ps1" -RegFile "%~f1" -InstallPath "%DST%" || ( pause & exit /b 1 )
) else if not "%~1"=="" (
  %PS% "%~dp0tools\set_cdkey.ps1" -Key "%~1" -InstallPath "%DST%" || ( pause & exit /b 1 )
) else (
  %PS% "%~dp0tools\set_cdkey.ps1" -InstallPath "%DST%" || ( pause & exit /b 1 )
)

echo [3/4] Patching plasma.exe ...
%PS% "%~dp0tools\patch_plasma.ps1" -Exe "%DST%\plasma.exe" || ( pause & exit /b 1 )

echo [4/4] hexfix (optional) ...
if exist "%~dp0hexfix\d3d9.dll" (
  copy /y "%~dp0hexfix\d3d9.dll" "%DST%\" >nul
  if not exist "%DST%\hexfix.ini" copy "%~dp0hexfix\hexfix.ini" "%DST%\" >nul
  echo hexfix installed. Settings: %DST%\hexfix.ini
) else (
  echo hexfix\d3d9.dll not built - skipped. Run hexfix\build.bat to enable frame cap / VSync options.
)

echo Creating desktop shortcut ...
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Cosmic Osmo''s Hex Isle.lnk');$s.TargetPath='%DST%\plasma.exe';$s.WorkingDirectory='%DST%';$s.Save()"

echo.
echo Done. Launch %DST%\plasma.exe
pause
