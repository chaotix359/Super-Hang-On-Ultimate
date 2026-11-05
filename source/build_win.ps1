param([string]$Toolchain = $env:LLVM_MINGW, [switch]$Test)
$ErrorActionPreference = 'Stop'
if ($Toolchain) {
    $cc = Join-Path $Toolchain 'bin\x86_64-w64-mingw32-clang.exe'
    $rc = Join-Path $Toolchain 'bin\llvm-rc.exe'
} else {
    $cc = (Get-Command x86_64-w64-mingw32-clang.exe -ErrorAction Stop).Source
    $rc = (Get-Command llvm-rc.exe -ErrorAction Stop).Source
}
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path 'build\win' -Force | Out-Null
    (Get-ChildItem 'recomp\overrides\*.c' | Sort-Object Name | ForEach-Object { Get-Content -LiteralPath $_.FullName -Raw }) -join "`n" | Set-Content -LiteralPath 'build\win\overrides_all.c' -Encoding utf8
    $sources = @('src\rc_runtime.c','src\gen\main_rc.c','src\gen\sub_rc.c','build\win\overrides_all.c','src\sho_core.c','src\sho_video.c','src\sho_music.c','src\sho_md.c','src\sho_mdgfx.c','src\sho_mdcourse.c','src\sho_mdsnd.c','src\third_party\musashi\m68kcpu.c','src\third_party\musashi\m68kops.c','src\third_party\musashi\m68kdasm.c','src\third_party\musashi\softfloat\softfloat.c','src\third_party\z80\z80.c','src\third_party\nukedopn2\ym3438.c','src\third_party\nukedopm\opm.c','src\third_party\puff\puff.c')
    if ($Test) { $sources += 'src\test_main.c' } else { $sources += @('src\win32_main.c','src\win32_launcher.c','src\win32_d3d11.c') }
    $objects = foreach ($source in $sources) {
        $obj = Join-Path 'build\win' ([IO.Path]::GetFileNameWithoutExtension($source) + '.o')
        & $cc -O2 -DNDEBUG -fno-strict-aliasing -w -Isrc -c $source -o $obj
        if ($LASTEXITCODE) { throw "Compilation failed: $source" }
        $obj
    }
    if ($Test) { & $cc @objects -static -o 'build\win\shotest.exe' } else {
        & $rc /FO 'build\win\app.res' 'res\app.rc'
        if ($LASTEXITCODE) { throw 'Resource compilation failed' }
        & $cc @objects 'build\win\app.res' -static -mwindows -lwinmm -ld3d11 -ldxgi -o 'build\win\SuperHangOn.exe'
    }
    if ($LASTEXITCODE) { throw 'Link failed' }
    Write-Output 'Build complete: source\build\win'
} finally { Pop-Location }
