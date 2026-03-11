$ErrorActionPreference = "Stop"
$Root = Split-Path $PSScriptRoot

# Refresh PATH from registry (picks up winget/installer changes without restarting terminal)
$machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
$userPath = [Environment]::GetEnvironmentVariable("Path", "User")
$env:Path = "$machinePath;$userPath"

# Parse --compiler flag (zig, msvc, clang). Default: zig
$compiler = "zig"
foreach ($arg in $args) {
    if ($arg -match "^--compiler=(.+)$") { $compiler = $Matches[1] }
}

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " UzEngine Full Build Pipeline" -ForegroundColor Cyan
Write-Host " Compiler: $compiler" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

# ---- Pre-flight: detect locked files ----
Write-Host "[0/4] Checking for blocking processes..." -ForegroundColor Yellow

function Test-FileLocked([string]$Path) {
    if (-not (Test-Path $Path)) { return $false }
    try {
        $stream = [System.IO.File]::Open($Path, 'Open', 'ReadWrite', 'None')
        $stream.Close()
        return $false
    } catch {
        return $true
    }
}

$filesToCheck = @(
    @{ Path = "$Root/extension/native/uzengine.node"; Desc = "uzengine.node (extension)" }
    @{ Path = "$Root/napi/build/Release/uzengine.node"; Desc = "uzengine.node (napi build)" }
    @{ Path = "$Root/runtime/build/lib/uzengine.lib"; Desc = "uzengine.lib" }
)

$blocked = $false
foreach ($file in $filesToCheck) {
    if (Test-FileLocked $file.Path) {
        Write-Host "      LOCKED: $($file.Desc)" -ForegroundColor Red
        $name = Split-Path $file.Path -Leaf
        $procs = Get-Process | Where-Object {
            try { $_.Modules | Where-Object { $_.FileName -like "*$name*" } } catch {}
        }
        foreach ($p in $procs) {
            Write-Host "        -> held by $($p.ProcessName) (PID $($p.Id))" -ForegroundColor Red
        }
        $blocked = $true
    }
}

if ($blocked) {
    Write-Host ""
    Write-Host "ERROR: One or more build artifacts are locked." -ForegroundColor Red
    Write-Host "       Close VSCode Extension Host or reload the window, then retry." -ForegroundColor Red
    Write-Host "       Tip: In VSCode run 'Developer: Reload Window' first." -ForegroundColor Gray
    Write-Host ""
    exit 1
}

Write-Host "      OK - no locks detected" -ForegroundColor Green
Write-Host ""

# ---- Build Pipeline ----

# 1. Runtime
Write-Host "[1/4] Building runtime ($compiler)..." -ForegroundColor Yellow
Push-Location "$Root/runtime"

switch ($compiler) {
    "zig" {
        zig build -Doptimize=ReleaseFast
    }
    "msvc" {
        cmake --preset msvc-release 2>&1 | Out-Null
        cmake --build build/msvc-release --config Release
    }
    "clang" {
        cmake --preset clang-release 2>&1 | Out-Null
        cmake --build build/clang-release --config Release
    }
    default {
        Pop-Location
        throw "Unknown compiler: $compiler. Use zig, msvc, or clang."
    }
}

if ($LASTEXITCODE -ne 0) { Pop-Location; throw "runtime build failed" }
Pop-Location
Write-Host "      OK" -ForegroundColor Green
Write-Host ""

# 2. N-API bridge (cmake-js)
Write-Host "[2/4] Building napi (cmake-js + Electron)..." -ForegroundColor Yellow
Push-Location "$Root/napi"
npx cmake-js compile --runtime=electron --runtime-version=39.6.0 --CDCMAKE_BUILD_TYPE=Release
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "napi build failed" }
Pop-Location
Write-Host "      OK" -ForegroundColor Green
Write-Host ""

# Copy .node to extension
Write-Host "      Copying uzengine.node to extension/native/..." -ForegroundColor Gray
New-Item -ItemType Directory -Path "$Root/extension/native" -Force | Out-Null
Copy-Item "$Root/napi/build/Release/uzengine.node" "$Root/extension/native/uzengine.node" -Force
Write-Host "      OK" -ForegroundColor Green
Write-Host ""

# 3. MCP server (TypeScript)
Write-Host "[3/4] Building mcp (tsc)..." -ForegroundColor Yellow
Push-Location "$Root/mcp"
npx tsc -p .
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "mcp build failed" }
Pop-Location
Write-Host "      OK" -ForegroundColor Green
Write-Host ""

# 4. Extension (TypeScript)
Write-Host "[4/4] Building extension (tsc)..." -ForegroundColor Yellow
Push-Location "$Root/extension"
npx tsc -p .
if ($LASTEXITCODE -ne 0) { Pop-Location; throw "extension build failed" }
Pop-Location
Write-Host "      OK" -ForegroundColor Green
Write-Host ""

Write-Host "============================================" -ForegroundColor Cyan
Write-Host " Build complete! (compiler: $compiler)" -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
