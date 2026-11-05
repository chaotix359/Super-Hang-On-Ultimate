@echo off
rem Runs during installation: builds SuperHangOn.exe from the user's ROM.
rem   %1 = folder holding src\, python\ and llvm-mingw\      %2 = the game folder being installed to
setlocal
set "B=%~1"
set "APP=%~2"
set "PY=%B%\python\python.exe"
set "LLVM=%B%\llvm-mingw"
set "SRC=%B%\src"
set PYTHONUTF8=1
set PYTHONDONTWRITEBYTECODE=1

echo [build] game folder: %APP%
echo [build] python: 
"%PY%" -V
if errorlevel 1 exit /b 9
cd /d "%SRC%"
if errorlevel 1 exit /b 9

echo [build] step 1: translating the game from your ROM
"%PY%" setup.py --rom "%APP%\shangon.zip" --cc "%LLVM%\bin\x86_64-w64-mingw32-clang.exe"
if errorlevel 1 exit /b 10

echo [build] step 2: compiling SuperHangOn.exe
powershell -NoProfile -ExecutionPolicy Bypass -File "%SRC%\source\build_win.ps1" -Toolchain "%LLVM%"
if errorlevel 1 exit /b 11

if not exist "%SRC%\source\build\win\SuperHangOn.exe" exit /b 12
copy /y "%SRC%\source\build\win\SuperHangOn.exe" "%APP%\SuperHangOn.exe" >nul
if errorlevel 1 exit /b 13
echo [build] done
exit /b 0
