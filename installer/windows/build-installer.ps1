<#
  Builds SuperHangOnUltimate-Setup-<version>.exe  (run this on Windows)

      powershell -ExecutionPolicy Bypass -File installer\windows\build-installer.ps1

  Needs  - Inno Setup 6.3 or newer (free, https://jrsoftware.org/isdl.php)
         - internet on the first run (downloads Python and LLVM-MinGW into installer\windows\cache)

  The installer contains ONLY the project source + a compiler + Python. This script stops if anything translated from the
  ROM (src\gen, recomp\overrides), a ROM, or a built program ends up in what would be packaged.
#>
param(
    [string]$Version   = "0.9.34",
    [string]$Iscc      = "",
    [string]$PythonUrl = "https://www.python.org/ftp/python/3.12.10/python-3.12.10-embed-amd64.zip",
    [string]$LlvmUrl   = "https://github.com/mstorsjo/llvm-mingw/releases/download/20261020/llvm-mingw-20261020-ucrt-x86_64.zip",
    [switch]$KeepAllTargets
)
$ErrorActionPreference = 'Stop'
$ProgressPreference    = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$here  = $PSScriptRoot
$root  = (Resolve-Path (Join-Path $here '..\..')).Path
$cache = Join-Path $here 'cache'
$stage = Join-Path $here 'stage'
$lock  = Join-Path $here 'downloads.lock'

function Say($m) { Write-Host "==> $m" -ForegroundColor Cyan }

# ---------------------------------------------------------------- Inno Setup
if (-not $Iscc) {
    $cands = @(
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe", "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 7\ISCC.exe", "$env:ProgramFiles\Inno Setup 7\ISCC.exe",
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe", "$env:LOCALAPPDATA\Programs\Inno Setup 7\ISCC.exe")
    $Iscc = $cands | Where-Object { Test-Path $_ } | Select-Object -First 1
}
if (-not $Iscc -or -not (Test-Path $Iscc)) { throw "Inno Setup not found. Install it from https://jrsoftware.org/isdl.php or pass -Iscc <path to ISCC.exe>." }
Say "Inno Setup: $Iscc"

# ---------------------------------------------------------------- downloads (cached, hash-pinned after the first run)
New-Item -ItemType Directory -Force -Path $cache | Out-Null
$pins = @{}
if (Test-Path $lock) { Get-Content $lock | ForEach-Object { if ($_ -match '^([0-9a-f]{64})\s+(\S+)$') { $pins[$Matches[2]] = $Matches[1] } } }

function Get-Cached([string]$url) {
    $name = [IO.Path]::GetFileName($url)
    $file = Join-Path $cache $name
    if (-not (Test-Path $file)) { Say "Downloading $name"; Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $file }
    $h = (Get-FileHash -Algorithm SHA256 $file).Hash.ToLower()
    if ($pins.ContainsKey($name)) {
        if ($pins[$name] -ne $h) { Remove-Item $file; throw "SHA-256 of $name does not match downloads.lock - the file was deleted. Check the download source and try again." }
    } else {
        Add-Content -Path $lock -Value "$h  $name"
        Write-Host "    first download of $name - its SHA-256 ($h) is now recorded in downloads.lock; commit that file." -ForegroundColor Yellow
    }
    return $file
}
$pyZip   = Get-Cached $PythonUrl
$llvmZip = Get-Cached $LlvmUrl

function Expand-Zip([string]$zip, [string]$dest) {
    New-Item -ItemType Directory -Force -Path $dest | Out-Null
    if (Get-Command tar.exe -ErrorAction SilentlyContinue) { & tar.exe -xf $zip -C $dest; if ($LASTEXITCODE) { throw "tar failed on $zip" } }
    else { Expand-Archive -Path $zip -DestinationPath $dest -Force }
}

# ---------------------------------------------------------------- stage a clean copy of the source
Say "Staging a clean copy of the source"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force -Path (Join-Path $stage 'src') | Out-Null
& robocopy.exe $root (Join-Path $stage 'src') /E `
    /XD .git installer build gen overrides cov music __pycache__ .vscode .idea `
    /XF *.exe *.zip *.o *.obj *.dasm *.sav *.nv shangon.ini gamepad.log .DS_Store Thumbs.db `
    /NFL /NDL /NJH /NJS /NP | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed ($LASTEXITCODE)" }

# ---------------------------------------------------------------- safety checks: nothing of SEGA's may be packaged
Say "Checking that nothing ROM-derived is included"
$src = Join-Path $stage 'src'
$bad = @()
foreach ($p in @('source\src\gen', 'source\recomp\overrides', 'source\build')) { if (Test-Path (Join-Path $src $p)) { $bad += $p } }
Get-ChildItem $src -Recurse -File | Where-Object {
    ($_.Extension -in '.exe', '.zip', '.dasm', '.sav', '.nv', '.ini', '.md', '.gen', '.smd' -and $_.Name -notmatch '^(README|NOTICE|LICENSE).*\.md$') -or
    ($_.Extension -eq '.bin' -and $_.Name -ne 'coverage.bin')
} | ForEach-Object { $bad += $_.FullName.Substring($src.Length + 1) }
if ($bad.Count) { throw ("Refusing to build - these must not be in the installer:`n  " + ($bad -join "`n  ")) }
foreach ($need in @('setup.py', 'LICENSE', 'NOTICE.md', 'README.txt', 'source\recomp\coverage.bin', 'source\recomp\recomp.py', 'source\build_win.ps1', 'source\res\shangon.ico')) {
    if (-not (Test-Path (Join-Path $src $need))) { throw "Missing from the staged source: $need" }
}
if ((Get-ChildItem (Join-Path $src 'source\recomp\patches') -Filter *.patch).Count -lt 1) { throw "No patches in source\recomp\patches - run: python setup.py --make-patches" }

# ---------------------------------------------------------------- tools that travel with the installer
Say "Unpacking Python"
Expand-Zip $pyZip (Join-Path $stage 'python')
if (-not (Test-Path (Join-Path $stage 'python\python.exe'))) { throw "python.exe not found after unpacking" }

Say "Unpacking LLVM-MinGW (large - this takes a minute)"
$tmpLlvm = Join-Path $stage '_llvm'
Expand-Zip $llvmZip $tmpLlvm
$inner = Get-ChildItem $tmpLlvm -Directory | Select-Object -First 1
Move-Item $inner.FullName (Join-Path $stage 'llvm-mingw')
Remove-Item $tmpLlvm -Recurse -Force
if (-not $KeepAllTargets) {      # the game is built for 64-bit Windows only
    foreach ($t in 'aarch64-w64-mingw32', 'armv7-w64-mingw32', 'i686-w64-mingw32') {
        $d = Join-Path $stage "llvm-mingw\$t"; if (Test-Path $d) { Remove-Item $d -Recurse -Force }
    }
}
foreach ($need in @('bin\x86_64-w64-mingw32-clang.exe', 'bin\llvm-rc.exe', 'x86_64-w64-mingw32')) {
    if (-not (Test-Path (Join-Path $stage "llvm-mingw\$need"))) { throw "LLVM-MinGW is missing $need - try -KeepAllTargets or a different -LlvmUrl" }
}

# ---------------------------------------------------------------- build the installer
Say "Compiling the installer (several minutes - it compresses about 700 MB)"
Push-Location $here
try { & $Iscc "/DAppVersion=$Version" "/DStageDir=$stage" "SuperHangOn.iss"; if ($LASTEXITCODE) { throw "ISCC failed ($LASTEXITCODE)" } }
finally { Pop-Location }

$out = Join-Path $here "output\SuperHangOnUltimate-Setup-$Version.exe"
Say ("Done: {0}  ({1:N0} MB)" -f $out, ((Get-Item $out).Length / 1MB))
Write-Host "Windows SmartScreen will warn about this unsigned installer: 'More info' > 'Run anyway'." -ForegroundColor Yellow
