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
$cppModules = @(
    @{ Name = "runtime"; Src = "runtime/src"; Includes = @("-I$Root/runtime/src") }
    @{ Name = "napi"; Src = "napi/src"; Includes = @("-I$Root/napi/src", "-I$Root/runtime/src", "-I$Root/napi/node_modules/node-addon-api") }
)

$clangTidy = Get-Command clang-tidy -ErrorAction SilentlyContinue
foreach ($mod in $cppModules) {
    if (-not (Test-Path (Join-Path $Root "$($mod.Name)/.clang-tidy"))) { continue }
    $srcPath = Join-Path $Root $mod.Src

    Write-Host "[$($mod.Name)] clang-tidy..." -ForegroundColor Yellow
    if ($clangTidy) {
        $sources = Get-ChildItem -Path $srcPath -Filter "*.cpp" -Recurse | Select-Object -ExpandProperty FullName
        if ($sources) {
            & clang-tidy $sources -- -std=c++17 $mod.Includes
            if ($LASTEXITCODE -ne 0) { $failed += $mod.Name }
            else { Write-Host "[$($mod.Name)] OK" -ForegroundColor Green }
        }
    } else {
        Write-Host "[$($mod.Name)] SKIP - clang-tidy not found" -ForegroundColor DarkYellow
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
