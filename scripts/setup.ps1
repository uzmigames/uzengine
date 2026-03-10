# setup.ps1 — UzEngine dependency setup script
# Downloads and configures all thirdparty dependencies for the runtime.
# Run from project root: .\scripts\setup.ps1

$ErrorActionPreference = "Stop"
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RootDir = Split-Path -Parent $ScriptDir
$ThirdPartyDir = Join-Path $RootDir "runtime\thirdparty"
$TempDir = Join-Path $RootDir ".setup_temp"

if (-not (Test-Path $ThirdPartyDir)) { New-Item -ItemType Directory -Path $ThirdPartyDir | Out-Null }
if (-not (Test-Path $TempDir)) { New-Item -ItemType Directory -Path $TempDir | Out-Null }

Write-Host "=== UzEngine Setup ===" -ForegroundColor Cyan
Write-Host "Thirdparty: $ThirdPartyDir"
Write-Host ""

# ─── Helpers ────────────────────────────────────────────────────────────────

function Download-File {
    param([string]$Url, [string]$OutFile)
    Write-Host "  Downloading $Url..." -ForegroundColor Gray
    curl.exe -sL -o $OutFile $Url
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to download $Url" }
}

function Clone-Repo {
    param([string]$Name, [string]$Url, [string]$Branch, [string[]]$KeepDirs, [string[]]$RemoveDirs)
    $path = Join-Path $ThirdPartyDir $Name
    if ((Test-Path $path) -and (Get-ChildItem $path | Measure-Object).Count -gt 0) {
        Write-Host "  [$Name] already exists, skipping." -ForegroundColor Green
        return
    }
    if (Test-Path $path) { Remove-Item -Recurse -Force $path }
    Write-Host "  [$Name] cloning $Branch..." -ForegroundColor Yellow
    $cloneOutput = cmd /c "git clone --depth 1 -b $Branch $Url $path 2>&1"
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to clone $Name" }

    # Remove .git
    $gitDir = Join-Path $path ".git"
    if (Test-Path $gitDir) { Remove-Item -Recurse -Force $gitDir }

    # Remove specific dirs if specified
    if ($RemoveDirs) {
        foreach ($dir in $RemoveDirs) {
            $p = Join-Path $path $dir
            if (Test-Path $p) { Remove-Item -Recurse -Force $p }
        }
    }
}

function Download-Extract-Zip {
    param([string]$Name, [string]$Url, [string]$SubPath)
    $path = Join-Path $ThirdPartyDir $Name
    if (Test-Path $path) {
        Write-Host "  [$Name] already exists, skipping." -ForegroundColor Green
        return
    }
    $zip = Join-Path $TempDir "$Name.zip"
    Write-Host "  [$Name] downloading..." -ForegroundColor Yellow
    Download-File -Url $Url -OutFile $zip

    if ($SubPath) {
        $tempExtract = Join-Path $TempDir "${Name}_temp"
        Expand-Archive -Path $zip -DestinationPath $tempExtract -Force
        New-Item -ItemType Directory -Path $path -Force | Out-Null
        $src = Join-Path $tempExtract $SubPath
        Copy-Item -Path "$src\*" -Destination $path -Recurse -Force
        Remove-Item -Recurse -Force $tempExtract
    } else {
        Expand-Archive -Path $zip -DestinationPath $path -Force
    }
    Remove-Item $zip
}

function Download-Header {
    param([string]$Name, [string]$Url, [string]$FileName)
    $dir = Join-Path $ThirdPartyDir $Name
    if (Test-Path $dir) {
        Write-Host "  [$Name] already exists, skipping." -ForegroundColor Green
        return
    }
    Write-Host "  [$Name] downloading header..." -ForegroundColor Yellow
    New-Item -ItemType Directory -Path $dir -Force | Out-Null
    Download-File -Url $Url -OutFile (Join-Path $dir $FileName)
}

# ─── Prerequisites ──────────────────────────────────────────────────────────

Write-Host "[1/6] Checking prerequisites..." -ForegroundColor Cyan

# Refresh PATH from registry (picks up winget/installer changes without restarting terminal)
function Refresh-Path {
    $machinePath = [Environment]::GetEnvironmentVariable("Path", "Machine")
    $userPath = [Environment]::GetEnvironmentVariable("Path", "User")
    $env:Path = "$machinePath;$userPath"
}

