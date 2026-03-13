#!/usr/bin/env bash
# setup.sh — UzEngine dependency setup script (macOS/Linux)
# Downloads and configures all thirdparty dependencies for the runtime.
# Run from project root: ./scripts/setup.sh
#
# macOS: Downloads all cross-platform + Metal-specific deps
# Linux: Downloads all cross-platform deps (Metal deps skipped)

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$SCRIPT_DIR")"
THIRDPARTY_DIR="$ROOT_DIR/runtime/thirdparty"
TEMP_DIR="$ROOT_DIR/.setup_temp"

PLATFORM="$(uname -s)"   # Darwin | Linux
ARCH="$(uname -m)"       # x86_64 | aarch64 | arm64

mkdir -p "$THIRDPARTY_DIR"
mkdir -p "$TEMP_DIR"

# ─── Colors ─────────────────────────────────────────────────────────────────

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
MAGENTA='\033[0;35m'
CYAN='\033[0;36m'
GRAY='\033[0;37m'
DARK_YELLOW='\033[0;33m'
NC='\033[0m' # No Color

echo -e "${CYAN}=== UzEngine Setup ===${NC}"
echo "Platform: $PLATFORM ($ARCH)"
echo "Thirdparty: $THIRDPARTY_DIR"
echo ""

# ─── Helpers ────────────────────────────────────────────────────────────────

# download_file <url> <out_file>
download_file() {
    local url="$1"
    local out_file="$2"
    echo -e "${GRAY}  Downloading $url...${NC}"
    curl -sL -o "$out_file" "$url"
}

# clone_repo <name> <url> <branch> [remove_dirs...]
# Remove dirs are passed as space-separated string in arg $4
clone_repo() {
    local name="$1"
    local url="$2"
    local branch="$3"
    local remove_dirs="${4:-}"
    local path="$THIRDPARTY_DIR/$name"

    if [[ -d "$path" ]] && [[ -n "$(ls -A "$path" 2>/dev/null)" ]]; then
        echo -e "${GREEN}  [$name] already exists, skipping.${NC}"
        return
    fi

    [[ -d "$path" ]] && rm -rf "$path"

    echo -e "${YELLOW}  [$name] cloning $branch...${NC}"
    git clone --depth 1 -b "$branch" "$url" "$path" --quiet

    # Remove .git
    rm -rf "$path/.git"

    # Remove specified subdirs
    if [[ -n "$remove_dirs" ]]; then
        for dir in $remove_dirs; do
            local p="$path/$dir"
            [[ -d "$p" ]] && rm -rf "$p"
        done
    fi
}

# download_extract_zip <name> <url> [sub_path]
# sub_path: if provided, copy contents of sub_path inside zip to dest
download_extract_zip() {
    local name="$1"
    local url="$2"
    local sub_path="${3:-}"
    local path="$THIRDPARTY_DIR/$name"

    if [[ -d "$path" ]]; then
        echo -e "${GREEN}  [$name] already exists, skipping.${NC}"
        return
    fi

    local zip="$TEMP_DIR/$name.zip"
    echo -e "${YELLOW}  [$name] downloading...${NC}"
    download_file "$url" "$zip"

    if [[ -n "$sub_path" ]]; then
        local temp_extract="$TEMP_DIR/${name}_temp"
        mkdir -p "$temp_extract"
        unzip -q "$zip" -d "$temp_extract"
        mkdir -p "$path"
        cp -r "$temp_extract/$sub_path/." "$path/"
        rm -rf "$temp_extract"
    else
        mkdir -p "$path"
        unzip -q "$zip" -d "$path"
    fi

    rm -f "$zip"
}

# download_header <name> <url> <filename>
download_header() {
    local name="$1"
    local url="$2"
    local filename="$3"
    local dir="$THIRDPARTY_DIR/$name"

    if [[ -d "$dir" ]]; then
        echo -e "${GREEN}  [$name] already exists, skipping.${NC}"
        return
    fi

    echo -e "${YELLOW}  [$name] downloading header...${NC}"
    mkdir -p "$dir"
    download_file "$url" "$dir/$filename"
}

