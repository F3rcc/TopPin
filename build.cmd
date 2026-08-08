@echo off
setlocal
cd /d "%~dp0"

where gcc.exe >nul 2>nul || (
  echo [TopPin] gcc.exe not found.
  exit /b 1
)
where windres.exe >nul 2>nul || (
  echo [TopPin] windres.exe not found.
  exit /b 1
)

if not exist build mkdir build
if not exist dist mkdir dist

windres.exe --codepage=65001 TopPin.rc -O coff -o build\TopPin.res.o || exit /b 1
gcc.exe TopPin.c build\TopPin.res.o -o dist\TopPin.exe ^
  -std=c17 -Os -s -municode -mwindows ^
  -Wall -Wextra -Wpedantic ^
  -ffunction-sections -fdata-sections ^
  -fno-asynchronous-unwind-tables -fno-unwind-tables ^
  -Wl,--gc-sections,--dynamicbase,--nxcompat,--high-entropy-va ^
  -luser32 -lshell32 || exit /b 1

for %%F in (dist\TopPin.exe) do echo [TopPin] Built %%~zF bytes: %%F
endlocal
