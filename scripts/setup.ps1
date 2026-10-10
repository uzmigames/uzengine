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
    # -f makes curl exit non-zero on HTTP >= 400 instead of writing the error
    # body to disk, which would surface later as a corrupt-archive failure.
    curl.exe -fsSL -o $OutFile $Url
    if ($LASTEXITCODE -ne 0) { Write-Error "Failed to download $Url (curl exit $LASTEXITCODE)" }
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
    # $path is quoted: the repo may live under a directory containing spaces
    # (e.g. C:\Users\First Last\...), which git would otherwise parse as extra
    # positional arguments and reject with "Too many arguments".
    $cloneOutput = cmd /c "git clone --depth 1 -b ""$Branch"" ""$Url"" ""$path"" 2>&1"
    if ($LASTEXITCODE -ne 0) {
        Write-Host $cloneOutput -ForegroundColor DarkGray
        Write-Error "Failed to clone $Name"
    }

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
    # -VerifyFile: relative path of an artifact that proves the package is
    # complete. Source trees are committed to git but *.dll/*.lib/*.exe are
    # gitignored, so a fresh clone leaves these directories present but
    # missing every binary. Testing the directory alone would skip forever.
    param([string]$Name, [string]$Url, [string]$SubPath, [string]$VerifyFile)
    $path = Join-Path $ThirdPartyDir $Name
    $probe = if ($VerifyFile) { Join-Path $path $VerifyFile } else { $path }
    if (Test-Path $probe) {
        Write-Host "  [$Name] already exists, skipping." -ForegroundColor Green
        return
    }
    if ($VerifyFile -and (Test-Path $path)) {
        Write-Host "  [$Name] incomplete (missing $VerifyFile), re-extracting..." -ForegroundColor Yellow
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
    # Apply from repo root — git apply works on the working tree even for non-git subdirs
    Push-Location $RootDir
    try {
        foreach ($patch in $patches) {
            # Probe: if reverse-apply succeeds, the patch is already in the working tree
            # (typically because upstream merged the same fix). Skip silently.
            & git apply --reverse --check $patch.FullName 2>$null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "  [$Name] already applied: $($patch.Name)" -ForegroundColor DarkGray
                continue
            }
            Write-Host "  [$Name] applying $($patch.Name)..." -ForegroundColor Magenta
            & git apply --ignore-whitespace $patch.FullName 2>$null
            if ($LASTEXITCODE -ne 0) {
                Write-Host "  [$Name] WARN: patch did not apply cleanly: $($patch.Name)" -ForegroundColor Yellow
            }
        }
    } finally {
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

# Zig — setup installs 0.15.x. build.zig also builds with 0.16: it selects the
# std.fs (0.15) or std.Io.Dir (0.16) directory API at compile time.
$ZigPinnedVersion = "0.15.2"
$ZigToolsDir = Join-Path $RootDir ".tools\zig-x86_64-windows-$ZigPinnedVersion"

function Test-ZigVersionOk {
    param([string]$Cmd)
    try {
        $v = & $Cmd version 2>$null
        if ($LASTEXITCODE -eq 0 -and $v -match "^0\.15\.") { return $v }
    } catch {}
    return $null
}

# 1. Check existing zig in PATH
$systemZigVer = $null
if (Get-Command zig -ErrorAction SilentlyContinue) {
    $systemZigVer = Test-ZigVersionOk "zig"
}
if (-not $systemZigVer) { Refresh-Path; if (Get-Command zig -ErrorAction SilentlyContinue) { $systemZigVer = Test-ZigVersionOk "zig" } }

# 2. Check pinned local zig
$localZigOk = (Test-Path (Join-Path $ZigToolsDir "zig.exe"))

if ($systemZigVer) {
    Write-Host "  Zig: $systemZigVer (system)" -ForegroundColor Green
} elseif ($localZigOk) {
    $localVer = & (Join-Path $ZigToolsDir "zig.exe") version
    Write-Host "  Zig: $localVer (.tools/)" -ForegroundColor Green
} else {
    Write-Host "  Zig 0.15.x not found. Downloading $ZigPinnedVersion to .tools\..." -ForegroundColor Yellow
    $zigZip = Join-Path $TempDir "zig-$ZigPinnedVersion.zip"
    $zigUrl = "https://ziglang.org/download/$ZigPinnedVersion/zig-x86_64-windows-$ZigPinnedVersion.zip"
    Download-File -Url $zigUrl -OutFile $zigZip
    $toolsRoot = Join-Path $RootDir ".tools"
    if (-not (Test-Path $toolsRoot)) { New-Item -ItemType Directory -Path $toolsRoot | Out-Null }
    Expand-Archive -Path $zigZip -DestinationPath $toolsRoot -Force
    Remove-Item $zigZip
    if (-not (Test-Path (Join-Path $ZigToolsDir "zig.exe"))) {
        Write-Error "Zig extraction failed: $ZigToolsDir\zig.exe not found"
    }
    $localVer = & (Join-Path $ZigToolsDir "zig.exe") version
    Write-Host "  Zig: $localVer (.tools/)" -ForegroundColor Green
}

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
    & $VcpkgExe install "${pkg}:x64-windows" --vcpkg-root $VcpkgDir
    if ($LASTEXITCODE -ne 0) { Write-Error "vcpkg install $pkg failed (exit $LASTEXITCODE)" }
}

