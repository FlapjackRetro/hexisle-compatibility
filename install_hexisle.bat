@echo off
rem Cosmic Osmo's Hex Isle - manual install for Windows 10/11 (skips plInstaller.exe)
rem Run as Administrator from the folder containing the disc contents.
rem Usage: install_hexisle.bat [path\to\registry-fix-from-Cyan.reg]
setlocal
cd /d "%~dp0"

net session >nul 2>&1
if errorlevel 1 (
  echo This script must be run as Administrator.
  pause & exit /b 1
)

set "SRC=%~dp0Cosmic Osmo's Hex Isle.app\Contents\Resources\transgaming\c_drive\Cosmic Osmo's Hex Isle"
set "DST=C:\Cosmo"
set "KEY=HKLM\SOFTWARE\WOW6432Node\Cyan Worlds, Inc.\Installer\{B9E0F969-021D-4037-B3E8-E1E15635F164}"

if not exist "%SRC%\plasma.exe" (
  echo Could not find plasma.exe under "%SRC%"
  pause & exit /b 1
)

echo Copying game files to %DST% ...
robocopy "%SRC%" "%DST%" /E /NFL /NDL /NJH /NP
if errorlevel 8 ( echo Copy failed. & pause & exit /b 1 )

rem VC++ 7.1 runtime that plasma.exe imports (msvcrt.dll is a system KnownDLL, not needed)
copy /y "msvcp71.dll" "%DST%\" >nul
copy /y "msvcr71.dll" "%DST%\" >nul

if not "%~1"=="" (
  echo Importing registry fix "%~1" ...
  reg import "%~1" || ( echo Registry import failed. & pause & exit /b 1 )
)

reg query "%KEY%" /v AntiGrandmaCopyProt >nul 2>&1
if errorlevel 1 (
  echo.
  echo WARNING: CD-key registry entry not found. The game will report
  echo "Your CD key is missing or invalid". See README.md, "CD key".
)

echo Creating desktop shortcut ...
powershell -NoProfile -Command "$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')+'\Cosmic Osmo''s Hex Isle.lnk');$s.TargetPath='%DST%\plasma.exe';$s.WorkingDirectory='%DST%';$s.Save()"

echo.
echo Done. Launch %DST%\plasma.exe
pause