# Zig
if (-not (Get-Command zig -ErrorAction SilentlyContinue)) {
    Refresh-Path
}
if (-not (Get-Command zig -ErrorAction SilentlyContinue)) {
    Write-Host "  Zig not found. Installing via winget..." -ForegroundColor Yellow
    winget install zig.zig --accept-package-agreements --accept-source-agreements 2>&1 | Out-Null
    Refresh-Path
    if (-not (Get-Command zig -ErrorAction SilentlyContinue)) {
        Write-Error "Zig installed but not in PATH. Restart terminal and re-run setup."
    }
}
$zigVer = zig version
Write-Host "  Zig: $zigVer" -ForegroundColor Green

# CMake
if (-not (Get-Command cmake -ErrorAction SilentlyContinue)) {
    Write-Host "  CMake not found. Installing via winget..." -ForegroundColor Yellow
    winget install Kitware.CMake --accept-package-agreements --accept-source-agreements
} else {
    Write-Host "  CMake: found" -ForegroundColor Green
}

# Node.js
if (-not (Get-Command node -ErrorAction SilentlyContinue)) {
    Write-Host "  Node.js not found. Installing via winget..." -ForegroundColor Yellow
    winget install OpenJS.NodeJS.LTS --accept-package-agreements --accept-source-agreements
} else {
    $nodeVer = node --version
    Write-Host "  Node.js: $nodeVer" -ForegroundColor Green
}

# ─── vcpkg ──────────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[2/6] Setting up vcpkg..." -ForegroundColor Cyan

$VcpkgDir = Join-Path $ThirdPartyDir "vcpkg"
$VcpkgExe = Join-Path $VcpkgDir "vcpkg.exe"

if (-not (Test-Path $VcpkgExe)) {
    Write-Host "  Cloning vcpkg..." -ForegroundColor Yellow
    git clone --depth 1 https://github.com/Microsoft/vcpkg.git $VcpkgDir
    & (Join-Path $VcpkgDir "bootstrap-vcpkg.bat") -disableMetrics
    if (-not (Test-Path $VcpkgExe)) { Write-Error "vcpkg bootstrap failed!" }
} else {
    Write-Host "  vcpkg already installed." -ForegroundColor Green
}

$VcpkgPackages = @("zlib", "zstd")
foreach ($pkg in $VcpkgPackages) {
    Write-Host "  vcpkg: installing $pkg..." -ForegroundColor Yellow
    & $VcpkgExe install "${pkg}:x64-windows" --vcpkg-root $VcpkgDir 2>&1 | Out-Null
}

# ─── Git Clones ─────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[3/6] Cloning thirdparty repos..." -ForegroundColor Cyan

# DirectX Headers
Clone-Repo -Name "directx_headers" `
    -Url "https://github.com/microsoft/DirectX-Headers.git" `
    -Branch "v1.614.1" `
    -RemoveDirs @("test", "googletest")

# D3D12 Memory Allocator
Clone-Repo -Name "d3d12ma" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git" `
    -Branch "v2.0.1" `
    -RemoveDirs @("docs", "tools")

# spdlog (logging)
Clone-Repo -Name "spdlog" `
    -Url "https://github.com/gabime/spdlog.git" `
    -Branch "v1.15.3" `
    -RemoveDirs @("bench", "example", "logos", "scripts", "tests")

# Tracy (profiling)
Clone-Repo -Name "tracy" `
    -Url "https://github.com/wolfpld/tracy.git" `
    -Branch "v0.11.1" `
    -RemoveDirs @("capture", "csvexport", "doc", "dtl", "examples", "extra", "icon", "imgui",
                  "import", "manual", "nfd", "profiler", "python", "server", "test", "update",
                  "zstd", "getopt")

# mimalloc (global allocator)
Clone-Repo -Name "mimalloc" `
    -Url "https://github.com/microsoft/mimalloc.git" `
    -Branch "v2.1.7" `
    -RemoveDirs @("docs", "test", "bench", "ide", "out")

