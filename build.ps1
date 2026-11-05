# Build SuperHangOn.exe on Windows. Needs llvm-mingw (https://github.com/mstorsjo/llvm-mingw/releases):
#   .\build.ps1 -Toolchain C:\llvm-mingw        (or set the LLVM_MINGW environment variable once)
# Output: source\build\win\SuperHangOn.exe
param([string]$Toolchain = $env:LLVM_MINGW, [switch]$Test)
& "$PSScriptRoot\source\build_win.ps1" -Toolchain $Toolchain -Test:$Test