# ─── Git Clones ─────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[3/8] Cloning thirdparty repos..." -ForegroundColor Cyan

# DirectX Headers
Clone-Repo -Name "directx_headers" `
    -Url "https://github.com/microsoft/DirectX-Headers.git" `
    -Branch "v1.619.1" `
    -RemoveDirs @("test", "googletest")

# D3D12 Memory Allocator
Clone-Repo -Name "d3d12ma" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator.git" `
    -Branch "v3.1.0" `
    -RemoveDirs @("docs", "tools")

# spdlog (logging)
Clone-Repo -Name "spdlog" `
    -Url "https://github.com/gabime/spdlog.git" `
    -Branch "v1.17.0" `
    -RemoveDirs @("bench", "example", "logos", "scripts", "tests")

# Tracy (profiling) — keep only LICENSE + public/ (client lib + headers)
Clone-Repo -Name "tracy" `
    -Url "https://github.com/wolfpld/tracy.git" `
    -Branch "v0.13.1" `
    -RemoveDirs @("capture", "cmake", "csvexport", "doc", "dtl", "examples", "extra",
                  "getopt", "icon", "imgui", "import", "manual", "nfd", "profiler",
                  "python", "server", "test", "update", "zstd")

# Remove build files and Fortran binding (keep only LICENSE + public/)
$tracyPath = Join-Path $ThirdPartyDir "tracy"
if (Test-Path $tracyPath) {
    @("CMakeLists.txt", "Config.cmake.in", "NEWS", "README.md",
      "meson.build", "meson.options") | ForEach-Object {
        $p = Join-Path $tracyPath $_
        if (Test-Path $p) { Remove-Item -Force $p }
    }
    $f90 = Join-Path $tracyPath "public\TracyClient.F90"
    if (Test-Path $f90) { Remove-Item -Force $f90 }
}

# mimalloc (global allocator)
Clone-Repo -Name "mimalloc" `
    -Url "https://github.com/microsoft/mimalloc.git" `
    -Branch "v2.2.4" `
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
    -Branch "apr2025" `
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
    -Branch "1.0.3" `
    -RemoveDirs @("doc", "test", "util")

# EnTT (ECS — header-only, engine/ecs/core/world.h includes <entt/entt.hpp>)
# Pinned to the commit recorded in the git index for this path (tag v3.16.0).
Clone-Repo -Name "entt" `
    -Url "https://github.com/skypjack/entt.git" `
    -Branch "v3.16.0" `
    -RemoveDirs @("docs", "test", "docker")

# nlohmann/json (config parsing)
Download-Header -Name "nlohmann" `
    -Url "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp" `
    -FileName "json.hpp"

# GoogleTest (testing framework)
Clone-Repo -Name "googletest" `
    -Url "https://github.com/google/googletest.git" `
    -Branch "v1.17.0" `
    -RemoveDirs @("ci", "docs", "googlemock", "fake_fuchsia_sdk", "test", "samples")

# SPIRV-Reflect (shader reflection)
Clone-Repo -Name "spirv-reflect" `
    -Url "https://github.com/KhronosGroup/SPIRV-Reflect.git" `
    -Branch "vulkan-sdk-1.4.309.0" `
    -RemoveDirs @("examples", "tests", "docs", "scripts", "cmake")

# xxhash (fast hashing)
Clone-Repo -Name "xxhash" `
    -Url "https://github.com/Cyan4973/xxHash.git" `
    -Branch "v0.8.3" `
    -RemoveDirs @("tests", "doc", "cmake_unofficial", "contrib")