# jemalloc (alternative allocator)
Clone-Repo -Name "jemalloc" `
    -Url "https://github.com/jemalloc/jemalloc.git" `
    -Branch "5.3.0" `
    -RemoveDirs @("doc", "test", "scripts", "msvc")

# dlmalloc (lightweight allocator)
Download-Header -Name "dlmalloc" `
    -Url "https://gee.cs.oswego.edu/pub/misc/malloc.c" `
    -FileName "dlmalloc.c"

# atomic_queue (lock-free queue)
Clone-Repo -Name "atomic_queue" `
    -Url "https://github.com/max0x7ba/atomic_queue.git" `
    -Branch "master" `
    -RemoveDirs @("benchmarks", "doc", "html", "scripts", "src")

# DirectXMath
Clone-Repo -Name "directxmath" `
    -Url "https://github.com/microsoft/DirectXMath.git" `
    -Branch "oct2024" `
    -RemoveDirs @("SHMath", "XDSP", "Spherical", "Extensions", "MatrixStack", "Tests", "Utilities")

# glm (math, header-only)
Clone-Repo -Name "glm" `
    -Url "https://github.com/g-truc/glm.git" `
    -Branch "1.0.1" `
    -RemoveDirs @("doc", "test", "util")

# nlohmann/json (config parsing)
Download-Header -Name "nlohmann" `
    -Url "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" `
    -FileName "json.hpp"

# GoogleTest (testing framework)
Clone-Repo -Name "googletest" `
    -Url "https://github.com/google/googletest.git" `
    -Branch "v1.15.2" `
    -RemoveDirs @("ci", "docs", "googlemock", "fake_fuchsia_sdk")

# Crashpad (crash reporting) — TODO: find working minimal crashpad repo
# Clone-Repo -Name "crashpad" `
#     -Url "https://github.com/nickvdp/mini-crashpad.git" `
#     -Branch "main" `
#     -RemoveDirs @("docs", "test")
Write-Host "  [crashpad] skipped (manual setup required)" -ForegroundColor DarkYellow

# ─── Downloads ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[4/6] Downloading prebuilt packages..." -ForegroundColor Cyan

# DXC (DirectX Shader Compiler)
Download-Extract-Zip -Name "dxc" `
    -Url "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip"

# Agility SDK (DX12 runtime)
Download-Extract-Zip -Name "agility" `
    -Url "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.614.1" `
    -SubPath "build\native"

# ─── Global Cleanup ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[5/6] Cleaning up..." -ForegroundColor Cyan

$JunkDirs = @(".github", ".vscode", ".ci", ".circleci")
$JunkFiles = @(".gitignore", ".gitattributes", ".gitmodules",
               ".clang-format", ".clang-tidy", ".editorconfig",
               ".travis.yml", ".pre-commit-config.yaml",
               "docker-compose.yml", "mkdocs.yml",
               "codecov.yml", "CONTRIBUTING.md",
               "CODE_OF_CONDUCT.md", "SECURITY.md",
               "Makefile", "Doxyfile", "appveyor.yml")

Get-ChildItem -Path $ThirdPartyDir -Directory | ForEach-Object {
    $repoPath = $_.FullName
    if ($_.Name -eq "vcpkg") { return } # skip vcpkg

    # Remove .git leftovers
    $gitDir = Join-Path $repoPath ".git"
    if (Test-Path $gitDir) { Remove-Item -Recurse -Force $gitDir }

    foreach ($dir in $JunkDirs) {
        $p = Join-Path $repoPath $dir
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    foreach ($file in $JunkFiles) {
        $p = Join-Path $repoPath $file
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# Remove temp dir
if (Test-Path $TempDir) { Remove-Item -Recurse -Force $TempDir }

# ─── npm install ───────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[6/6] Installing Node dependencies..." -ForegroundColor Cyan

$npmModules = @("napi", "mcp", "extension")
foreach ($mod in $npmModules) {
    $modPath = Join-Path $RootDir $mod
    if (Test-Path (Join-Path $modPath "package.json")) {
        Write-Host "  [$mod] npm install..." -ForegroundColor Yellow
        Push-Location $modPath
        npm install 2>&1 | Out-Null
        if ($LASTEXITCODE -ne 0) { Pop-Location; Write-Error "npm install failed in $mod" }
        Pop-Location
        Write-Host "  [$mod] OK" -ForegroundColor Green
    }
}

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Green
Write-Host "Thirdparty libraries installed at: $ThirdPartyDir"
Write-Host ""
Write-Host "Next: scripts\build.ps1"
