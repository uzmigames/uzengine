$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot

# Refresh PATH from registry
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machinePath;$userPath"

$withCoverage = $args -contains "--coverage"

# Parse --compiler flag (zig, msvc, clang). Default: zig
$compiler = "zig"
foreach ($arg in $args) {
    if ($arg -match "^--compiler=(.+)$") { $compiler = $Matches[1] }
}

$action = if ($withCoverage) { "Testing (with coverage)" } else { "Testing" }

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " UzEngine $action" -ForegroundColor Cyan
Write-Host " Compiler: $compiler" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

$failed = @()

# Runtime
Write-Host "[runtime] Testing ($compiler)..." -ForegroundColor Yellow
Push-Location (Join-Path $Root "runtime")

switch ($compiler) {
    "zig" {
        zig build test
    }
    "msvc" {
        $testExe = "build/msvc-debug/Debug/test_runtime.exe"
        if (-not (Test-Path $testExe)) {
            Write-Host "  Building first..." -ForegroundColor Gray
            cmake --preset msvc-debug 2>&1 | Out-Null
            cmake --build build/msvc-debug
        }
        & $testExe
    }
    "clang" {
        $testExe = "build/clang-debug/test_runtime.exe"
        if (-not (Test-Path $testExe)) {
            Write-Host "  Building first..." -ForegroundColor Gray
            cmake --preset clang-debug 2>&1 | Out-Null
            cmake --build build/clang-debug
        }
        & $testExe
    }
}

if ($LASTEXITCODE -ne 0) { $failed += "runtime" }
else { Write-Host "[runtime] OK" -ForegroundColor Green }
Pop-Location
Write-Host ""

# TS modules (vitest)
$tsModules = @("extension", "mcp")
foreach ($mod in $tsModules) {
    $modPath = Join-Path $Root $mod
    $hasConfig = (Test-Path (Join-Path $modPath "vitest.config.ts")) -or (Test-Path (Join-Path $modPath "vitest.config.mts"))
    if (-not $hasConfig) { continue }

    Write-Host "[$mod] vitest..." -ForegroundColor Yellow
    Push-Location $modPath
    if ($withCoverage) {
        npx vitest run --coverage
    } else {
        npx vitest run
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
