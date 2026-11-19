#Requires -Version 5.1
# Test builds for the Windows integration job (.github/workflows/build.yml,
# job "integration"). Run from an MSVC developer shell with Qt on PATH:
#
#   build_windows.ps1 -Name u3 -Mocks          # the program as it is (5.3.0-dev), plus bin\
#   build_windows.ps1 -Name unew3 -Version 9.9.9
#
# The app and the helper are built from the real project files with the
# same MSVC and Qt as a release; only test-only properties differ (see the
# vcxproj): the test defines, the UI test hook, and the fake NVAPI library.
# Qt is deployed and pruned the way build.ps1 does it. Output: out\<Name>\,
# which the runner copies into each scenario's folder.
param(
    [Parameter(Mandatory = $true)] [string] $Name,
    [string] $Version = "5.3.0-dev",
    [switch] $Mocks
)

$ErrorActionPreference = "Stop"
$h    = $PSScriptRoot
$repo = (Resolve-Path "$h\..\..").Path
$bin  = "$h\bin"

function Assert-Ok([string] $What) {
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

# ---- the stand-ins: fake NVIDIA driver, helper programs ----
if ($Mocks) {
    New-Item -ItemType Directory -Force $bin | Out-Null
    Push-Location $bin
    $libs = @("user32.lib", "shell32.lib", "shlwapi.lib", "advapi32.lib")
    try {
        cl /nologo /std:c++17 /EHsc /O2 /utf-8 /LD "/I$repo\nvapi" "$h\mock\nvapi_mock.cpp" /Fe:nvapi64.dll /link /IMPLIB:nvapi64.lib
        Assert-Ok "nvapi mock"
        foreach ($p in "exit3", "fake_tar", "fg", "filelock", "holder", "winlist") {
            cl /nologo /std:c++17 /EHsc /O2 /utf-8 "$h\mock\$p.cpp" /Fe:"$p.exe" /link $libs
            Assert-Ok $p
        }
        foreach ($p in "dummy_game", "ready_then_exit") {
            cl /nologo /std:c++17 /EHsc /O2 /utf-8 "$h\mock\$p.cpp" /Fe:"$p.exe" /link /SUBSYSTEM:WINDOWS $libs
            Assert-Ok $p
        }
        Copy-Item dummy_game.exe gameA.exe
        Copy-Item dummy_game.exe gameB.exe
        Remove-Item *.obj -ErrorAction SilentlyContinue
    } finally {
        Pop-Location
    }
}

# ---- the app and the helper ----
$work = "$h\build\$Name"
$out  = "$h\out\$Name"
foreach ($d in $work, $out) {
    if (Test-Path $d) { Remove-Item -Recurse -Force $d }
}
New-Item -ItemType Directory -Force $out | Out-Null

# Trailing "\\": MSBuild wants a final separator, and a lone "\" before the
# end of an argument can be read as an escaped quote.
$common = @("-p:Configuration=Release", "-p:Platform=x64", "-p:SolutionDir=$repo\\", "-m", "-nologo", "-v:minimal")

MSBuild.exe "$repo\limit-nvpstate\limit-nvpstate.vcxproj" @common `
    "-p:AppVersion=$Version" `
    "-p:LimitnvTestDefines=LIMITNV_TEST_HOOKS%3BLIMITNV_UPDATE_TEST" `
    "-p:LimitnvTestHook=$h\mock\testhook.cpp" `
    "-p:NvapiLib=$bin\nvapi64.lib" `
    "-p:OutDir=$work\app\\" "-p:IntDir=$work\app-obj\\" | Out-Host
Assert-Ok "app build"

MSBuild.exe "$repo\updater-helper\limit-nvpstate-updater.vcxproj" @common `
    "-p:LimitnvTestDefines=LIMITNV_TEST_HOOKS" `
    "-p:OutDir=$work\helper\\" "-p:IntDir=$work\helper-obj\\" | Out-Host
Assert-Ok "helper build"

Copy-Item "$work\app\limit-nvpstate.exe", "$work\helper\limit-nvpstate-updater.exe", "$bin\nvapi64.dll" $out

windeployqt.exe "$out\limit-nvpstate.exe" `
    --release `
    --no-translations `
    --no-system-d3d-compiler `
    --no-opengl-sw `
    --no-angle `
    --no-virtualkeyboard `
    --no-quick-import `
    --no-webkit2 `
    --no-compiler-runtime | Out-Host
Assert-Ok "windeployqt"

# The same pruning as build.ps1 (keep the two lists in step).
foreach ($item in "imageformats", "iconengines", "Qt5Svg.dll", "bearer", "Qt5Network.dll") {
    if (Test-Path "$out\$item") { Remove-Item -Recurse -Force "$out\$item" }
}

Write-Host "built $Name ($Version) into $out"
