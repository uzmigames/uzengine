# Build System Spec

## Overview

The UzEngine build system transforms a development project into a distributable game. It includes: asset cooking (import + optimize + compress), PAK packaging, binary compilation, staging, and platform-specific packaging (installer, APK, etc.).

---

## 1. Build Pipeline Stages

```
Source Project
├── scenes/     ← .uzscene (copied as-is)
├── materials/  ← .uzmat (copied as-is)
├── shaders/    ← .hlsl / .glsl (compiled offline in shipping build)
├── assets/     ← .fbx/.png/.wav (needs cooking)
└── project.uzproj

          COOK STAGE
              ↓
.import/
├── meshes/*.uzmesh   (binary, optimized, LODs)
├── meshes/*.uzskel   (text)
├── meshes/*.uzanim   (binary)
├── textures/*.dds    (BC7/BC5/BC6H)
├── audio/*.ogg       (normalized)
└── shaders/*.dxil    (pre-compiled DXIL for shipping)

          PACK STAGE
              ↓
build/
└── game.uzpak   (all cooked assets, zstd-compressed, directory at end)

          LINK STAGE
              ↓
UzEngine-Runtime.exe (built with UZ_SHIPPING=1, no editor code)

          STAGE STAGE
              ↓
staged/windows/
├── MyGame.exe
├── game.uzpak
└── *.dll (turbojpeg, etc.)

          PACKAGE STAGE
              ↓
dist/
└── MyGame-1.0-windows-setup.exe   (NSIS installer, signed)
```

---

## 2. Cook Stage

### Asset Cooking Rules

| Source | Cook Rule | Output |
|--------|----------|--------|
| `.fbx` / `.glb` | ufbx/fastgltf + meshoptimizer LODs | `.uzmesh`, `.uzskel`, `.uzanim` |
| `.png` / `.jpg` | DirectXTex: generate mipmaps, BC7/BC5 compress | `.dds` |
| `.hdr` | DirectXTex: to BC6H, equirect→cubemap | `.dds` (cubemap) |
| `.wav` | Normalize, encode to OGG Vorbis (libvorbis) | `.ogg` |
| `.ogg` | Re-normalize, validate | `.ogg` |
| `.uzscene` | Copy as-is | `.uzscene` |
| `.uzmat` | Resolve shader references, copy | `.uzmat` |
| `.hlsl` | DXC offline compile (shipping: no runtime recompile) | `.dxil` |

### Incremental Cook

Controlled by `.import.json` sidecars:
- Each source file has a `sha256` hash stored
- Cook is skipped if hash matches and output exists
- Force re-cook: `uzengine cook --force`

### Cook Configuration (in `project.uzproj`)

```toml
[build]
cook_threads = 8

[build.textures]
default_format = "BC7"
normal_format  = "BC5"
hdr_format     = "BC6H"
mip_filter     = "box"
max_size       = 4096

[build.meshes]
lod_count    = 4
lod_ratios   = [1.0, 0.5, 0.25, 0.1]
max_bones    = 4

[build.audio]
target_loudness_lufs = -16.0
sample_rate = 48000

[build.shaders]
offline_compile = true      # compile all shaders at build time (no DXC at runtime)
optimization    = "O3"
```

---

## 3. Pack Stage

Assembles cooked assets into a `.uzpak` file. See `asset-pipeline.md` for format details.

```bash
uzengine pack --project=./ --output=build/game.uzpak
```

Output: all cooked assets in a single memory-mappable PAK with zstd compression.

**Selective packing** (update patches):
```bash
uzengine pack --diff-from=game_1.0.uzpak --output=patch_1.1.uzpak
```

---

## 4. Runtime Binary (Shipping vs Editor)

### CMake Targets

```cmake
# Editor build (default)
add_executable(UzEngine WIN32
    ${ENGINE_SOURCES}
    ${EDITOR_SOURCES}      # editor panels, asset importers, DXC runtime
)

# Shipping build (export template, no editor code)
add_executable(UzEngine-Runtime WIN32
    ${ENGINE_SOURCES}
    # No EDITOR_SOURCES
)
target_compile_definitions(UzEngine-Runtime PRIVATE UZ_SHIPPING=1)
```

### Code gated by `UZ_SHIPPING`

```cpp
// Editor-only code is compiled out in shipping:
#ifndef UZ_SHIPPING
    IpcPoller::start();      // no IPC server in shipping
    EditorPanels::init();    // no editor panels
    DXC::init_runtime();     // no runtime shader compilation
#endif

// Always present:
SceneManager::load(startup_scene);
Renderer::run_loop();
```

---

## 5. Platform Targets

### Platform Matrix

| Platform | Renderer | Audio | Input | Build Tool |
|---------|---------|-------|-------|-----------|
| **Windows 10+** (primary) | DX12 | miniaudio | Win32 + XInput | MSVC + CMake |
| **Linux** (Phase 4) | Vulkan | miniaudio | SDL2 | GCC/Clang + CMake |
| **macOS** (Phase 4) | Metal | CoreAudio | AppKit | Clang/Xcode + CMake |
| **Android** (Phase 5) | Vulkan (GLES3 fallback) | miniaudio | Android NDK | NDK + Gradle |

