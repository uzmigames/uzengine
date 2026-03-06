# build.ps1 -- UzEngine build script (Ninja + CMake)
#
# Usage:
#   .\build.ps1                    # Debug build (default)
#   .\build.ps1 -Config Release    # Release build
#   .\build.ps1 -Config Release -Clean   # Clean then build
#   .\build.ps1 -Test              # Build + run tests
#   .\build.ps1 -Config Release -Test    # Release build + tests
#
# Requirements: cmake, ninja, MSVC (Visual Studio Build Tools or VS 2022)

param(
    [ValidateSet("Debug","Release","RelWithDebInfo")]
    [string]$Config = "Debug",

    [switch]$Clean,
    [switch]$Test,
    [switch]$Verbose
)

$ErrorActionPreference = "Stop"

# ---------------------------------------------------------------------------
# Paths
# ---------------------------------------------------------------------------
$Root      = $PSScriptRoot
$BuildDir  = Join-Path $Root "build"
$Vcpkg     = "C:\Users\Bolado\vcpkg\scripts\buildsystems\vcpkg.cmake"

if (-not (Test-Path $Vcpkg)) {
    $Vcpkg = "C:\vcpkg\scripts\buildsystems\vcpkg.cmake"
}
if (-not (Test-Path $Vcpkg)) {
    Write-Error "vcpkg toolchain not found. Set Vcpkg to the correct path."
}

# ---------------------------------------------------------------------------
# Locate MSVC via vswhere and inject its environment into this process
# ---------------------------------------------------------------------------
function Import-VcVars {
    param([string]$VcvarsPath, [string]$Arch = "x64", [string]$VcVarsVer = "")

    $tempBat = [IO.Path]::GetTempFileName() + ".bat"
    $verArg = if ($VcVarsVer) { " -vcvars_ver=$VcVarsVer" } else { "" }
    $batContent = "@echo off`r`ncall `"$VcvarsPath`" $Arch$verArg > nul 2>&1`r`nset`r`n"
    [IO.File]::WriteAllText($tempBat, $batContent, [Text.Encoding]::ASCII)

    $lines = & cmd.exe /c "`"$tempBat`""
    Remove-Item $tempBat -ErrorAction SilentlyContinue

    $skip = @('_', 'PROMPT', 'CMDEXTVERSION', 'ERRORLEVEL', 'COMSPEC')
    foreach ($line in $lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $n = $Matches[1]
            $v = $Matches[2]
            if ($n -notin $skip) {
                [Environment]::SetEnvironmentVariable($n, $v, 'Process')
                Set-Item -Path "Env:\$n" -Value $v -ErrorAction SilentlyContinue
            }
        }
    }
}

$VsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
if (Test-Path $VsWhere) {
    $VsPath = & $VsWhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if ($VsPath) {
        $VcvarsAll = Join-Path $VsPath "VC\Auxiliary\Build\vcvarsall.bat"
        if (Test-Path $VcvarsAll) {
            Write-Host "  Setting up MSVC environment from: $VsPath" -ForegroundColor DarkGray

            # Detect the newest installed MSVC toolset so vcpkg and CMake use the same compiler
            $msvcToolsRoot = Join-Path $VsPath "VC\Tools\MSVC"
            $latestVer = ""
            if (Test-Path $msvcToolsRoot) {
                $latestVer = Get-ChildItem $msvcToolsRoot -Directory |
                    Sort-Object Name -Descending |
                    Select-Object -First 1 -ExpandProperty Name
            }

            Import-VcVars -VcvarsPath $VcvarsAll -Arch "x64" -VcVarsVer $latestVer
            if ($latestVer) {
                Write-Host "  MSVC toolset: $latestVer" -ForegroundColor DarkGray
            }
            Write-Host "  MSVC environment ready." -ForegroundColor DarkGray
        } else {
            Write-Warning "vcvarsall.bat not found -- compiler may not be detected by Ninja."
        }
    } else {
        Write-Warning "No Visual Studio installation with VC++ tools found."
    }
} else {
    Write-Warning "vswhere.exe not found -- skipping MSVC environment setup."
}

# ---------------------------------------------------------------------------
# Clean
# ---------------------------------------------------------------------------
if ($Clean -and (Test-Path $BuildDir)) {
    Write-Host "Cleaning build directory..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $BuildDir
}

# ---------------------------------------------------------------------------
# Configure (only if CMakeCache.txt missing or after clean)
# ---------------------------------------------------------------------------
$CacheFile = Join-Path $BuildDir "CMakeCache.txt"
if (-not (Test-Path $CacheFile)) {
    Write-Host "Configuring with Ninja Multi-Config..." -ForegroundColor Cyan

    $ConfigArgs = @(
        "-B", $BuildDir,
        "-G", "Ninja Multi-Config",
        "-DCMAKE_TOOLCHAIN_FILE=$Vcpkg",
        "-DVCPKG_TARGET_TRIPLET=x64-windows",
        "-DCMAKE_EXPORT_COMPILE_COMMANDS=ON"
    )

    & cmake $Root @ConfigArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "CMake configure failed." }
    Write-Host "Configure OK." -ForegroundColor Green
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
Write-Host "Building [$Config]..." -ForegroundColor Cyan

$BuildArgs = @(
    "--build", $BuildDir,
    "--config", $Config,
    "--parallel"
)
if ($Verbose) { $BuildArgs += "--verbose" }

& cmake @BuildArgs
if ($LASTEXITCODE -ne 0) { Write-Error "Build failed." }
Write-Host "Build OK [$Config]." -ForegroundColor Green

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if ($Test) {
    Write-Host "Running tests [$Config]..." -ForegroundColor Cyan

    $CTestArgs = @(
        "--test-dir", $BuildDir,
        "--build-config", $Config,
        "--output-on-failure",
        "--parallel", [Environment]::ProcessorCount
    )

    & ctest @CTestArgs
    if ($LASTEXITCODE -ne 0) { Write-Error "Tests FAILED." }
    Write-Host "All tests PASSED." -ForegroundColor Green
}

Write-Host ""
Write-Host "Done. Output: $BuildDir\$Config\" -ForegroundColor Green
