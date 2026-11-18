# build-vst3.ps1 -- builds dist\MelScope.vst3 (a VST3 bundle) with the bundled MinGW.
#
# The CLAP build stays a plain g++ one-liner (build.ps1); this one also compiles the
# handful of VST3 SDK support files a plugin needs, which is why it lives in a script.

param([switch]$RunTests)

$ErrorActionPreference = 'Stop'
# the script lives in vst3\, the project root is one level up
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$tools = Join-Path $root '_tools'
$sdk = Join-Path $tools 'vst3sdk'
$gxx = Join-Path $tools 'mingw64\bin\g++.exe'
$objDir = Join-Path $root 'build-vst3\obj'
$outDir = Join-Path $root 'dist\MelScope.vst3\Contents\x86_64-win'

if (-not (Test-Path $gxx)) { throw "MinGW not found: $gxx" }
if (-not (Test-Path (Join-Path $sdk 'pluginterfaces'))) {
    throw "VST3 SDK missing. Run: git clone --depth 1 --recurse-submodules --shallow-submodules https://github.com/steinbergmedia/vst3sdk.git _tools/vst3sdk"
}

New-Item -ItemType Directory -Force -Path $objDir, $outDir | Out-Null

$env:TMP = Join-Path $tools 'tmp'
$env:TEMP = $env:TMP
$env:PATH = (Join-Path $tools 'mingw64\bin') + ';' + $env:PATH

$flags = @('-DRELEASE=1', '-DNDEBUG', '-DNOMINMAX', '-std=c++17', '-O2', '-fno-exceptions',
           "-I$sdk", "-I$($sdk)\public.sdk", "-I$root\src")
$libs = @('-lgdiplus', '-lgdi32', '-luser32', '-lole32', '-luuid', '-lcomdlg32', '-lshlwapi', '-ldwmapi')

# SDK support sources: the single-component effect pulls in its controller, parameters
# and bus bookkeeping, and the Windows module entry provides InitDll/ExitDll.
$sdkSources = @(
    'public.sdk\source\main\pluginfactory.cpp',
    'public.sdk\source\main\dllmain.cpp',
    'public.sdk\source\vst\vstsinglecomponenteffect.cpp',
    'public.sdk\source\vst\vstcomponentbase.cpp',
    'public.sdk\source\vst\vstcomponent.cpp',
    'public.sdk\source\vst\vstbus.cpp',
    'public.sdk\source\vst\vstparameters.cpp',
    'public.sdk\source\vst\vstaudioeffect.cpp',
    'public.sdk\source\vst\vstinitiids.cpp',
    'base\source\fstring.cpp',
    'base\source\fobject.cpp',
    'base\source\fbuffer.cpp',
    'base\source\baseiids.cpp',
    'base\source\updatehandler.cpp',
    'base\thread\source\flock.cpp',
    'base\thread\source\fcondition.cpp',
    'base\source\fdebug.cpp',
    'public.sdk\source\common\pluginview.cpp',
    'public.sdk\source\common\memorystream.cpp',
    'pluginterfaces\base\coreiids.cpp',
    'pluginterfaces\base\funknown.cpp',
    'pluginterfaces\base\ustring.cpp'
)

$ourSources = @(
    'vst3\Vst3Plugin.cpp',
    'vst3\Vst3Entry.cpp',
    'vst3\Vst3Iids.cpp',
    'src\MelCore.cpp',
    'src\Gui.cpp',
    'src\Plugin.cpp'
)

$objects = @()
foreach ($rel in $sdkSources) {
    $src = Join-Path $sdk $rel
    if (-not (Test-Path $src)) { throw "missing source: $src" }
    $obj = Join-Path $objDir (([IO.Path]::GetFileNameWithoutExtension($rel)) + '.o')
    $objects += $obj
    $srcTime = (Get-Item $src).LastWriteTimeUtc
    if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTimeUtc -ge $srcTime) { continue }
    & $gxx @flags "-I$sdk" "-I$($sdk)\base" -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $rel" }
}

foreach ($rel in $ourSources) {
    $src = Join-Path $root $rel
    if (-not (Test-Path $src)) { throw "missing source: $src" }
    $obj = Join-Path $objDir (([IO.Path]::GetFileNameWithoutExtension($rel)) + '.o')
    $objects += $obj
    $srcTime = (Get-Item $src).LastWriteTimeUtc
    if ((Test-Path $obj) -and (Get-Item $obj).LastWriteTimeUtc -ge $srcTime) { continue }
    & $gxx @flags "-I$($root)\src" "-I$($tools)\clap\include" -c $src -o $obj
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $rel" }
}

$def = Join-Path $objDir 'MelScope.def'
@'
LIBRARY MelScope
EXPORTS
    GetPluginFactory
    InitDll
    ExitDll
'@ | Set-Content $def -Encoding ascii

$dll = Join-Path $outDir 'MelScope.vst3'
& $gxx -shared -static -municode -o $dll @objects $def @libs
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Write-Host ("built " + $dll + " (" + [math]::Round((Get-Item $dll).Length / 1KB) + " KB)")

# host-side smoke test: loads the bundle the way a DAW does
$testObj = Join-Path $objDir 'Vst3HostTest.o'
& $gxx @flags "-I$sdk" -c (Join-Path $root 'vst3\Vst3HostTest.cpp') -o $testObj
if ($LASTEXITCODE -ne 0) { throw "compile failed: Vst3HostTest.cpp" }
$testExe = Join-Path $root 'build-vst3\vst3test.exe'
# link against the same objects as the plugin so every SDK symbol is present
$testObjects = @($testObj) + $objects
& $gxx -static -o $testExe @testObjects @libs
if ($LASTEXITCODE -ne 0) { Write-Host "note: host test link failed (missing SDK helpers?)" }
else {
    Write-Host ("built " + $testExe)
    if ($RunTests) {
        Write-Host "--- host test"
        # the test reports on stderr, which Stop-mode would treat as a failure
        $prevEap = $ErrorActionPreference
        $ErrorActionPreference = 'Continue'
        & $testExe $dll 2>&1 | ForEach-Object { Write-Host $_ }
        $code = $LASTEXITCODE
        $ErrorActionPreference = $prevEap
        if ($code -ne 0) { throw "host test failed" }
    }
}
