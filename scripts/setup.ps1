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

function Apply-Patches {
    param([string]$Name)
    $patchDir = Join-Path $ThirdPartyDir "$Name\patches"
    if (-not (Test-Path $patchDir)) { return }
    $patches = Get-ChildItem $patchDir -Filter "*.patch" | Sort-Object Name
    if ($patches.Count -eq 0) { return }
    foreach ($patch in $patches) {
        Write-Host "  [$Name] applying $($patch.Name)..." -ForegroundColor Magenta
        # Apply from repo root — git apply works on the working tree even for non-git subdirs
        Push-Location $RootDir
        git apply --directory="runtime/thirdparty/$Name" $patch.FullName 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [$Name] WARN: patch may already be applied: $($patch.Name)" -ForegroundColor DarkYellow
        }
        Pop-Location
    }
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

Write-Host "[1/8] Checking prerequisites..." -ForegroundColor Cyan

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
Write-Host "[2/8] Setting up vcpkg..." -ForegroundColor Cyan

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
Write-Host "[3/8] Cloning thirdparty repos..." -ForegroundColor Cyan

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

# SAL stub for non-MSVC platforms (DirectXMath requires sal.h)
$salPath = Join-Path $ThirdPartyDir "directxmath\Inc\sal.h"
if (-not (Test-Path $salPath)) {
    Write-Host "  [sal.h] downloading cross-platform stub..." -ForegroundColor Yellow
    Download-File `
        -Url "https://raw.githubusercontent.com/dotnet/runtime/main/src/coreclr/pal/inc/rt/sal.h" `
        -OutFile $salPath
} else {
    Write-Host "  [sal.h] already exists, skipping." -ForegroundColor Green
}

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
    -RemoveDirs @("ci", "docs", "googlemock", "fake_fuchsia_sdk", "test", "samples")

# SPIRV-Reflect (shader reflection)
Clone-Repo -Name "spirv-reflect" `
    -Url "https://github.com/KhronosGroup/SPIRV-Reflect.git" `
    -Branch "vulkan-sdk-1.3.268.0" `
    -RemoveDirs @("examples", "tests", "docs", "scripts", "cmake")

# xxhash (fast hashing)
Clone-Repo -Name "xxhash" `
    -Url "https://github.com/Cyan4973/xxHash.git" `
    -Branch "v0.8.2" `
    -RemoveDirs @("tests", "doc", "cmake_unofficial", "contrib")

# SSE Math Fun (SIMD math functions)
Clone-Repo -Name "sse_mathfun" `
    -Url "https://github.com/RJVB/sse_mathfun.git" `
    -Branch "master" `
    -RemoveDirs @()

# MikkTSpace (tangent space generation)
Clone-Repo -Name "mikktspace" `
    -Url "https://github.com/mmikk/MikkTSpace.git" `
    -Branch "master" `
    -RemoveDirs @()

# stb (image loading/writing/resizing — header-only, keep only needed headers)
Clone-Repo -Name "stb" `
    -Url "https://github.com/nothings/stb.git" `
    -Branch "master" `
    -RemoveDirs @("docs", "data", "tests", "tools", "deprecated")

# Keep only the headers we use
$stbKeep = @("stb_image.h", "stb_image_write.h", "stb_image_resize2.h", "stb_truetype.h", "LICENSE")
$stbPath = Join-Path $ThirdPartyDir "stb"
if (Test-Path $stbPath) {
    Get-ChildItem $stbPath -File | Where-Object { $stbKeep -notcontains $_.Name } | Remove-Item -Force
    Get-ChildItem $stbPath -Directory | Remove-Item -Recurse -Force
}

# libjpeg-turbo (fast JPEG decode with SIMD)
Clone-Repo -Name "libjpeg-turbo" `
    -Url "https://github.com/libjpeg-turbo/libjpeg-turbo.git" `
    -Branch "3.0.3" `
    -RemoveDirs @("doc", "java", "release", "win", "fuzz", "cmakescripts", "md5", "testimages", "sharedlib")

