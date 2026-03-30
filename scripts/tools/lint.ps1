$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot

# Refresh PATH from registry
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machinePath;$userPath"

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " UzEngine Lint" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$failed = @()

# C++ modules (clang-tidy)
$clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue

# Runtime — lint src/, core/, platform/, demos/, tools/
$runtimePath = Join-Path $Root "runtime"
if (Test-Path (Join-Path $runtimePath ".clang-tidy")) {
    Write-Host "[runtime] clang-tidy..." -ForegroundColor Yellow
    if ($clangTidy) {
        $runtimeDirs = @("src", "core", "platform", "demos", "tools")
        $sources = @()
        foreach ($dir in $runtimeDirs) {
            $dirPath = Join-Path $runtimePath $dir
            if (Test-Path $dirPath) {
                $sources += Get-ChildItem -Path $dirPath -Filter "*.cpp" -Recurse | Select-Object -ExpandProperty FullName
            }
        }
        if ($sources) {
            $includes = @("-I$runtimePath/src")
            & clang-tidy $sources -- -std=c++17 $includes
            if ($LASTEXITCODE -ne 0) { $failed += "runtime" }
            else { Write-Host "[runtime] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[runtime] SKIP - clang-tidy not found" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# N-API module
$napiPath = Join-Path $Root "napi"
if (Test-Path (Join-Path $napiPath ".clang-tidy")) {
    $napiSrc = Join-Path $napiPath "src"
    Write-Host "[napi] clang-tidy..." -ForegroundColor Yellow
    if ($clangTidy) {
        $sources = Get-ChildItem -Path $napiSrc -Filter "*.cpp" -Recurse -ErrorAction SilentlyContinue | Select-Object -ExpandProperty FullName
        if ($sources) {
            $includes = @("-I$napiPath/src", "-I$runtimePath/src", "-I$napiPath/node_modules/node-addon-api")
            & clang-tidy $sources -- -std=c++17 $includes
            if ($LASTEXITCODE -ne 0) { $failed += "napi" }
            else { Write-Host "[napi] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[napi] SKIP - clang-tidy not found" -ForegroundColor DarkYellow
    }
    Write-Host ""
}

# TS modules (eslint)
$tsModules = @("extension", "mcp")
foreach ($mod in $tsModules) {
    $modPath = Join-Path $Root $mod
    if (-not (Test-Path (Join-Path $modPath "eslint.config.mjs"))) { continue }

    Write-Host "[$mod] eslint..." -ForegroundColor Yellow
    Push-Location $modPath
    npx eslint src/
    if ($LASTEXITCODE -ne 0) { $failed += $mod }
    else { Write-Host "[$mod] OK" -ForegroundColor Green }
    Pop-Location
    Write-Host ""
}

if ($failed.Count -gt 0) {
    Write-Host "FAILED: $($failed -join ', ')" -ForegroundColor Red
    exit 1
}
Write-Host "All modules passed lint." -ForegroundColor Green
