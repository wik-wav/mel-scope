# Builds MelScope.clap and the verification tools.
# Toolchain lives in _tools/ (MinGW-w64 GCC + CLAP headers); see README.md.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$tools = Join-Path $root '_tools'
$gpp = Join-Path $tools 'mingw64\bin\g++.exe'
$clap = Join-Path $tools 'clap\include'

if (-not (Test-Path $gpp)) { throw "compiler not found: $gpp" }
if (-not (Test-Path $clap)) { throw "CLAP headers not found: $clap" }

# gcc needs a writable TMPDIR
$env:TMP = Join-Path $tools 'tmp'
$env:TEMP = $env:TMP
New-Item -ItemType Directory -Force -Path $env:TMP | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root 'build') | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $root 'dist') | Out-Null

$src = @('src\Plugin.cpp', 'src\Gui.cpp', 'src\MelCore.cpp')
$common = @("-O2", "-std=c++17", "-I$root\src", "-I$clap")

Push-Location $root
try {
    Write-Host 'building MelScope.clap ...'
    & $gpp @common -shared -static -o build\MelScope.clap @src `
        -lgdiplus -lgdi32 -luser32 -lole32 -luuid -lcomdlg32
    if ($LASTEXITCODE -ne 0) { throw 'plugin build failed' }

    Write-Host 'building hosttest.exe ...'
    & $gpp @common -static -o build\hosttest.exe hosttest\main.cpp src\MelCore.cpp `
        -lgdi32 -luser32 -lole32 -luuid
    if ($LASTEXITCODE -ne 0) { throw 'hosttest build failed' }

    Write-Host 'building selftest.exe ...'
    & $gpp @common -static -o build\selftest.exe offline\selftest.cpp src\MelCore.cpp
    if ($LASTEXITCODE -ne 0) { throw 'selftest build failed' }

    Write-Host 'building melrender.exe ...'
    & $gpp @common -static -o build\melrender.exe offline\melrender.cpp src\MelCore.cpp
    if ($LASTEXITCODE -ne 0) { throw 'melrender build failed' }

    Copy-Item build\MelScope.clap dist\MelScope.clap -Force
    Write-Host ''
    Write-Host ('built: ' + (Get-Item build\MelScope.clap).Length + ' bytes -> build\MelScope.clap, dist\MelScope.clap')
} finally {
    Pop-Location
}
