$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot

# Refresh PATH from registry
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machinePath;$userPath"

$checkOnly = $args -contains "--check"
$action = if ($checkOnly) { "Checking format" } else { "Formatting" }

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " UzEngine $action" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$failed = @()

# C++ modules (clang-format)
$clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue

# Runtime — scan src/, core/, platform/, demos/, tools/, tests/
$runtimePath = Join-Path $Root "runtime"
if (Test-Path (Join-Path $runtimePath ".clang-format")) {
    Write-Host "[runtime] clang-format..." -ForegroundColor Yellow
    if ($clangFormat) {
        $runtimeDirs = @("src", "core", "platform", "demos", "tools", "tests")
        $sources = @()
        foreach ($dir in $runtimeDirs) {
            $dirPath = Join-Path $runtimePath $dir
            if (Test-Path $dirPath) {
                $sources += Get-ChildItem -Path $dirPath -Include "*.cpp","*.h" -Recurse | Select-Object -ExpandProperty FullName
            }
        }
        if ($sources) {
            if ($checkOnly) {
                & clang-format --dry-run --Werror $sources
            } else {
                & clang-format -i $sources
            }
            if ($LASTEXITCODE -ne 0) { $failed += "runtime" }
            else { Write-Host "[runtime] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[runtime] SKIP - clang-format not found" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# N-API module
$napiPath = Join-Path $Root "napi"
if (Test-Path (Join-Path $napiPath ".clang-format")) {
    $napiSrc = Join-Path $napiPath "src"
    Write-Host "[napi] clang-format..." -ForegroundColor Yellow
    if ($clangFormat) {
        $sources = Get-ChildItem -Path $napiSrc -Include "*.cpp","*.h" -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        if ($sources) {
            if ($checkOnly) {
                & clang-format --dry-run --Werror $sources
            } else {
                & clang-format -i $sources
            }
            if ($LASTEXITCODE -ne 0) { $failed += "napi" }
            else { Write-Host "[napi] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[napi] SKIP - clang-format not found" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# TS modules (prettier)
$tsModules = @("extension", "mcp")
foreach ($mod in $tsModules) {
    $modPath = Join-Path $Root $mod
    if (-not (Test-Path (Join-Path $modPath ".prettierrc"))) { continue }

    Write-Host "[$mod] prettier..." -ForegroundColor Yellow
    Push-Location $modPath
    if ($checkOnly) {
        npx prettier --check "src/**/*.ts"
    } else {
        npx prettier --write "src/**/*.ts"
    }
    if ($LASTEXITCODE -ne 0) { $failed += $mod }
    else { Write-Host "[$mod] OK" -ForegroundColor Green }
    Pop-Location
    Write-Host ""
}

if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "All modules passed." -ForegroundColor Green
