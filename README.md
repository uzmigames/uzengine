# UzEngine

[![License](https://img.shields.io/badge/License-Apache_2.0-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-blue.svg)]()
[![Zig](https://img.shields.io/badge/Zig-0.15.2-orange.svg)]()
[![MSVC](https://img.shields.io/badge/MSVC-17.14-blue.svg)]()
[![Clang](https://img.shields.io/badge/Clang-21-green.svg)]()
[![VSCode](https://img.shields.io/badge/VSCode-1.85+-purple.svg)]()
[![Electron](https://img.shields.io/badge/Electron-39.6.0-teal.svg)]()

AAA-quality game engine embedded in VSCode, with open file formats designed for LLM manipulation. **The first engine built for autonomous AI-driven development.**

UzEngine is a C++23 deferred PBR engine with a multi-backend RHI, a LuaJIT scripting layer, and a VSCode extension that hosts the runtime in-process. Every authoring format is plain JSON, every render pass can be inspected from the command line, and an MCP server exposes the codebase to AI agents — so a human or an agent can build, run, inspect, and verify the engine without a GUI.

## Contents

- [Key Features](#key-features)
- [Project Status](#project-status)
- [Rendering Features](#rendering-features)
- [Repository Layout](#repository-layout)
- [Getting Started](#getting-started)
- [Running the Standalone Runtime](#running-the-standalone-runtime)
- [Projects, Scenes and File Formats](#projects-scenes-and-file-formats)
- [Lua Scripting](#lua-scripting)
- [Asset Pipeline Tools](#asset-pipeline-tools)
- [Rendering Diagnostics](#rendering-diagnostics)
- [Architecture](#architecture)
- [Engine Conventions](#engine-conventions)
- [AI-Driven Development](#ai-driven-development)
- [Quality](#quality)
- [Thirdparty Dependencies](#thirdparty-dependencies)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## Key Features

- **AAA Visual Quality** — rendering math and techniques based on published papers and proven engine architectures (clean-room reimplementation, every shader constant cited)
- **Embedded in VSCode** — native N-API addon loaded in-process, no separate process or IPC
- **Open Formats** — projects, scenes, materials, prefabs and VFX are human-readable JSON; meshes, textures, skeletons and animations are cooked to compact binary formats
- **Multi-Backend RHI** — DX12 (SM 5.0 – 6.9, bindless on SM 6.6+), DX11, Vulkan, Metal, plus a `null` backend for device-free runs
- **Multi-Compiler** — Zig, MSVC, and Clang all verified on Windows
- **No GC** — deterministic memory via Rust-inspired ownership and borrow checking in C++
- **Scriptable** — LuaJIT with `on_start` / `on_update` / `on_stop` lifecycle and 20+ `uz.*` API modules
- **Inspectable** — `--diag`, `--pass-dump`, `--vis`, `--pixel-debug` and GPU profiling straight from the CLI, with machine-readable JSON output
- **AI-First** — MCP server integration for autonomous development workflows

## Project Status

UzEngine is **pre-1.0** (runtime `v0.9.x`). APIs and file formats can still change between versions — see [`runtime/CHANGELOG.md`](runtime/CHANGELOG.md).

| Platform | Status |
|----------|--------|
| **Windows 10/11 x64** | Primary target. DX12 (default), DX11, Vulkan. Full toolchain: Zig, MSVC, Clang |
| **macOS** | Bring-up. CMake + Ninja with Apple Clang; the Metal backend renders `triangle_demo`. Shader compilation and the H.264 encoder report "unsupported platform" |
| **Linux** | Platform layer sources only (`runtime/platform/linux/`); no documented bring-up yet |

**Active work:** Lumen-lite world-space GI correction — DDGI with DXR hit lighting, validated against in-engine path-tracer ground truth. Plan: [`docs/analysis/lighting/uzengine/lumen-lite-correction-plan.md`](docs/analysis/lighting/uzengine/lumen-lite-correction-plan.md).

## Rendering Features

| System | Features |
|--------|----------|
| **PBR** | GGX + Burley diffuse + SmithJoint + multiscatter energy compensation, anisotropic GGX, sheen (Estevez & Kulla 2017), 5 metal IOR presets |
| **Shadows** | 4-cascade CSM, PCSS blocker search, ESM, contact shadows, distance field shadows, static shadow cache |
| **Lighting** | Directional + point (64) + spot (16) lights, IBL (SH L2 + env map importance sampling + prefiltered specular + VNDF), clustered shading |
| **Atmosphere** | Bruneton sky, multi-scatter LUT, ozone, aerial perspective, height fog (2-layer + sky injection), volumetric clouds, god rays |
| **GI** | SDF GI (mesh SDFs, global distance field, radiance cache, final gather, SDF reflections), DDGI (Majercik 2019) as the DXR option with hardware hit lighting (per-instance material table, ray-query shadows, sky cubemap miss, multi-bounce), SSGI (5 compute passes: trace, spatial denoise, temporal accum, bilateral upsample, prev color reduce), SVGF denoiser (3 passes), light probe volumes (L2 SH grid); AMD Brixelizer GI is in the tree but dormant (never constructed since 2026-04-10) |
| **Post-Process** | TAA (variance clipping), SSR (stochastic multi-ray), GTAO (multi-bounce + bent normals), bloom (6-mip Karis smooth knee), FilmToneMap ACES, auto-exposure, DOF (near/far/sharp layers), motion blur |
| **Anti-Aliasing** | TAA, SMAA (1x/T2x/S2x/4x), FXAA |
| **Upscaling** | FSR 1.0 (spatial), FSR 2.x (temporal with motion vectors), DLSS |
| **Film Effects** | Film grain, vignette, chromatic aberration, color grading LUT (32x32x32), white balance (Bradford adaptation) |
| **Skeletal Animation** | FBX importer v16 (per-source axis convention: Maya Y-up RH + 3ds Max Z-up RH coexisting), GPU compute skinning (persistent UPLOAD-heap palette staging, 480+ FPS on 120k-vertex characters), TRS bone composition, clip retargeting (`AS_*` clip prefix-strip + alias), animation crossfade |
| **Quality** | 5-tier scalability (Low/Medium/High/Ultra/Cinematic) with per-group controls (shadows, textures, effects, GI, screen, view distance, post-process) |
| **VFX** | GPU compute particle system (65K/emitter), collision, flipbook SubUV, trails, decals, attractor forces, vector fields, sub-emitters, splines, back-to-front sorting |
| **Raytracing** | DXR 1.1 infrastructure (BLAS/TLAS managers, shader table builder, RT pipeline), reference path tracer (ground-truth goldens + lighting decomposition debug modes) with emissive GBuffer + roughness-perturbed mirror specular |
| **Camera** | CameraManager with push/pop stack, editor free-cam (F5), per-camera render targets, multi-camera switching, scriptable 3rd-person follow (Lua) |
| **Scripting** | LuaJIT bindings for scene, lighting, atmosphere, fog, clouds, decals, probes, materials, camera, input, animation, renderer, post-process, CVars, VFX — see [Lua Scripting](#lua-scripting) |
| **Diagnostics** | Pass dump, pass validation, render-target visualization, pixel debug, per-pass GPU profiling, GPU validation, DRED breadcrumbs, FLIP perceptual metric |
| **Backends** | DX12 SM6.8 (bindless), DX12 SM5, DX11, Vulkan, Metal (bring-up) |

## Repository Layout

| Package | Description | Build |
|---------|------------|-------|
| [`runtime/`](runtime/) | Engine core (C++23, ~135 HLSL shaders) + CLI tools + unit tests | Zig / CMake (MSVC, Clang) |
| [`sample/`](sample/) | 31 sample projects with scenes, materials, meshes, VFX and Lua scripts | `.uzproject` / `.uzscene` |
| [`napi/`](napi/) | N-API bridge to Electron | cmake-js / MSVC |
| [`extension/`](extension/) | VSCode extension (viewport, scene hierarchy, engine mode) | TypeScript |
| [`mcp/`](mcp/) | MCP server for AI workflows (hybrid BM25 + HNSW doc search) | TypeScript |
| [`tools/`](tools/) | Standalone authoring tools (material editor) | — |
| [`scripts/`](scripts/) | Setup, build, lint, format, test and validation scripts | PowerShell / Bash / Python |
| [`docs/`](docs/) | Architecture, engine specs, guides, analyses, changelogs | Markdown |

Inside `runtime/`:

```
runtime/
├── engine/        Engine subsystems
│   ├── core, math, memory, containers, job, logging, profiling,
│   │   config, assertions, paths, random, event, property      Foundation
│   ├── rhi/       dx12 · dx11 · vulkan · metal backends            GPU abstraction
│   ├── shader, material, renderer                              Rendering
│   ├── asset, ecs, animation, physics, vfx, scripting, ui      Features
│   └── app, ipc, debug                                         Application layer
├── editor/        Gizmos, grid, outline, selection, toolbar, stats, screenshot
├── platform/      windows · macos · linux
├── shaders/       HLSL (runtime/ + editor/), embedded into the binary by uzembed
├── tools/         uzengine, uzcook, uzpack, uzmesh, uzvalidate, uzvfxinspect, uzembed
├── demos/         triangle, vfx_viewport, vfx_viewport_app
├── tests/         GoogleTest suites (test_runtime.exe)
├── schemas/       JSON schemas for engine formats
└── thirdparty/    Vendored sources and isolated locked native profile
```

## Getting Started

### Prerequisites

- Windows 10/11 x64 with a DX12-capable GPU (DXR-capable for the ray-traced GI and path tracer)
- [Zig 0.15.2](https://ziglang.org/download/0.15.2/release-notes.html) (Windows baseline)
- [Node.js 22+](https://nodejs.org/)
- [Visual Studio Build Tools 2022](https://visualstudio.microsoft.com/downloads/) (C++ workload)
- [CMake 3.21+](https://cmake.org/) (for MSVC/Clang builds)

### Setup

The Windows stabilization profile preserves the vendored sources and provisions
the missing native dependencies from a checked-in lock. Install Python 3.11+,
Visual Studio 2022 C++ Build Tools and Windows SDK **10.0.26100.0** first:

```bash
pwsh -NoProfile -File scripts/setup-native.ps1
cd runtime
zig build '-Dnative-deps=thirdparty/.native' -Ddll=true
```

Archives are SHA-256 checked before extraction. Headers and DLLs use the same
locked package. LuaJIT is rebuilt from the repository's explicit source list.
Use `-VerifyOnly` to verify an existing profile and `-Offline` for a populated
download cache. A changed or incomplete profile fails; choose a new `-Destination`
and pass that runtime-relative directory with `-Dnative-deps`.

This profile excludes optional DLSS/Brixelizer binaries. Existing developer
builds without `-Dnative-deps` keep their configured SDK layout. The older
`scripts/setup.ps1` / `setup.bat` vendor-refresh installer uses moving sources
and is not the reproducible baseline path. See the
[native bootstrap evidence](docs/analysis/project-stabilization/08-native-bootstrap.md).

### Build

All operations go through `scripts/`. The `--compiler` flag selects the toolchain.

**Complete pipeline** (all steps handled automatically):

```bash
# Default (Zig + cmake-js + TypeScript)
scripts/build.ps1

# MSVC (static lib for all components)
scripts/build.ps1 --compiler=msvc

# Clang
scripts/build.ps1 --compiler=clang

# CMD equivalent
scripts/build.bat
scripts/build.bat --compiler=msvc
```

Equivalent npm scripts at the repo root: `npm run build` (release) and `npm run build:debug`.

**Runtime only** (fastest loop for engine work):

```bash
cd runtime
zig build                          # Debug build → runtime/build/bin/
zig build -Doptimize=ReleaseFast   # Release build
zig build -Ddll=true               # Also build uzengine_runtime.dll (VSCode / N-API)
zig build -Deditor=true            # Also build editor.lib (grid, font, stats, screenshot)
zig build test                     # Build and run the unit tests
zig build run                      # Build and launch uzengine.exe (no arguments forwarded)
zig build cook / zig build pack    # Build and launch uzcook / uzpack
```

Output binaries land in `runtime/build/bin/`: `uzengine.exe`, `uzcook.exe`, `uzpack.exe`, `uzmesh.exe`, `uzvalidate.exe`, `uzvfxinspect.exe`, `uzembed.exe`, `test_runtime.exe`.

**Manual step-by-step** (for extension development):

```bash
# 1. Build runtime as DLL + static lib
cd runtime && zig build -Ddll=true

# 2. Compile N-API addon (MSVC links DLL import lib)
cd napi && npx cmake-js compile --runtime=electron --runtime-version=39.6.0

# 3. Deploy addon + DLLs to extension
cp napi/build/Release/uzengine.node extension/native/
cp napi/build/Release/*.dll extension/native/
mkdir -p extension/native/D3D12
cp napi/build/Release/D3D12/*.dll extension/native/D3D12/

# 4. Build extension TypeScript
cd extension && npx tsc
```

> The build scripts check for locked files (e.g. `.node` loaded by VSCode) before building.
> If a lock is detected, close the Extension Host or run **Developer: Reload Window** first.

### Run

**Inside VSCode** — press **F5** to launch the Extension Development Host, then use the command palette:

| Command | Action |
|---------|--------|
| `UzEngine: Start` | Start the embedded runtime |
| `UzEngine: Open Viewport` | Open the render viewport |
| `UzEngine: Open Scene` | Load a `.uzscene` |
| `UzEngine: Toggle Engine Mode` | Switch between editor and game mode |
| `UzEngine: Refresh Scene Tree` | Refresh the Scene Hierarchy view |

**Standalone** — run any sample project directly:

```bash
runtime/build/bin/uzengine.exe --project sample/projects/sponza.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/damaged_helmet.uzproject --editor
```

See [`sample/README.md`](sample/README.md) for the full list of sample projects.

### Test

```bash
scripts/tools/test.ps1                         # Zig (default)
scripts/tools/test.ps1 --compiler=msvc         # MSVC
scripts/tools/test.ps1 --compiler=clang        # Clang
scripts/tools/test.ps1 --coverage              # With coverage (TS modules)
```

## Running the Standalone Runtime

`uzengine.exe` accepts `--key value` or `--key=value`. A flag with no value is a boolean `true`.

> Values that start with `-` must use the `=` form, otherwise they are read as a new flag:
> `--camera-pos=-120,40,180`.

### Project and window

| Flag | Description |
|------|-------------|
| `--project <file.uzproject>` | Project to load (content root, default scene, window, CVars) |
| `--scene <file.uzscene>` | Scene to load when no `--project` is given |
| `--editor` | Editor mode: overlays (grid, stats, gizmos) and dormant gameplay scripts |
| `--width <px>` / `--height <px>` | Window size (default 1280×720, or the project's `window` block) |
| `--frames <N>` | Exit after N frames (default: run until closed) |
| `--hidden` | Render normally with an invisible window — automated captures don't steal focus |
| `--headless` | Device smoke test only: creates the RHI device, logs, and exits (renders nothing) |
| `--camera-pos x,y,z` / `--camera-look x,y,z` | Override the camera pose (combine with `--editor` for deterministic captures) |

### Rendering backend

| Flag | Description |
|------|-------------|
| `--rhi dx12\|dx11\|vulkan\|metal\|null` | Backend (default: DX12 on Windows, Metal on macOS, Vulkan elsewhere) |
| `--sm <5.0 … 6.9>` | Shader model (also `--shader-model`) |
| `--adapter <index>` | GPU adapter index |
| `--cvar name=value` | Override any CVar at startup (repeatable; CLI wins over the project's `cvars`) |

### Logging and capture

| Flag | Description |
|------|-------------|
| `--verbose` | Info-level logging (the default level is Error) |
| `--log-level trace\|debug\|info\|warning\|error` | Explicit log level |
| `--screenshot <path.png>` | Capture a screenshot (use an absolute path) |
| `--screenshot-frame <N>` | Frame at which the screenshot is taken |
| `--run-manifest <path.json>` | Write executable identity, effective CVars, scene and run stats |

Runtime logs are written to `%LOCALAPPDATA%/UzEngine/logs/uzengine_*.log`.

### Controls

| Key | Action |
|-----|--------|
| WASD | Move camera (XY plane) |
| E / Q | Move up / down (Z axis) |
| Right-click + Mouse | Look around |
| Scroll wheel | Adjust camera speed |
| F1 / F2 | Deferred / Forward render path |
| F5 | Toggle editor free-cam |
| G | Toggle game view (editor) |

### CVars

Render features are controlled by console variables (`r.*`). Set them per project in the `cvars` block of a `.uzproject`, at startup with `--cvar`, or at runtime from Lua with `uz.cvar.set`.

```bash
uzengine.exe --project sample/projects/sponza.uzproject --cvar r.TAA=1 --cvar r.SSGI=1 --cvar quality=3
```

`quality` selects the scalability preset (0 = Low, 1 = Medium, 2 = High, 3 = Ultra, 4 = Cinematic). Full list: [`docs/guides/cvar-reference.md`](docs/guides/cvar-reference.md).

## Projects, Scenes and File Formats

A **project** (`.uzproject`) points to a content root and a default scene, and carries window settings and CVar defaults:

```json
{
  "name": "Sponza Atrium",
  "version": "1.0",
  "content_root": "../content",
  "default_scene": "scenes/sponza.uzscene",
  "window": { "title": "UzEngine — Sponza Atrium", "width": 1920, "height": 1080 },
  "cvars": { "r.RadianceCache": 1, "r.TAA": 1 }
}
```

A **scene** (`.uzscene`) is a list of entities with components. Paths are relative to the content root:

```json
{
  "name": "Minimal Test",
  "version": "1.0",
  "entities": [
    { "name": "Sun",
      "components": {
        "transform": { "position": [0, 0, 920], "rotation": [-30, 0, 60] },
        "light": { "type": "directional", "color": [1.0, 0.96, 0.88], "intensity": 10 } } },
    { "name": "Cube_A",
      "components": {
        "transform": { "position": [0, 0, 50], "scale": [80, 80, 80] },
        "mesh": { "asset": "meshes/cube.uzmesh" },
        "material": { "albedo": "textures/checker.png" } } },
    { "name": "MainCamera",
      "components": {
        "transform": { "position": [0, -350, 120], "rotation": [0, 0, -8] },
        "camera": { "fov": 55, "near": 1, "far": 100000, "active": true } } },
    { "name": "SkyAtmosphere",
      "components": { "transform": { "position": [0, 0, 1020] }, "sky_atmosphere": {} } },
    { "name": "Controller", "components": { "script": { "path": "scripts/fly_camera.lua" } } }
  ]
}
```

### Format reference

| Extension | Encoding | Content |
|-----------|----------|---------|
| `.uzproject` | JSON | Project: content root, default scene, window, CVars |
| `.uzscene` | JSON | Scene: entities and components |
| `.uzprefab` | JSON | Reusable entity hierarchy |
| `.uzmat` / `.uzmaterial` | JSON | Material graph / material parameters |
| `.uzvfx` / `.uzvfxsys` / `.uzfx` | JSON | VFX emitter / VFX system / effect assets |
| `.uzimport` | JSON | Import sidecar (importer, settings, dependencies) |
| `.uzmesh` | Binary | Cooked mesh: vertices, indices, tangents, AABB, LODs |
| `.uztexture` | Binary | Cooked texture: BC1/BC5/BC7 with prebuilt mips |
| `.uzskel` / `.uzanim` | Binary | Skeleton / animation clip |
| `.uzsdf` | Binary | Baked mesh signed distance field |
| `.uzshader` | Binary | Compiled shader |
| `.uzpak` / `.uzpakindex` / `.uzmanifest` | Binary / index | Packed asset archives for shipping |

When a cooked `.uztexture` exists next to a source image, the runtime loads it automatically instead of decoding the PNG.

## Lua Scripting

Attach a script to an entity with a `script` component. The runtime calls three lifecycle hooks:

```lua
-- scripts/hello.lua
function on_start(entity)
    uz.log("Hello from Lua! Entity loaded.")
end

function on_update(entity, dt)
    local rx, ry, rz = uz.scene.get_rotation(entity)
    uz.scene.set_rotation(entity, rx, ry + 45 * dt, rz)
end

function on_stop(entity)
    uz.log("Goodbye from Lua!")
end
```

| Module | Purpose |
|--------|---------|
| `uz.scene` | Entities, transforms, lights, sky atmosphere, sky light, height fog, clouds, decals, reflection probes, materials, scene load/save |
| `uz.math` | Vectors and quaternions (`quat_from_euler`, `quat_from_axis_angle`, …) |
| `uz.camera` | Position, look-at, FOV, orthographic |
| `uz.input` | Keyboard and mouse |
| `uz.time` | Time scale, pause, fixed/raw delta, frame count (`uz.delta_time`, `uz.total_time`, `uz.frame_index`) |
| `uz.animation` | Clip playback and crossfade |
| `uz.renderer` / `uz.viewport` / `uz.debug` | Render path, resolution, stats, view modes, show flags, quality |
| `uz.postprocess` | Post-process volumes, overrides and blending |
| `uz.cvar` | `get`, `set`, `list`, `info`, `reset`, `reset_all` |
| `uz.pass_inspector` | List targets, dump, compare, timing, pass toggles |
| `uz.mesh` / `uz.texture` | Asset loading |
| `uz.vfs` | Virtual file system reads (loose files or `.uzpak`) |
| `uz.vfx` | Particle systems, trails, decals |
| `uz.window` / `uz.editor` | Window info / editor integration |
| `uz.log`, `uz.log_warn`, `uz.log_error`, `uz.quit` | Logging and exit |

Guides: [`docs/user/guides/lua-scripting.md`](docs/user/guides/lua-scripting.md), [`docs/guides/lua-vfx-api-reference.md`](docs/guides/lua-vfx-api-reference.md). Examples: [`sample/content/scripts/`](sample/content/scripts/).

## Asset Pipeline Tools

| Tool | Purpose | Usage |
|------|---------|-------|
| `uzmesh` | Any model → `.uzmesh` (FBX, glTF/GLB, OBJ, STL): axis conversion to Z-up, dedup, winding fix, tangents, AABB, auto-LOD | `uzmesh --input model.glb --output model.uzmesh` |
| `uzcook` | Batch-cook a content directory (shaders, textures → BC), write `.uzimport` sidecars, optionally pack | `uzcook --source <dir> --output <dir> [--pak <name>] [--textures-only] [--no-compress]` |
| `uzpack` | Pack assets into `.uzpak` archives + `.uzpakindex` (LZ4, optional scramble) | `uzpack --input <dir> --output <dir> --name <base> [--max-pak-mb 500]` |
| `uzvalidate` | Validate `.uzvfx` / `.uzvfxsys` assets; JUnit XML for CI | `uzvalidate <content-dir> [--junit <out>]` |
| `uzvfxinspect` | Inspect `.uzvfxsnap` particle snapshots | `uzvfxinspect <file> [--json \| --summary \| --channel <id>]` |
| `uzembed` | Embed HLSL into C++ headers at build time (runs automatically) | `uzembed --input-dir <dir> --output-dir <dir>` |

Details: [`docs/guides/texture-pipeline.md`](docs/guides/texture-pipeline.md), [`docs/runtime/asset-loading.md`](docs/runtime/asset-loading.md), [`docs/guides/scene-load-optimization.md`](docs/guides/scene-load-optimization.md).

## Rendering Diagnostics

Rendering bugs are investigated with built-in tooling before any shader is edited:

```bash
# 1. Health check of every render target → DIAG_JSON:{...} on stdout
uzengine.exe --project sample/projects/sponza.uzproject --diag

# 2. Visualize one target (GBuffer channel or any registered pass target)
uzengine.exe --project ... --hidden --vis ssgi.trace --screenshot E:/out/ssgi.png --frames 60
uzengine.exe --project ... --hidden --view-mode albedo --screenshot E:/out/albedo.png --frames 60

# 3. Dump every pass output as PNG + INDEX.html → pass-dump-f<N>/
uzengine.exe --project ... --pass-dump --frames 120

# 4. Read exact GBuffer/HDR values at one pixel → PIXEL_DEBUG_JSON:{...}
uzengine.exe --project ... --pixel-debug --pixel-debug-x 960 --pixel-debug-y 540
```

| Flag | Purpose |
|------|---------|
| `--diag` | Validate all targets (NaN / Inf / all-zero / skipped) and print a JSON report |
| `--vis <target>` | Fullscreen visualization of a named render target |
| `--view-mode <mode>` | `lit`, `albedo`, `worldnormal`, `metallic`, `roughness`, `ao`, `depth`, `worldposition`, `lighting`, `lightingonly`, `detaillighting`, `shadows`, `reflections`, `wireframe`, `litwireframe`, `unlit`, `tangent`, `faceted`, `overview` |
| `--pass-dump` | Dump all pass targets to PNG with an HTML index |
| `--pixel-debug` (+ `-x`, `-y`) | GPU printf of depth, AO, albedo, HDR, roughness, metallic, emissive at a pixel |
| `--gpu-profile` | Per-pass GPU timestamps |
| `--gpu-validation` | D3D12 debug layer + GPU-based validation |
| `--gpu-breadcrumbs` | DRED breadcrumbs for device-removed crashes |
| `--shadow-debug <N>` | Shadow debug modes |

Demo applications under `runtime/demos/` also accept `--screenshot-stdout` (base64 PNG on stdout), `--pass-validate`, `--pass-toggle <pass>`, `--pass-compare <pass>` and `--all-modes <file.png>`.

## Architecture

### DLL-Based N-API Bridge

UzEngine uses a **DLL-based architecture** for ABI compatibility with MSVC:

```
Runtime (Zig)  ──→  uzengine_runtime.dll (41MB)
                     + uzengine_runtime.lib (6KB, MSVC-compatible import lib)
                         │
                         └──→  napi/src/*.cpp (MSVC)
                              + uzengine.node (N-API addon, 162KB)
                                  │
                                  └──→  extension/src/extension.ts (VSCode extension)
```

**Why DLL?** Zig's static library format (LLVM archive) is incompatible with MSVC's `link.exe` (LNK1143 COMDAT error). By building the runtime as a DLL, we produce an MSVC-compatible import lib that cmake-js can link against.

**Benefits**:
- N-API addon compiled with MSVC (zero ABI surprises)
- Zig runtime + MSVC N-API bridge + TypeScript extension all work together
- Multi-compiler support (Zig produces the DLL, MSVC compiles the bridge, tsc compiles the UI)

### Build Pipeline

```
scripts/build.ps1 [--compiler=zig|msvc|clang]
  │
  ├─ 0. Pre-flight        check for locked .node / .lib files
  ├─ 1. build:runtime     zig build -Ddll=true
  │                       → uzengine_runtime.dll + import lib
  ├─ 2. build:napi        cmake-js compile
  │                       → napi/build/Release/uzengine.node
  │                       → napi/build/Release/*.dll (copies)
  │     └─ copy:artifacts → extension/native/
  ├─ 3. build:mcp         tsc → mcp/dist/
  └─ 4. build:extension   tsc → extension/out/extension.js
```

### Runtime Layers

Dependencies flow strictly downward — no cycles:

```
Application   app · ipc · debug · editor
Features      asset · ecs · animation · physics · vfx · scripting · ui
Rendering     renderer (render graph + passes) · material · shader
GPU           rhi  (dx12 · dx11 · vulkan · metal · null)
Foundation    core · math · memory · containers · job · logging · profiling · config
Platform      windows · macos · linux
```

The runtime is **path-agnostic**: no filesystem path is hardcoded in `runtime/`. Every path enters through CLI arguments, project files, asset manifests or explicit API calls.

More: [`docs/architecture.md`](docs/architecture.md), [`docs/architecture/`](docs/architecture/), [`docs/engine/`](docs/engine/).

## Engine Conventions

| Category | Convention |
|----------|-----------|
| Coordinate system | Z-up, X-forward, left-handed |
| Distances | Centimeters |
| Angles | Degrees in components/scenes, radians on the GPU |
| Atmosphere | Kilometers |
| Color | Linear RGB |
| Temperature | Kelvin |
| Time | Seconds |

Importers convert source assets on the way in — for example glTF (Y-up, right-handed) maps to `Engine.X = -glTF.Z`, `Engine.Y = glTF.X`, `Engine.Z = glTF.Y`, with triangle winding reversed.

## AI-Driven Development

The repository is set up so AI agents can work on it end to end:

- **`uzengine-docs` MCP server** ([`mcp/`](mcp/)) — hybrid BM25 + HNSW search over the codebase and docs. Tools: `search_docs`, `get_entity`, `get_relationships`, `get_data_flow` (trace a field from component → constant buffer → shader), `get_subsystem_map`, `get_refs` (academic references index).
- **Rulebook** — task management, persistent memory and knowledge base via the `rulebook` MCP server. Rules live in [`AGENTS.md`](AGENTS.md) and [`AGENTS.override.md`](AGENTS.override.md); tasks in `.rulebook/tasks/`.
- **Machine-readable diagnostics** — `DIAG_JSON:` / `PIXEL_DEBUG_JSON:` stdout prefixes, run manifests and PNG pass dumps make visual verification scriptable.
- **Reference-driven rendering** — shaders cite their source papers (`/// @ref Author Year "Title"`) and new GI work is validated against path-tracer ground truth by PSNR.

Both servers are registered in [`.mcp.json`](.mcp.json).

## Quality

| Tool | Runtime | N-API | Extension | MCP |
|------|---------|-------|-----------|-----|
| **Compiler** | Zig / MSVC / Clang | MSVC | tsc | tsc |
| **Lint** | clang-tidy | — | ESLint 9 | ESLint 9 |
| **Format** | clang-format | clang-format | Prettier 3.8 | Prettier 3.8 |
| **Test** | GoogleTest 1.15 | — | Vitest 4 | Vitest 4 |
| **CI** | `runtime.yml` | — | `extension.yml` | `mcp.yml` |

```bash
scripts/tools/lint.ps1             # clang-tidy + eslint
scripts/tools/format.ps1           # clang-format + prettier
scripts/tools/format.ps1 --check   # CI mode (check only)
scripts/tools/test.ps1                   # zig test + vitest
scripts/tools/test.ps1 --compiler=msvc   # MSVC tests
```

- **Zero warnings** — all warnings promoted to errors across every module
- **95% coverage** — enforced via Vitest thresholds on branches, functions, lines, and statements
- **Structured logging** — `LogOutputChannel` (extension), `pino` (mcp), `spdlog` behind `UZ_LOG` (runtime)
- **Visual verification** — rendering changes are verified with screenshots at multiple camera poses, not only unit tests

## Thirdparty Dependencies

Thirdparty libraries live under `runtime/thirdparty/`. The Windows baseline uses
[`scripts/setup-native.ps1`](scripts/setup-native.ps1) and its checked-in lock;
[`scripts/setup.ps1`](scripts/setup.ps1) is the legacy vendor-refresh installer.
See [`runtime/thirdparty/README.md`](runtime/thirdparty/README.md) for the library inventory.

## Documentation

| Where | What |
|-------|------|
| [Architecture](docs/architecture.md) | System design, layers, execution model |
| [Engine specs](docs/engine/) | Per-subsystem design (core, RHI, rendering, scripting, VFX, …) |
| [User docs](docs/user/) | Getting started, guides and API reference |
| [Guides](docs/guides/) | Implementation guides (build, shaders, materials, GI, shadows, VFX, Lua, CVars, …) |
| [Analyses](docs/analysis/) | Investigations and correction plans |
| [Shaders](docs/shaders.md) / [References](docs/references.md) | Shader catalog / cited papers |
| [Roadmap](docs/roadmap.md) / [DAG](docs/dag.md) / [PDR](docs/pdr.md) | Phases and milestones, dependency graph, project design record |
| [Specs](docs/specs/) | Per-milestone specifications |
| [Changelog](runtime/CHANGELOG.md) | Detailed per-version changelog (split files in [`docs/changelog/`](docs/changelog/)) |

## Contributing

Read [`CONTRIBUTING.md`](CONTRIBUTING.md) and the [Code of Conduct](CODE_OF_CONDUCT.md). Commits follow Conventional Commits (`feat`, `fix`, `docs`, `refactor`, `perf`, `test`, `build`, `ci`, `chore`) and must pass type-check, lint, tests and coverage gates.

## License

[Apache License 2.0](LICENSE) — Copyright 2026 Uzmi Games