# lz4 (fast compression for .uzpak asset archives)
Clone-Repo -Name "lz4" `
    -Url "https://github.com/lz4/lz4.git" `
    -Branch "v1.10.0" `
    -KeepDirs @("lib") `
    -RemoveDirs @("tests", "doc", "programs", "contrib", "examples", "ossfuzz", "build")

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

# ufbx (FBX file parser — single-file C library, MIT license)
Clone-Repo -Name "ufbx" `
    -Url "https://github.com/ufbx/ufbx.git" `
    -Branch "v0.14.3" `
    -RemoveDirs @("test", "data", "examples", "misc", "extra", "bindings")

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

# spng (SIMD-accelerated PNG decode — faster than stb_image for PNG)
# Single .c + .h library with SSE2/AVX2/NEON row filter acceleration.
# Used by the texture pipeline for PNG decode; stb_image kept for JPEG/BMP/HDR.
Clone-Repo -Name "spng" `
    -Url "https://github.com/randy408/libspng.git" `
    -Branch "v0.7.4" `
    -RemoveDirs @("docs", "tests", "benchmarks", "examples", "fuzzing")

# libjpeg-turbo (fast JPEG decode with SIMD)
Clone-Repo -Name "libjpeg-turbo" `
    -Url "https://github.com/libjpeg-turbo/libjpeg-turbo.git" `
    -Branch "3.1.3" `
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

# Keep only the header + miniz dependency
$texrPath = Join-Path $ThirdPartyDir "tinyexr"
if (Test-Path $texrPath) {
    Get-ChildItem $texrPath -File | Where-Object { $_.Name -notin @("tinyexr.h", "miniz.h", "miniz.c") } | Remove-Item -Force
    # Download miniz if missing (tinyexr requires it for EXR decompression)
    $minizH = Join-Path $texrPath "miniz.h"
    $minizC = Join-Path $texrPath "miniz.c"
    # miniz 3.0.0 from tinyexr deps (with MINIZ_EXPORT stub prepended)
    if (-not (Test-Path $minizH)) {
        Write-Host "  [tinyexr] downloading miniz.h..." -ForegroundColor Yellow
        Download-File -Url "https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.9/deps/miniz/miniz.h" -OutFile $minizH
        # Prepend MINIZ_EXPORT define if missing (standalone builds need this)
        $content = Get-Content $minizH -Raw
        if ($content -notmatch 'MINIZ_EXPORT') {
            "#ifndef MINIZ_EXPORT`n#define MINIZ_EXPORT`n#endif`n$content" | Set-Content $minizH -NoNewline
        }
    }
    if (-not (Test-Path $minizC)) {
        Write-Host "  [tinyexr] downloading miniz.c..." -ForegroundColor Yellow
        Download-File -Url "https://raw.githubusercontent.com/syoyo/tinyexr/v1.0.9/deps/miniz/miniz.c" -OutFile $minizC
    }
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

# etcpak (GPU texture compressor — BC1/BC3/BC7/ETC1/ETC2, same as Godot)
Clone-Repo -Name "etcpak" `
    -Url "https://github.com/wolfpld/etcpak.git" `
    -Branch "master" `
    -RemoveDirs @("build", ".github")

# Remove build files from root (keep Source/ + LICENSE)
$astcPath = Join-Path $ThirdPartyDir "astcenc"
if (Test-Path $astcPath) {
    @("CMakeLists.txt","README.md") | ForEach-Object {
        $p = Join-Path $astcPath $_
        if (Test-Path $p) { Remove-Item -Force $p }
    }
}

# LuaJIT 2.1 (Lua scripting runtime — requires MSVC to build)
Clone-Repo -Name "luajit" `
    -Url "https://github.com/LuaJIT/LuaJIT.git" `
    -Branch "v2.1" `
    -RemoveDirs @("doc", "etc")