### Cross-Platform Abstraction

```cpp
// src/platform/platform.h — thin platform abstraction
namespace Platform {
    void*     create_window(const char* title, int w, int h);
    void      swap_buffers(void* window);
    bool      poll_events(void* window);   // returns false on close
    double    get_time_seconds();
    void*     load_library(const char* name);
    void*     get_proc(void* lib, const char* name);
}

// Platform implementations:
// src/platform/win32.cpp   — CreateWindowEx, Win32 loop
// src/platform/linux.cpp   — XCB / Wayland (Phase 4)
// src/platform/macos.mm    — NSWindow / Metal (Phase 4)
// src/platform/android.cpp — ANativeWindow / GameActivity (Phase 5)
```

### Android Notes

```bash
# From Windows, cross-compile for Android ARM64
cmake -B build-android \
  -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
  -DANDROID_ABI=arm64-v8a \
  -DANDROID_PLATFORM=android-26 \
  -GNinja

cmake --build build-android
```

---

## 6. Packaging

### Windows — NSIS Installer

```nsis
; packaging/windows/installer.nsi
!define PRODUCT_NAME "MyGame"
!define PRODUCT_VERSION "1.0.0"

Name "${PRODUCT_NAME} ${PRODUCT_VERSION}"
OutFile "dist\${PRODUCT_NAME}-${PRODUCT_VERSION}-setup.exe"
InstallDir "$PROGRAMFILES64\${PRODUCT_NAME}"

Section "Install"
  SetOutPath "$INSTDIR"
  File "staged\windows\MyGame.exe"
  File "staged\windows\game.uzpak"
  File "staged\windows\turbojpeg.dll"
  CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\MyGame.exe"
SectionEnd

Section "Uninstall"
  RMDir /r "$INSTDIR"
  Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
SectionEnd
```

Generated by CMake CPack:
```cmake
include(CPack)
set(CPACK_GENERATOR "NSIS")
set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/icon.ico")
```

### Windows — Code Signing

```bash
# Azure Trusted Signing (~$10/month, no HSM hardware needed)
signtool.exe sign \
  /tr http://timestamp.acs.microsoft.com /td SHA256 /fd SHA256 \
  /dlib "Azure.CodeSigning.Dlib.dll" \
  /dmdf "signing_metadata.json" \
  "dist/MyGame-1.0.0-setup.exe"
```

### macOS (Phase 4) — Notarization

```bash
codesign --deep --force --options runtime \
  --sign "Developer ID Application: YourName (TEAMID)" MyGame.app
xcrun notarytool submit MyGame.dmg --wait \
  --apple-id you@example.com --team-id TEAMID --password APP_SPECIFIC_PASS
xcrun stapler staple MyGame.dmg
```

### Android (Phase 5) — APK Signing

```bash
apksigner sign --ks release.keystore --ks-key-alias MyGame \
  --ks-pass pass:password dist/MyGame.apk
```

---

## 7. Build Tool CLI (`uzengine` CLI)

```bash
# Cook all assets (incremental)
uzengine cook

# Cook all assets (force re-cook)
uzengine cook --force

# Pack cooked assets into PAK
uzengine pack --output=build/game.uzpak

# Full build: cook + pack + compile + stage
uzengine build --platform=windows --config=release

# Build and sign
uzengine build --platform=windows --config=release --sign

# Run the staged build
uzengine run --platform=windows

# Export for another platform (requires platform toolchain)
uzengine build --platform=android --abi=arm64-v8a
```

---

## 8. VSCode Editor — Build Panel

```
Build Panel
┌──────────────────────────────────────────────────────┐
│  Platform: [▼ Windows x64]     Config: [▼ Release]   │
├──────────────────────────────────────────────────────┤
│  [Cook Assets]  [Pack]  [Build]  [Build & Run]       │
├──────────────────────────────────────────────────────┤
│  Progress:  ████████████░░░░  67%  Cooking textures  │
│                                                      │
│  ✓ Cooked 12/18 textures                             │
│  ✓ Cooked 3/3 meshes                                 │
│  ⏳ Cooking hero_diffuse.png (BC7, 4096×4096)        │
├──────────────────────────────────────────────────────┤
│  Output size: 287 MB (estimated)                     │
│  Last build: 2026-03-01 22:15  (15 min ago)          │
└──────────────────────────────────────────────────────┘
```

### IPC Handlers (build.*)

```
build.cook       { force? }         → stream progress events
build.pack       { output? }        → { ok, size_bytes }
build.compile    { platform, config } → stream compile log
build.getStatus  {}                 → { stage, progress, errors }
build.cancel     {}
build.openStaged { platform }       → open output folder in Explorer
```
