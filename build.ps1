#Requires -Version 5.1
$ErrorActionPreference = "Stop"

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
    MSBuild.exe ".\limit-nvpstate.sln" `
        -p:Configuration=Release `
        -p:Platform=x64 `
        -m `
        -nologo `
        -v:minimal

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
        --no-compiler-runtime

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

$exitCode = Invoke-Build
Write-Host
exit $exitCode
