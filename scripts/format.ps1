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
$cppModules = @("runtime", "napi")
$clangFormat = Get-Command clang-format -ErrorAction SilentlyContinue

foreach ($mod in $cppModules) {
    $modPath = Join-Path $Root $mod
    if (-not (Test-Path (Join-Path $modPath ".clang-format"))) { continue }
    $srcPath = Join-Path $modPath "src"

    Write-Host "[$mod] clang-format..." -ForegroundColor Yellow
    if ($clangFormat) {
        $sources = Get-ChildItem -Path $srcPath -Include "*.cpp","*.h" -Recurse | Select-Object -ExpandProperty FullName
        if ($sources) {
            if ($checkOnly) {
                & clang-format --dry-run --Werror $sources
            } else {
                & clang-format -i $sources
            }
            if ($LASTEXITCODE -ne 0) { $failed += $mod }
            else { Write-Host "[$mod] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[$mod] SKIP - clang-format not found" -ForegroundColor DarkYellow
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