$luajitLib = Join-Path $ThirdPartyDir "luajit\src\lua51.lib"
$buildScript = Join-Path $PSScriptRoot "build_luajit.bat"
if (-not (Test-Path $luajitLib)) {
    Write-Host "  [luajit] building with MSVC..." -ForegroundColor Yellow
    cmd /c "`"$buildScript`""
    if (Test-Path $luajitLib) {
        Write-Host "  [luajit] build OK" -ForegroundColor Green
    } else {
        Write-Host "  [luajit] WARN: build failed - run scripts\build_luajit.bat manually from a VS Developer Prompt" -ForegroundColor Red
    }
} else {
    Write-Host "  [luajit] already built, skipping." -ForegroundColor Green
}

# ─── Vulkan Dependencies ──────────────────────────────────────────────────

# Vulkan-Headers (Khronos official API headers)
Clone-Repo -Name "vulkan_headers" `
    -Url "https://github.com/KhronosGroup/Vulkan-Headers.git" `
    -Branch "v1.4.342" `
    -RemoveDirs @("tests", "cmake")

# volk (Vulkan meta-loader — dynamic function dispatch, no static vulkan-1.lib needed)
Clone-Repo -Name "volk" `
    -Url "https://github.com/zeux/volk.git" `
    -Branch "vulkan-sdk-1.4.304.1" `
    -RemoveDirs @("cmake", "test")

# VMA — Vulkan Memory Allocator (AMD, GPU memory sub-allocation)
Clone-Repo -Name "vma" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" `
    -Branch "v3.2.1" `
    -RemoveDirs @("docs", "tools", "cmake")

# Crashpad (crash reporting) — deferred to milestone 2.x when crash reporting is implemented
Write-Host "  [crashpad] skipped (not needed until 2.x crash reporting)" -ForegroundColor DarkYellow

# ─── Post-Processing & Upscaling ──────────────────────────────────────────

# AMD FidelityFX SDK 2.x (FSR4, Radiance Caching, Denoiser)
Clone-Repo -Name "amd-fidelityfx" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" `
    -Branch "main" `
    -RemoveDirs @("docs", "samples", "tools", "PrebuiltSignedDLL", "framework", "BuildFidelityFXSolution.bat")

# AMD FidelityFX SDK 1.1.4 (Brixelizer GI — removed in 2.x)
# Provides ffx_brixelizer.h/.cpp, ffx_brixelizer_gi.h/.cpp, and ~20 HLSL compute shaders
# for real-time SDF-based global illumination. MIT license.
Clone-Repo -Name "amd-fidelityfx-1.1" `
    -Url "https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git" `
    -Branch "v1.1.4" `
    -RemoveDirs @("docs", "samples", "tools", "PrebuiltSignedDLL", "framework", "BuildFidelityFXSolution.bat")

# NVIDIA DLSS SDK (Deep Learning Super Sampling — AI temporal upscaling)
# Provides nvsdk_ngx headers + nvngx_dlss.dll runtime. Requires NVIDIA RTX GPU.
# NOTE: The SDK is ~200MB; runtime/thirdparty/dlss/ is .gitignored.
Clone-Repo -Name "dlss" `
    -Url "https://github.com/NVIDIA/DLSS.git" `
    -Branch "main" `
    -RemoveDirs @("doc", "samples", "utils", "tests")

# SMAA — Subpixel Morphological Anti-Aliasing (header-only HLSL/GLSL)
Clone-Repo -Name "smaa" `
    -Url "https://github.com/iryoku/smaa.git" `
    -Branch "master" `
    -RemoveDirs @("Demo", "Scripts")

# ─── Mesh & Geometry Tools ────────────────────────────────────────────────

# MikkTSpace — Industry-standard tangent space generation (Morten Mikkelsen)
Download-Header -Name "mikktspace" `
    -Url "https://raw.githubusercontent.com/mmikk/MikkTSpace/master/mikktspace.h" `
    -FileName "mikktspace.h"
# Also download the .c implementation
$mikktDir = Join-Path $ThirdPartyDir "mikktspace"
$mikktC = Join-Path $mikktDir "mikktspace.c"
if (-not (Test-Path $mikktC)) {
    Download-File -Url "https://raw.githubusercontent.com/mmikk/MikkTSpace/master/mikktspace.c" -OutFile $mikktC
}

# xatlas — UV atlas generation (lightmaps, LOD baking)
Clone-Repo -Name "xatlas" `
    -Url "https://github.com/jpcy/xatlas.git" `
    -Branch "master" `
    -RemoveDirs @("extra", "viewer", "test")

# meshoptimizer — vertex cache / overdraw / fetch optimization, simplification
# Pinned to the commit recorded in the git index for this path (tag v0.21).
Clone-Repo -Name "meshoptimizer" `
    -Url "https://github.com/zeux/meshoptimizer.git" `
    -Branch "v0.21" `
    -RemoveDirs @("demo", "js", "gltf", "tools")

# fastgltf — glTF 2.0 parser (bundles simdjson under deps/simdjson)
# Pinned to the commit recorded in the git index for this path (tag v0.8.0).
Clone-Repo -Name "fastgltf" `
    -Url "https://github.com/spnda/fastgltf.git" `
    -Branch "v0.8.0" `
    -RemoveDirs @("tests", "examples", "docs")

# fastgltf pulls the simdjson singleheader at CMake configure time. The Zig
# build compiles deps/simdjson/simdjson.cpp directly, so fetch it here instead.
# Version must match SIMDJSON_TARGET_VERSION in fastgltf/CMakeLists.txt.
$simdjsonVersion = "3.9.4"
$simdjsonDir = Join-Path $ThirdPartyDir "fastgltf\deps\simdjson"
foreach ($f in @("simdjson.h", "simdjson.cpp")) {
    $dst = Join-Path $simdjsonDir $f
    if (-not (Test-Path $dst)) {
        New-Item -ItemType Directory -Path $simdjsonDir -Force | Out-Null
        Write-Host "  [simdjson] downloading $f ($simdjsonVersion)..." -ForegroundColor Yellow
        Download-File `
            -Url "https://raw.githubusercontent.com/simdjson/simdjson/v$simdjsonVersion/singleheader/$f" `
            -OutFile $dst
    }
}

# ─── Image Formats ────────────────────────────────────────────────────────

# libpng — PNG image format (16-bit, interlacing, robust asset pipeline)
Clone-Repo -Name "libpng" `
    -Url "https://github.com/pnggroup/libpng.git" `
    -Branch "v1.6.49" `
    -RemoveDirs @("contrib", "projects", "tests")

# libpng needs pnglibconf.h — copy the prebuilt version from scripts/
$libpngDir = Join-Path $ThirdPartyDir "libpng"
$prebuilt  = Join-Path $libpngDir "scripts\pnglibconf.h.prebuilt"
$target    = Join-Path $libpngDir "pnglibconf.h"
if ((Test-Path $prebuilt) -and -not (Test-Path $target)) {
    Copy-Item $prebuilt $target
    Write-Host "  [libpng] copied pnglibconf.h.prebuilt → pnglibconf.h" -ForegroundColor Green
}

# ─── Font & Text Rendering ────────────────────────────────────────────────

# msdfgen — Multi-channel signed distance field font atlas generator
Clone-Repo -Name "msdfgen" `
    -Url "https://github.com/Chlumsky/msdfgen.git" `
    -Branch "v1.13" `
    -RemoveDirs @("cmake", "test")

# FreeType — Font rasterization (hinting, CJK, complex scripts)
Clone-Repo -Name "freetype" `
    -Url "https://github.com/freetype/freetype.git" `
    -Branch "VER-2-13-3" `
    -RemoveDirs @("docs", "tests", "builds")

# ─── Downloads ──────────────────────────────────────────────────────────────

Write-Host ""
Write-Host "[4/8] Downloading prebuilt packages..." -ForegroundColor Cyan

# DXC (DirectX Shader Compiler)
Download-Extract-Zip -Name "dxc" `
    -Url "https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2602/dxc_2026_02_20.zip" `
    -VerifyFile "bin\x64\dxcompiler.dll"

# ─── DirectX 12 Agility SDK ───────────────────────────────────────────────
# D3D12Core.dll must sit next to the exe (see engine/rhi/dx12/dx12_agility.cpp,
# which exports D3D12SDKVersion/D3D12SDKPath). Version must match
# D3D12_SDK_VERSION from thirdparty/directx_headers/include/directx/d3d12.h.
# Shipped as a NuGet package, which is a plain zip with a build/native/ layout.
$agilityDir = Join-Path $ThirdPartyDir "agility"
$agilityCore = Join-Path $agilityDir "bin\x64\D3D12Core.dll"
if (-not (Test-Path $agilityCore)) {
    $agilityVersion = "1.619.2"
    $agilityZip = Join-Path $TempDir "agility.zip"
    Write-Host "  [agility] downloading D3D12 Agility SDK $agilityVersion..." -ForegroundColor Yellow
    Download-File `
        -Url "https://www.nuget.org/api/v2/package/Microsoft.Direct3D.D3D12/$agilityVersion" `
        -OutFile $agilityZip

    $agilityTemp = Join-Path $TempDir "agility_temp"
    Expand-Archive -Path $agilityZip -DestinationPath $agilityTemp -Force

    New-Item -ItemType Directory -Path $agilityDir -Force | Out-Null
    Copy-Item -Path (Join-Path $agilityTemp "build\native\include") -Destination $agilityDir -Recurse -Force
    Copy-Item -Path (Join-Path $agilityTemp "build\native\bin")     -Destination $agilityDir -Recurse -Force
    Copy-Item -Path (Join-Path $agilityTemp "LICENSE.txt")          -Destination $agilityDir -Force -ErrorAction SilentlyContinue

    Remove-Item -Recurse -Force $agilityTemp
    Remove-Item $agilityZip

    if (Test-Path $agilityCore) {
        Write-Host "  [agility] installed $agilityVersion" -ForegroundColor Green
    } else {
        Write-Error "Agility SDK extraction failed - D3D12Core.dll not found"
    }
} else {
    Write-Host "  [agility] already exists, skipping." -ForegroundColor Green
}

# ─── FXC (d3dcompiler_47.dll — SM5.0 fallback path) ───────────────────────
# Not redistributed standalone; sourced from the installed Windows 10/11 SDK.
$fxcDll = Join-Path $ThirdPartyDir "fxc\bin\x64\d3dcompiler_47.dll"
if (-not (Test-Path $fxcDll)) {
    $sdkCandidates = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\*\x64\d3dcompiler_47.dll" -ErrorAction SilentlyContinue |
        Sort-Object { [version]$_.Directory.Parent.Name } -Descending
    if ($sdkCandidates) {
        $src = $sdkCandidates[0].FullName
        New-Item -ItemType Directory -Path (Split-Path $fxcDll) -Force | Out-Null
        Copy-Item $src $fxcDll -Force
        Write-Host "  [fxc] copied from $($sdkCandidates[0].Directory.Parent.Name)" -ForegroundColor Green
    } else {
        Write-Error "d3dcompiler_47.dll not found. Install the Windows 10/11 SDK (VS Installer > Individual Components)."
    }
} else {
    Write-Host "  [fxc] already exists, skipping." -ForegroundColor Green
}

# ─── Metal Dependencies (macOS build cross-compile support) ──────────────────

# metal-cpp (Apple's C++ wrapper for Metal — macOS only, downloaded for cross-ref)
# Using GitHub mirror — easier for automated setup than Apple's developer portal zip.
Clone-Repo -Name "metal-cpp" `
    -Url "https://github.com/AdrenalineJunkieDev/metal-cpp.git" `
    -Branch "metal-cpp_macOS15.2_iOS18.2" `
    -RemoveDirs @()

# Slang (HLSL→MSL/SPIR-V shader compiler, Windows prebuilt)
# macOS uses libslang.dylib; see setup.sh for the macOS download.
$slangArch = "windows-x86_64"
if ([System.Runtime.InteropServices.RuntimeInformation]::OSArchitecture -eq [System.Runtime.InteropServices.Architecture]::Arm64) {
    $slangArch = "windows-aarch64"
}
Download-Extract-Zip -Name "slang" `
    -Url "https://github.com/shader-slang/slang/releases/download/v2025.6.3/slang-2025.6.3-$slangArch.zip" `
    -VerifyFile "bin\slang.dll"

# ─── FFmpeg (H.264 hardware encoding for viewport frame streaming) ────────

# Pre-built LGPL shared libraries from BtbN's automated builds.
# Includes: libavcodec (H.264 encode/decode), libavutil, libswscale (RGBA to NV12).
# LGPL licensed - no GPL codec libraries, safe for proprietary use.
$ffmpegDir = Join-Path $ThirdPartyDir "ffmpeg"

# phase0_fix-vfx-demo-launch-pipeline -- self-healing for incomplete
# extractions. If a prior setup left only lib/+include/ but no bin/,
# the runtime DLLs (swscale-9.dll, avcodec-62.dll, ...) are missing
# and every exe crashes at startup. Detect the gap and force a fresh
# re-download.
$ffmpegBinDir = Join-Path $ffmpegDir "bin"
if ((Test-Path $ffmpegDir) -and -not (Test-Path $ffmpegBinDir)) {
    Write-Host "  [ffmpeg] bin/ missing -- forcing clean re-download..." -ForegroundColor Yellow
    Remove-Item -Recurse -Force $ffmpegDir -ErrorAction SilentlyContinue
}

# Probe an import lib, not the directory: the headers and *.dll.a files are
# committed but *.lib and bin/*.dll are gitignored, so a fresh clone has the
# directory without anything the linker needs.
$ffmpegLib = Join-Path $ffmpegDir "lib\avcodec.lib"
if (-not (Test-Path $ffmpegLib)) {
    $ffmpegZipUrl = "https://github.com/BtbN/FFmpeg-Builds/releases/download/latest/ffmpeg-master-latest-win64-lgpl-shared.zip"
    $ffmpegZip = Join-Path $TempDir "ffmpeg.zip"
    Write-Host "  [ffmpeg] downloading LGPL shared build..." -ForegroundColor Yellow
    Download-File -Url $ffmpegZipUrl -OutFile $ffmpegZip

    # Extract to temp, then merge inner dir into thirdparty/ffmpeg
    $ffmpegTemp = Join-Path $TempDir "ffmpeg_temp"
    Expand-Archive -Path $ffmpegZip -DestinationPath $ffmpegTemp -Force

    # Find the extracted inner directory (name varies with build date)
    $innerDir = Get-ChildItem $ffmpegTemp -Directory | Select-Object -First 1
    if ($innerDir) {
        New-Item -ItemType Directory -Path $ffmpegDir -Force | Out-Null
        Copy-Item -Path (Join-Path $innerDir.FullName "*") -Destination $ffmpegDir -Recurse -Force
        Write-Host "  [ffmpeg] installed: $($innerDir.Name)" -ForegroundColor Green
    } else {
        Write-Error "FFmpeg extraction failed - no inner directory found"
    }

    Remove-Item -Recurse -Force $ffmpegTemp -ErrorAction SilentlyContinue
    Remove-Item $ffmpegZip -ErrorAction SilentlyContinue

    # Remove docs we don't need (keep LICENSE for LGPL compliance)
    $ffmpegRemove = @("doc", "man", "README.txt")
    foreach ($f in $ffmpegRemove) {
        $p = Join-Path $ffmpegDir $f
        if (Test-Path $p) { Remove-Item -Recurse -Force $p }
    }
    # Remove executables (we only need libraries + headers)
    @("ffmpeg.exe", "ffprobe.exe", "ffplay.exe") | ForEach-Object {
        $p = Join-Path $ffmpegDir "bin\$_"
        if (Test-Path $p) { Remove-Item -Force $p }
    }
} else {
    Write-Host "  [ffmpeg] already exists, skipping." -ForegroundColor Green
}

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
        # No 2>&1: npm writes deprecation notices to stderr, and merging them
        # into the success stream makes each one a terminating NativeCommandError
        # under $ErrorActionPreference = "Stop".
        npm install --no-fund --loglevel=error | Out-Null
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
    @{ Name = "spng";             File = "spng\spng.h" },
    @{ Name = "libjpeg-turbo";    File = "src\jpeglib.h" },
    @{ Name = "tinyexr";          File = "tinyexr.h" },
    @{ Name = "basis_universal";  File = "transcoder\basisu_transcoder.h" },
    @{ Name = "astcenc";          File = "Source\astcenc.h" },
    @{ Name = "vulkan_headers";   File = "include\vulkan\vulkan.h" },
    @{ Name = "volk";             File = "volk.h" },
    @{ Name = "vma";              File = "include\vk_mem_alloc.h" },
    @{ Name = "metal-cpp";        File = "Metal\Metal.hpp" },
    @{ Name = "slang";            File = "include\slang.h" },
    @{ Name = "luajit";           File = "src\lua.h" },
    @{ Name = "ufbx";             File = "ufbx.h" },
    @{ Name = "ffmpeg";           File = "include\libavcodec\avcodec.h" }
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

# ─── RenderDoc API ─────────────────────────────────────────────────────────
# Single-header API for in-application frame capture integration.
# Source: https://github.com/baldurk/renderdoc
Download-Header -Name "renderdoc" `
    -Url "https://raw.githubusercontent.com/baldurk/renderdoc/v1.35/renderdoc/api/app/renderdoc_app.h" `
    -FileName "renderdoc_app.h"

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
