# =============================================================================
# start-server.ps1 -- Build (if needed) and launch UzEngine streaming server
#
# Usage:
#   .\scripts\start-server.ps1            # Release build (default)
#   .\scripts\start-server.ps1 -Config Debug
# =============================================================================

param(
    [ValidateSet('Debug', 'Release')]
    [string]$Config = 'Release'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$Root     = Split-Path $PSScriptRoot -Parent
$BuildDir = Join-Path $Root 'build'
$Exe      = Join-Path $BuildDir "$Config\render.exe"

Write-Host "[UzEngine] Config: $Config"

if (-not (Test-Path $Exe)) {
    Write-Host "[UzEngine] Executable not found. Building..."

    if (-not (Test-Path $BuildDir)) {
        Write-Host "[UzEngine] Configuring CMake..."
        cmake -B $BuildDir -G "Visual Studio 17 2022" -A x64 -S $Root
        if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    }

    Write-Host "[UzEngine] Building $Config..."
    cmake --build $BuildDir --config $Config
    if ($LASTEXITCODE -ne 0) { throw "Build failed" }
}

if (-not (Test-Path $Exe)) {
    throw "Executable not found after build: $Exe"
}

Write-Host "[UzEngine] Starting: $Exe"
Write-Host "[UzEngine] WebSocket server will listen on ws://localhost:9002"
Write-Host "[UzEngine] Open VSCode and run 'UzEngine: Open Stream Viewer'"
Write-Host ""

& $Exe
