# OfflineKB Windows 构建脚本
# 用法: .\scripts\build.ps1 [-Target OfflineKB|offlinekb-cli|all]

param(
    [ValidateSet("OfflineKB", "offlinekb-cli", "all")]
    [string]$Target = "all"
)

$ErrorActionPreference = "Stop"

# MSYS2 Qt 工具链必须在 PATH 中，否则 uic.exe 会因缺少 DLL 失败 (0xC0000139)
$env:PATH = "F:/msys2/mingw64/bin;F:/msys2/mingw64/share/qt6/bin;" + $env:PATH

$Root = Split-Path -Parent $PSScriptRoot
$BuildDir = Join-Path $Root "build"

if (-not (Test-Path $BuildDir)) {
    New-Item -ItemType Directory -Path $BuildDir | Out-Null
}

Set-Location $BuildDir
cmake ..
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

switch ($Target) {
    "OfflineKB" { cmake --build . --target OfflineKB -j 8 }
    "offlinekb-cli" { cmake --build . --target offlinekb-cli -j 8 }
    "all" { cmake --build . -j 8 }
}

if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host ""
Write-Host "Build OK."
Write-Host "  GUI:  $BuildDir/OfflineKB.exe"
Write-Host "  CLI:  $BuildDir/offlinekb-cli.exe"
