@echo off
setlocal
cd /d "%~dp0\.."
if not exist build mkdir build
gcc.exe tests\TopPinSmoke.c -o build\TopPinSmoke.exe -std=c17 -Os -municode -Wall -Wextra -Wpedantic -luser32
endlocal