# Remove CLI tools, test files, build configs (keep only lib source + headers)
$jtPath = Join-Path $ThirdPartyDir "libjpeg-turbo"
if (Test-Path $jtPath) {
    $jtRemove = @("BUILDING.md","ChangeLog.md","README.md","README.ijg","change.log","coderules.txt",
                   "libjpeg.txt","structure.txt","usage.txt","wizard.txt","jconfig.txt","doxygen.config",
                   "doxygen-extra.css","cjpeg.1","djpeg.1","jpegtran.1","rdjpgcom.1","wrjpgcom.1",
                   "cjpeg.c","djpeg.c","jpegtran.c","rdjpgcom.c","wrjpgcom.c","cdjpeg.c","cdjpeg.h",
                   "rdbmp.c","rdcolmap.c","rdgif.c","rdppm.c","rdswitch.c","rdtarga.c",
                   "wrbmp.c","wrgif.c","wrppm.c","wrtarga.c",
                   "tjbench.c","tjexample.c","tjunittest.c","tjutil.c","tjutil.h","strtest.c",
                   "jcstest.c","jpeg_nbits.c","example.c","croptest.in","tjbenchtest.in","tjexampletest.in",
                   "CMakeLists.txt","jconfig.h.in","jconfigint.h.in","jversion.h.in","libjpeg.map.in",
                   "turbojpeg-jni.c","turbojpeg-mapfile","turbojpeg-mapfile.jni","turbojpeg-mp.c")
    foreach ($f in $jtRemove) {
        $p = Join-Path $jtPath $f
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# tinyexr (EXR/HDR image format — single header only)
Clone-Repo -Name "tinyexr" `
    -Url "https://github.com/syoyo/tinyexr.git" `
    -Branch "v1.0.9" `
    -RemoveDirs @("examples", "experimental", "test", "deps", "cmake", "jni")

# Keep only the header
$texrPath = Join-Path $ThirdPartyDir "tinyexr"
if (Test-Path $texrPath) {
    Get-ChildItem $texrPath -File | Where-Object { $_.Name -ne "tinyexr.h" } | Remove-Item -Force
}

# basis_universal (GPU texture compression — BC7, ASTC, ETC2)
Clone-Repo -Name "basis_universal" `
    -Url "https://github.com/BinomialLLC/basis_universal.git" `
    -Branch "master" `
    -RemoveDirs @("webgl", "test_files", "bin", "OpenCL", "build", "cmd_help", "contrib",
                  "example", "example_capi", "example_transcoding", "khronos", "python",
                  "shader_deblocking", "zstd", "encoder_lib")

# Remove build/tool files from root (keep encoder/, transcoder/, LICENSE*)
$basisPath = Join-Path $ThirdPartyDir "basis_universal"
if (Test-Path $basisPath) {
    $basisRemove = @("CMakeLists.txt","CppProperties.json","README.md","readme_wasi.md","NOTICE",
                     "all_builds.py","basisu.manifest","basisu.sln","basisu.vcxproj",
                     "basisu.vcxproj.filters","basisu_tool.cpp","basisu_tool_help.h",
                     "build_python_win.bat","encoder_lvl_vs_perf.png")
    foreach ($f in $basisRemove) {
        $p = Join-Path $basisPath $f
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# astcenc (ASTC texture encoder/decoder)
Clone-Repo -Name "astcenc" `
    -Url "https://github.com/ARM-software/astc-encoder.git" `
    -Branch "5.3.0" `
    -RemoveDirs @("Docs", "Test", "Jenkins", "Utils")

# Remove build files from root (keep Source/ + LICENSE)
$astcPath = Join-Path $ThirdPartyDir "astcenc"
if (Test-Path $astcPath) {
    @("CMakeLists.txt","README.md") | ForEach-Object {
        $p = Join-Path $astcPath $_
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# Crashpad (crash reporting) — deferred to milestone 2.x when crash reporting is implemented
Write-Host "  [crashpad] skipped (not needed until 2.x crash reporting)" -ForegroundColor DarkYellow

# ─── Downloads ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[4/8] Downloading prebuilt packages..." -ForegroundColor Cyan

# DXC (DirectX Shader Compiler)
Download-Extract-Zip -Name "dxc" `
    -Url "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.8.2407/dxc_2024_07_31.zip"

# Agility SDK (DX12 runtime)
Download-Extract-Zip -Name "agility" `
    -Url "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/1.614.1" `
    -SubPath "build\native"

# ─── Global Cleanup ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[5/8] Cleaning up..." -ForegroundColor Cyan

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

# ─── Apply Patches ────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[6/9] Applying thirdparty patches..." -ForegroundColor Cyan

Get-ChildItem -Path $ThirdPartyDir -Directory | ForEach-Object {
    if ($_.Name -eq "vcpkg") { return }
    Apply-Patches -Name $_.Name
}

Write-Host "  Patches applied." -ForegroundColor Green

# ─── npm install ───────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[7/9] Installing Node dependencies..." -ForegroundColor Cyan

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

# ─── Verify Image Libraries ────────────────────────────────────────────────

Write-Host ""
Write-Host "[8/9] Verifying image libraries..." -ForegroundColor Cyan

$ImageLibs = @(
    @{ Name = "stb";              File = "stb_image.h" },
    @{ Name = "libjpeg-turbo";    File = "jpeglib.h" },
    @{ Name = "tinyexr";          File = "tinyexr.h" },
    @{ Name = "basis_universal";  File = "transcoder\basisu_transcoder.h" },
    @{ Name = "astcenc";          File = "Source\astcenc.h" }
)

$allOk = $true
foreach ($lib in $ImageLibs) {
    $check = Join-Path $ThirdPartyDir "$($lib.Name)\$($lib.File)"
    if (Test-Path $check) {
        Write-Host "  [$($lib.Name)] OK" -ForegroundColor Green
    } else {
        Write-Host "  [$($lib.Name)] MISSING: $check" -ForegroundColor Red
        $allOk = $false
    }
}
if (-not $allOk) { Write-Warning "Some image libraries are incomplete. Check above." }

# ─── Summary ───────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[9/9] Summary" -ForegroundColor Cyan

$dirCount = (Get-ChildItem -Path $ThirdPartyDir -Directory | Where-Object { $_.Name -ne "vcpkg" } | Measure-Object).Count
Write-Host "  $dirCount thirdparty libraries installed" -ForegroundColor Green

Write-Host ""
Write-Host "=== Setup Complete ===" -ForegroundColor Green
Write-Host "Thirdparty libraries installed at: $ThirdPartyDir"
Write-Host ""
Write-Host "Next: scripts\build.ps1"
