#Requires -Version 5.1
param(
    # Compiled in as the program version (About box, Check for updates).
    # Empty: the project's default (a -dev version).
    [string] $Version = ""
)

$ErrorActionPreference = "Stop"

if ($Version -and $Version -notmatch '^[0-9]{1,9}(\.[0-9]{1,9}){0,3}$') {
    Write-Host "error: -Version must look like 5.3.0 (got '$Version')" -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------------------
# Changes from the original:
#   - no administrator gate: nothing in this script needs elevation
#   - MSBuild exit code is actually checked, so a failed build no longer copies
#     a stale executable and reports success
#   - -m for parallel compilation, -v:m to cut the log noise
#   - windeployqt output is pruned: with the icon as PNG nothing needs the
#     imageformats or iconengines plugins any more
#   - the PDB is deliberately not packaged
# ---------------------------------------------------------------------------

function Get-DirectorySize([string] $Path) {
    if (-not (Test-Path $Path)) { return 0 }
    $bytes = (Get-ChildItem -Path $Path -Recurse -File | Measure-Object -Property Length -Sum).Sum
    return [math]::Round($bytes / 1MB, 2)
}

function Invoke-Build {
    $buildRoot = ".\build"
    $stageDir  = ".\build\limit-nvpstate"

    if (Test-Path $buildRoot) {
        Remove-Item -Path $buildRoot -Recurse -Force
    }

    New-Item -ItemType Directory -Path $stageDir -Force | Out-Null

    # ---- compile ----
    $versionArgs = @()
    if ($Version) { $versionArgs = @("-p:AppVersion=$Version") }

    MSBuild.exe ".\limit-nvpstate.sln" `
        -p:Configuration=Release `
        -p:Platform=x64 `
        @versionArgs `
        -m `
        -nologo `
        -v:minimal | Out-Host

    if ($LASTEXITCODE -ne 0) {
        Write-Host "error: build failed (exit code $LASTEXITCODE)" -ForegroundColor Red
        return 1
    }

    $exePath = ".\x64\Release\limit-nvpstate.exe"

    if (-not (Test-Path $exePath)) {
        Write-Host "error: $exePath was not produced" -ForegroundColor Red
        return 1
    }

    # ---- stage ----
    Copy-Item $exePath          $stageDir
    Copy-Item ".\x64\Release\config.json" $stageDir

    # ---- Qt runtime ----
    windeployqt.exe "$stageDir\limit-nvpstate.exe" `
        --release `
        --no-translations `
        --no-system-d3d-compiler `
        --no-opengl-sw `
        --no-angle `
        --no-virtualkeyboard `
        --no-quick-import `
        --no-webkit2 `
        --no-compiler-runtime | Out-Host

    if ($LASTEXITCODE -ne 0) {
        Write-Host "error: windeployqt failed (exit code $LASTEXITCODE)" -ForegroundColor Red
        return 1
    }

    $sizeBefore = Get-DirectorySize $stageDir

    # ---- prune ----
    # The tray and window icons are PNG, which Qt5Gui decodes internally, so the
    # image format plugins (and the Qt5Svg they drag in) are dead weight.
    $prune = @(
        "$stageDir\imageformats",
        "$stageDir\iconengines",
        "$stageDir\Qt5Svg.dll",
        "$stageDir\bearer",
        "$stageDir\Qt5Network.dll"
    )

    foreach ($item in $prune) {
        if (Test-Path $item) {
            Remove-Item -Path $item -Recurse -Force
            Write-Host "pruned $item"
        }
    }

    # Optional, saves roughly 200 KB but drops the native Windows look. Only
    # remove this if you also call QApplication::setStyle("fusion") in main().
    #
    # Remove-Item -Path "$stageDir\styles" -Recurse -Force

    $sizeAfter = Get-DirectorySize $stageDir

    Write-Host ""
    Write-Host ("package: {0} MB (was {1} MB before pruning)" -f $sizeAfter, $sizeBefore) -ForegroundColor Green
    Write-Host "output:  $stageDir"

    return 0
}

# Everything above writes with Write-Host/Out-Host, so the only thing on the
# output stream is the return code. (Before, MSBuild's output was captured into
# $exitCode: errors were never shown and the step could not fail.)
$exitCode = Invoke-Build
Write-Host
exit ([int]($exitCode | Select-Object -Last 1))
