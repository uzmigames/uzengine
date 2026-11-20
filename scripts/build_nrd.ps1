# build_nrd.ps1 — Builds the NVIDIA NRD SDK (runtime/thirdparty/nrd, cloned by
# setup.ps1 at a pinned tag) into NRD.dll + NRD.lib with its DXIL shaders embedded.
#
# NRD publishes no binaries. Like the Brixelizer wrapper, the DLL is built with
# MSVC/CMake and loaded by the engine at runtime (LoadLibrary, extern "C" exports),
# so the Zig/mingw build never links an MSVC static library.
#   - DXIL only (DX12 engine): NRD_EMBEDS_DXBC_SHADERS / SPIRV are off.
#   - Shaders compile with the engine's pinned DXC (runtime/thirdparty/dxc), not a
#     DXC that ShaderMake would download on its own.
#   - NRD_NORMAL_ENCODING=4 (RGBA16_SNORM, NRD also accepts RGBA16_SFLOAT inputs for
#     it): the engine packs IN_NORMAL_ROUGHNESS into an RGBA16Float RHI texture.
#     NRD_ROUGHNESS_ENCODING=1 keeps linear roughness. NrdSdkDenoiser::Init checks
#     both against nrd::GetLibraryDesc() and refuses a DLL built differently.
#   - Static MSVC CRT: NRD.dll depends on kernel32 only (no VC redistributable).
#   - SHADERMAKE_TOOL=OFF builds ShaderMake inside the same solution; the default
#     nested ExternalProject build dies silently under a parent MSBuild.
# NRD's own CMake fetches two pinned archives from GitHub while configuring:
# ShaderMake (commit) and MathLib (tag v11).
#
# Usage: .\scripts\build_nrd.ps1 [-Force]
param([switch]$Force)
$ErrorActionPreference = "Stop"

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$NrdDir = Join-Path $RootDir "runtime\thirdparty\nrd"
$Dxc = Join-Path $RootDir "runtime\thirdparty\dxc\bin\x64\dxc.exe"
$BuildDir = Join-Path $NrdDir "_Build"
$Dll = Join-Path $NrdDir "_Bin\Release\NRD.dll"

if (-not (Test-Path (Join-Path $NrdDir "CMakeLists.txt"))) {
    throw "[nrd] source not found at $NrdDir; run scripts\setup.ps1 first."
}
if (-not (Test-Path $Dxc)) {
    throw "[nrd] pinned DXC not found at $Dxc; run scripts\setup.ps1 first."
}
if ((Test-Path $Dll) -and -not $Force) {
    Write-Host "  [nrd] already built: $Dll (use -Force to rebuild)" -ForegroundColor Green
    return
}
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    throw "[nrd] CMake not found. Install Kitware.CMake (setup.ps1 does this via winget)."
}

function Invoke-CMake {
    # CMake's message() lines go to stderr; under the script-wide "Stop" Windows
    # PowerShell 5.1 would turn them into terminating NativeCommandErrors (same
    # reason setup.ps1 has Invoke-Git). Merge the streams and judge the exit code.
    param([string[]]$Arguments)
    $ErrorActionPreference = "Continue"
    & cmake @Arguments 2>&1 | ForEach-Object { Write-Host "$_" }
    return $LASTEXITCODE
}

Write-Host "  [nrd] configuring (Visual Studio 17 2022, x64, DXIL via $Dxc)..." -ForegroundColor Yellow
$configure = Invoke-CMake @("-S", $NrdDir, "-B", $BuildDir, "-G", "Visual Studio 17 2022", "-A", "x64",
    "-DNRD_EMBEDS_DXIL_SHADERS=ON", "-DNRD_EMBEDS_DXBC_SHADERS=OFF", "-DNRD_EMBEDS_SPIRV_SHADERS=OFF",
    "-DNRD_NORMAL_ENCODING=4", "-DNRD_ROUGHNESS_ENCODING=1",
    "-DSHADERMAKE_TOOL=OFF", "-DSHADERMAKE_FIND_DXC=OFF", "-DSHADERMAKE_FIND_DXC_VK=OFF", "-DSHADERMAKE_FIND_FXC=OFF",
    "-DSHADERMAKE_DXC_PATH=$Dxc", "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded")
if ($configure -ne 0) { throw "[nrd] CMake configure failed (exit $configure)" }

Write-Host "  [nrd] building NRD.dll (Release)..." -ForegroundColor Yellow
$build = Invoke-CMake @("--build", $BuildDir, "--config", "Release", "--target", "NRD", "--", "/m", "/nologo", "/v:minimal")
if ($build -ne 0) { throw "[nrd] build failed (exit $build)" }
if (-not (Test-Path $Dll)) { throw "[nrd] build finished but $Dll is missing" }
Write-Host "  [nrd] built $Dll" -ForegroundColor Green