# apply_patches <name>
apply_patches() {
    local name="$1"
    local patch_dir="$THIRDPARTY_DIR/$name/patches"

    [[ -d "$patch_dir" ]] || return

    local patches=()
    while IFS= read -r -d '' f; do
        patches+=("$f")
    done < <(find "$patch_dir" -name "*.patch" -print0 | sort -z)

    [[ ${#patches[@]} -eq 0 ]] && return

    for patch in "${patches[@]}"; do
        local patch_name
        patch_name="$(basename "$patch")"
        echo -e "${MAGENTA}  [$name] applying $patch_name...${NC}"
        pushd "$ROOT_DIR" > /dev/null
        if ! git apply --directory="runtime/thirdparty/$name" "$patch" 2>/dev/null; then
            echo -e "${DARK_YELLOW}  [$name] WARN: patch may already be applied: $patch_name${NC}"
        fi
        popd > /dev/null
    done
}

# require_cmd <cmd> — exits if command is not found
require_cmd() {
    if ! command -v "$1" &>/dev/null; then
        echo -e "${RED}  ERROR: '$1' is required but not found.${NC}"
        echo "  Install it and re-run setup.sh"
        exit 1
    fi
}

# ─── Prerequisites ──────────────────────────────────────────────────────────

echo -e "${CYAN}[1/9] Checking prerequisites...${NC}"

if [[ "$PLATFORM" == "Darwin" ]]; then
    # macOS: try to install missing tools via Homebrew
    if ! command -v brew &>/dev/null; then
        echo -e "${RED}  Homebrew not found. Install it from https://brew.sh and re-run.${NC}"
        exit 1
    fi

    for tool in zig cmake node; do
        if ! command -v "$tool" &>/dev/null; then
            echo -e "${YELLOW}  $tool not found. Installing via brew...${NC}"
            brew install "$tool"
        fi
    done
else
    # Linux: just require tools to be present (package managers vary too much)
    require_cmd zig
    require_cmd cmake
    require_cmd node
fi

ZIG_VER="$(zig version)"
NODE_VER="$(node --version)"
echo -e "${GREEN}  Zig: $ZIG_VER${NC}"
echo -e "${GREEN}  CMake: $(cmake --version | head -1)${NC}"
echo -e "${GREEN}  Node.js: $NODE_VER${NC}"

# ─── vcpkg (macOS/Linux — for zlib, zstd) ────────────────────────────────────

echo ""
echo -e "${CYAN}[2/9] Setting up vcpkg...${NC}"

VCPKG_DIR="$THIRDPARTY_DIR/vcpkg"
VCPKG_EXE="$VCPKG_DIR/vcpkg"

if [[ ! -f "$VCPKG_EXE" ]]; then
    echo -e "${YELLOW}  Cloning vcpkg...${NC}"
    git clone --depth 1 https://github.com/Microsoft/vcpkg.git "$VCPKG_DIR" --quiet
    "$VCPKG_DIR/bootstrap-vcpkg.sh" -disableMetrics
    if [[ ! -f "$VCPKG_EXE" ]]; then
        echo -e "${RED}  vcpkg bootstrap failed!${NC}"
        exit 1
    fi
else
    echo -e "${GREEN}  vcpkg already installed.${NC}"
fi

VCPKG_TRIPLET="x64-linux"
if [[ "$PLATFORM" == "Darwin" ]]; then
    if [[ "$ARCH" == "arm64" ]] || [[ "$ARCH" == "aarch64" ]]; then
        VCPKG_TRIPLET="arm64-osx"
    else
        VCPKG_TRIPLET="x64-osx"
    fi
fi

for pkg in zlib zstd; do
    echo -e "${YELLOW}  vcpkg: installing $pkg...${NC}"
    "$VCPKG_EXE" install "${pkg}:${VCPKG_TRIPLET}" --vcpkg-root "$VCPKG_DIR" 2>/dev/null || true
done

# ─── Git Clones (cross-platform) ─────────────────────────────────────────────

echo ""
echo -e "${CYAN}[3/9] Cloning thirdparty repos...${NC}"

# Note: DirectX-specific deps (directx_headers, d3d12ma, dxc, agility, fxc)
# are Windows-only and intentionally omitted here.

# spdlog (logging)
clone_repo "spdlog" \
    "https://github.com/gabime/spdlog.git" \
    "v1.15.3" \
    "bench example logos scripts tests"

# Tracy (profiling) — keep only LICENSE + public/ (client lib + headers)
clone_repo "tracy" \
    "https://github.com/wolfpld/tracy.git" \
    "v0.13.1" \
    "capture cmake csvexport doc dtl examples extra getopt icon imgui import manual nfd profiler python server test update zstd"

# Remove build files and Fortran binding from tracy (keep only LICENSE + public/)
TRACY_PATH="$THIRDPARTY_DIR/tracy"
if [[ -d "$TRACY_PATH" ]]; then
    for f in CMakeLists.txt Config.cmake.in NEWS README.md meson.build meson.options; do
        [[ -f "$TRACY_PATH/$f" ]] && rm -f "$TRACY_PATH/$f"
    done
    [[ -f "$TRACY_PATH/public/TracyClient.F90" ]] && rm -f "$TRACY_PATH/public/TracyClient.F90"
fi

# mimalloc (global allocator)
clone_repo "mimalloc" \
    "https://github.com/microsoft/mimalloc.git" \
    "v2.1.7" \
    "docs test bench ide out"

# jemalloc (alternative allocator)
clone_repo "jemalloc" \
    "https://github.com/jemalloc/jemalloc.git" \
    "5.3.0" \
    "doc test scripts msvc"

# dlmalloc (lightweight allocator — single-file download)
download_header "dlmalloc" \
    "https://gee.cs.oswego.edu/pub/misc/malloc.c" \
    "dlmalloc.c"

# atomic_queue (lock-free queue)
clone_repo "atomic_queue" \
    "https://github.com/max0x7ba/atomic_queue.git" \
    "master" \
    "benchmarks doc html scripts src"

# DirectXMath (cross-platform SIMD math, works on all platforms via sal.h stub)
clone_repo "directxmath" \
    "https://github.com/microsoft/DirectXMath.git" \
    "oct2024" \
    "SHMath XDSP Spherical Extensions MatrixStack Tests Utilities"

# sal.h stub for non-MSVC platforms (DirectXMath requires sal.h)
SAL_PATH="$THIRDPARTY_DIR/directxmath/Inc/sal.h"
if [[ ! -f "$SAL_PATH" ]]; then
    echo -e "${YELLOW}  [sal.h] downloading cross-platform stub...${NC}"
    download_file \
        "https://raw.githubusercontent.com/dotnet/runtime/main/src/coreclr/pal/inc/rt/sal.h" \
        "$SAL_PATH"
else
    echo -e "${GREEN}  [sal.h] already exists, skipping.${NC}"
fi

# glm (math, header-only)
clone_repo "glm" \
    "https://github.com/g-truc/glm.git" \
    "1.0.1" \
    "doc test util"

# nlohmann/json (config parsing — single header)
download_header "nlohmann" \
    "https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp" \
    "json.hpp"

# GoogleTest (testing framework)
clone_repo "googletest" \
    "https://github.com/google/googletest.git" \
    "v1.15.2" \
    "ci docs googlemock fake_fuchsia_sdk test samples"

# SPIRV-Reflect (shader reflection)
clone_repo "spirv-reflect" \
    "https://github.com/KhronosGroup/SPIRV-Reflect.git" \
    "vulkan-sdk-1.3.268.0" \
    "examples tests docs scripts cmake"

# xxhash (fast hashing)
clone_repo "xxhash" \
    "https://github.com/Cyan4973/xxHash.git" \
    "v0.8.2" \
    "tests doc cmake_unofficial contrib"

# SSE Math Fun (SIMD math functions)
clone_repo "sse_mathfun" \
    "https://github.com/RJVB/sse_mathfun.git" \
    "master" \
    ""

# MikkTSpace (tangent space generation)
clone_repo "mikktspace" \
    "https://github.com/mmikk/MikkTSpace.git" \
    "master" \
    ""

# stb (image loading/writing/resizing — header-only, keep only needed headers)
clone_repo "stb" \
    "https://github.com/nothings/stb.git" \
    "master" \
    "docs data tests tools deprecated"

# Keep only the headers we use
STB_PATH="$THIRDPARTY_DIR/stb"
STB_KEEP=("stb_image.h" "stb_image_write.h" "stb_image_resize2.h" "stb_truetype.h" "LICENSE")
if [[ -d "$STB_PATH" ]]; then
    find "$STB_PATH" -maxdepth 1 -type f | while read -r f; do
        fname="$(basename "$f")"
        keep=false
        for k in "${STB_KEEP[@]}"; do
            [[ "$fname" == "$k" ]] && keep=true && break
        done
        [[ "$keep" == false ]] && rm -f "$f"
    done
    find "$STB_PATH" -maxdepth 1 -mindepth 1 -type d -exec rm -rf {} + 2>/dev/null || true
fi

# libjpeg-turbo (fast JPEG decode with SIMD)
clone_repo "libjpeg-turbo" \
    "https://github.com/libjpeg-turbo/libjpeg-turbo.git" \
    "3.0.3" \
    "doc java release win fuzz cmakescripts md5 testimages sharedlib"

# Remove CLI tools, test files, build configs (keep only lib source + headers)
JT_PATH="$THIRDPARTY_DIR/libjpeg-turbo"
if [[ -d "$JT_PATH" ]]; then
    JT_REMOVE=(
        "BUILDING.md" "ChangeLog.md" "README.md" "README.ijg" "change.log"
        "coderules.txt" "libjpeg.txt" "structure.txt" "usage.txt" "wizard.txt"
        "jconfig.txt" "doxygen.config" "doxygen-extra.css"
        "cjpeg.1" "djpeg.1" "jpegtran.1" "rdjpgcom.1" "wrjpgcom.1"
        "cjpeg.c" "djpeg.c" "jpegtran.c" "rdjpgcom.c" "wrjpgcom.c"
        "cdjpeg.c" "cdjpeg.h"
        "rdbmp.c" "rdcolmap.c" "rdgif.c" "rdppm.c" "rdswitch.c" "rdtarga.c"
        "wrbmp.c" "wrgif.c" "wrppm.c" "wrtarga.c"
        "tjbench.c" "tjexample.c" "tjunittest.c" "tjutil.c" "tjutil.h" "strtest.c"
        "jcstest.c" "jpeg_nbits.c" "example.c"
        "croptest.in" "tjbenchtest.in" "tjexampletest.in"
        "CMakeLists.txt" "jconfig.h.in" "jconfigint.h.in" "jversion.h.in" "libjpeg.map.in"
        "turbojpeg-jni.c" "turbojpeg-mapfile" "turbojpeg-mapfile.jni" "turbojpeg-mp.c"
    )
    for f in "${JT_REMOVE[@]}"; do
        [[ -f "$JT_PATH/$f" ]] && rm -f "$JT_PATH/$f"
    done
fi

# tinyexr (EXR/HDR image format — single header only)
clone_repo "tinyexr" \
    "https://github.com/syoyo/tinyexr.git" \
    "v1.0.9" \
    "examples experimental test deps cmake jni"

# Keep only the header
TEXR_PATH="$THIRDPARTY_DIR/tinyexr"
if [[ -d "$TEXR_PATH" ]]; then
    find "$TEXR_PATH" -maxdepth 1 -type f | while read -r f; do
        [[ "$(basename "$f")" != "tinyexr.h" ]] && rm -f "$f"
    done
fi

# basis_universal (GPU texture compression — BC7, ASTC, ETC2)
clone_repo "basis_universal" \
    "https://github.com/BinomialLLC/basis_universal.git" \
    "master" \
    "webgl test_files bin OpenCL build cmd_help contrib example example_capi example_transcoding khronos python shader_deblocking zstd encoder_lib"

# Remove build/tool files from root (keep encoder/, transcoder/, LICENSE*)
BASIS_PATH="$THIRDPARTY_DIR/basis_universal"
if [[ -d "$BASIS_PATH" ]]; then
    BASIS_REMOVE=(
        "CMakeLists.txt" "CppProperties.json" "README.md" "readme_wasi.md" "NOTICE"
        "all_builds.py" "basisu.manifest" "basisu.sln" "basisu.vcxproj"
        "basisu.vcxproj.filters" "basisu_tool.cpp" "basisu_tool_help.h"
        "build_python_win.bat" "encoder_lvl_vs_perf.png"
    )
    for f in "${BASIS_REMOVE[@]}"; do
        [[ -f "$BASIS_PATH/$f" ]] && rm -f "$BASIS_PATH/$f"
    done
fi

# astcenc (ASTC texture encoder/decoder)
clone_repo "astcenc" \
    "https://github.com/ARM-software/astc-encoder.git" \
    "5.3.0" \
    "Docs Test Jenkins Utils"

# Remove build files from root (keep Source/ + LICENSE)
ASTC_PATH="$THIRDPARTY_DIR/astcenc"
if [[ -d "$ASTC_PATH" ]]; then
    for f in CMakeLists.txt README.md; do
        [[ -f "$ASTC_PATH/$f" ]] && rm -f "$ASTC_PATH/$f"
    done
fi

# ─── Vulkan Dependencies ─────────────────────────────────────────────────────

# Vulkan-Headers (Khronos official API headers)
clone_repo "vulkan_headers" \
    "https://github.com/KhronosGroup/Vulkan-Headers.git" \
    "vulkan-sdk-1.4.309.0" \
    "tests cmake"

# volk (Vulkan meta-loader — dynamic function dispatch, no static vulkan-1.lib needed)
clone_repo "volk" \
    "https://github.com/zeux/volk.git" \
    "vulkan-sdk-1.4.309.0" \
    "cmake test"

# VMA — Vulkan Memory Allocator (AMD, GPU memory sub-allocation)
clone_repo "vma" \
    "https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator.git" \
    "v3.2.1" \
    "docs tools cmake"

# Crashpad — deferred to milestone 2.x
echo -e "${DARK_YELLOW}  [crashpad] skipped (not needed until 2.x crash reporting)${NC}"

# ─── Prebuilt Downloads ───────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[4/9] Downloading prebuilt packages...${NC}"

# ─── macOS-only: Metal Dependencies ──────────────────────────────────────────

if [[ "$PLATFORM" == "Darwin" ]]; then
    # metal-cpp (Apple's C++ wrapper for Metal, header-only)
    # Using GitHub mirror — easier for automated setup than Apple's developer portal zip.
    clone_repo "metal-cpp" \
        "https://github.com/AdrenalineJunkieDev/metal-cpp.git" \
        "metal-cpp_macOS15.2_iOS18.2" \
        ""

    # Slang (HLSL→MSL/SPIR-V shader compiler) — macOS prebuilt
    SLANG_PATH="$THIRDPARTY_DIR/slang"
    if [[ -d "$SLANG_PATH" ]]; then
        echo -e "${GREEN}  [slang] already exists, skipping.${NC}"
    else
        SLANG_VERSION="2025.6.3"
        # Map uname -m to Slang release naming
        if [[ "$ARCH" == "arm64" ]] || [[ "$ARCH" == "aarch64" ]]; then
            SLANG_ARCH="aarch64"
        else
            SLANG_ARCH="x86_64"
        fi
        SLANG_URL="https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-macos-${SLANG_ARCH}.zip"
        echo -e "${YELLOW}  [slang] downloading (macos-$SLANG_ARCH)...${NC}"
        SLANG_ZIP="$TEMP_DIR/slang.zip"
        download_file "$SLANG_URL" "$SLANG_ZIP"
        mkdir -p "$SLANG_PATH"
        unzip -q "$SLANG_ZIP" -d "$SLANG_PATH"
        rm -f "$SLANG_ZIP"
        echo -e "${GREEN}  [slang] OK${NC}"
    fi
else
    # Linux: Slang available but Metal skipped
    echo -e "${DARK_YELLOW}  [metal-cpp] skipped (macOS only)${NC}"

    SLANG_PATH="$THIRDPARTY_DIR/slang"
    if [[ -d "$SLANG_PATH" ]]; then
        echo -e "${GREEN}  [slang] already exists, skipping.${NC}"
    else
        SLANG_VERSION="2025.6.3"
        if [[ "$ARCH" == "aarch64" ]]; then
            SLANG_ARCH="aarch64"
        else
            SLANG_ARCH="x86_64"
        fi
        SLANG_URL="https://github.com/shader-slang/slang/releases/download/v${SLANG_VERSION}/slang-${SLANG_VERSION}-linux-${SLANG_ARCH}.zip"
        echo -e "${YELLOW}  [slang] downloading (linux-$SLANG_ARCH)...${NC}"
        SLANG_ZIP="$TEMP_DIR/slang.zip"
        download_file "$SLANG_URL" "$SLANG_ZIP"
        mkdir -p "$SLANG_PATH"
        unzip -q "$SLANG_ZIP" -d "$SLANG_PATH"
        rm -f "$SLANG_ZIP"
        echo -e "${GREEN}  [slang] OK${NC}"
    fi
fi

# ─── Global Cleanup ──────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[5/9] Cleaning up...${NC}"

JUNK_DIRS=(".github" ".vscode" ".ci" ".circleci")
JUNK_FILES=(
    ".gitignore" ".gitattributes" ".gitmodules"
    ".clang-format" ".clang-tidy" ".editorconfig"
    ".travis.yml" ".pre-commit-config.yaml"
    "docker-compose.yml" "mkdocs.yml"
    "codecov.yml" "CONTRIBUTING.md"
    "CODE_OF_CONDUCT.md" "SECURITY.md"
    "Makefile" "Doxyfile" "appveyor.yml"
)

find "$THIRDPARTY_DIR" -mindepth 1 -maxdepth 1 -type d | while read -r repo_path; do
    repo_name="$(basename "$repo_path")"
    [[ "$repo_name" == "vcpkg" ]] && continue

    # Remove leftover .git
    [[ -d "$repo_path/.git" ]] && rm -rf "$repo_path/.git"

    for dir in "${JUNK_DIRS[@]}"; do
        [[ -d "$repo_path/$dir" ]] && rm -rf "$repo_path/$dir"
    done
    for file in "${JUNK_FILES[@]}"; do
        [[ -f "$repo_path/$file" ]] && rm -f "$repo_path/$file"
    done
done

# Remove temp dir
[[ -d "$TEMP_DIR" ]] && rm -rf "$TEMP_DIR"

# ─── Apply Patches ───────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[6/9] Applying thirdparty patches...${NC}"

find "$THIRDPARTY_DIR" -mindepth 1 -maxdepth 1 -type d | while read -r repo_path; do
    repo_name="$(basename "$repo_path")"
    [[ "$repo_name" == "vcpkg" ]] && continue
    apply_patches "$repo_name"
done

echo -e "${GREEN}  Patches applied.${NC}"

# ─── npm install ─────────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[7/9] Installing Node dependencies...${NC}"

for mod in napi mcp extension; do
    mod_path="$ROOT_DIR/$mod"
    if [[ -f "$mod_path/package.json" ]]; then
        echo -e "${YELLOW}  [$mod] npm install...${NC}"
        pushd "$mod_path" > /dev/null
        npm install --silent
        popd > /dev/null
        echo -e "${GREEN}  [$mod] OK${NC}"
    fi
done

# ─── Verify Libraries ────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[8/9] Verifying libraries...${NC}"

declare -a VERIFY_LIBS
VERIFY_LIBS=(
    "stb:stb_image.h"
    "libjpeg-turbo:jpeglib.h"
    "tinyexr:tinyexr.h"
    "basis_universal:transcoder/basisu_transcoder.h"
    "astcenc:Source/astcenc.h"
    "vulkan_headers:include/vulkan/vulkan.h"
    "volk:volk.h"
    "vma:include/vk_mem_alloc.h"
    "slang:include/slang.h"
)

if [[ "$PLATFORM" == "Darwin" ]]; then
    VERIFY_LIBS+=("metal-cpp:Metal/Metal.hpp")
fi

ALL_OK=true
for entry in "${VERIFY_LIBS[@]}"; do
    lib_name="${entry%%:*}"
    lib_file="${entry##*:}"
    check="$THIRDPARTY_DIR/$lib_name/$lib_file"
    if [[ -f "$check" ]]; then
        echo -e "${GREEN}  [$lib_name] OK${NC}"
    else
        echo -e "${RED}  [$lib_name] MISSING: $check${NC}"
        ALL_OK=false
    fi
done

if [[ "$ALL_OK" == false ]]; then
    echo -e "${YELLOW}  WARNING: Some libraries are incomplete. Check above.${NC}"
fi

# ─── Summary ─────────────────────────────────────────────────────────────────

echo ""
echo -e "${CYAN}[9/9] Summary${NC}"

DIR_COUNT="$(find "$THIRDPARTY_DIR" -mindepth 1 -maxdepth 1 -type d | grep -v '/vcpkg$' | wc -l | tr -d ' ')"
echo -e "${GREEN}  $DIR_COUNT thirdparty libraries installed${NC}"

echo ""
echo -e "${GREEN}=== Setup Complete ===${NC}"
echo "Thirdparty libraries installed at: $THIRDPARTY_DIR"
echo ""
echo "Next: scripts/build.sh"
