<!-- doc-meta
title: Build and Deployment Guide
category: guide
theme: toolchain
status: active
updated: 2026-03-25
author: docs-operator
-->

# Build and Deployment Guide

Complete step-by-step instructions for building UzEngine from source, including the runtime, N-API bridge, and VSCode extension.

---

## Prerequisites

- **Zig 0.15+** — [Download](https://ziglang.org/download/)
- **Node.js 22+** — [Download](https://nodejs.org/)
- **Visual Studio Build Tools 2022** — C++ workload required
- **CMake 3.21+** — [Download](https://cmake.org/)
- **Git** — for version control

### Verify Installation

```bash
zig version                    # Should be 0.15.0 or later
node --version                 # Should be 22.22.0 or later
npm --version                  # Should be 10.0.0 or later
cmake --version                # Should be 3.21 or later
"C:\Program Files\Git\cmd\git" --version   # Git for Windows
```

---

## Quick Build (Automated)

The easiest way is to use the build scripts, which handle all steps automatically.

### PowerShell (Recommended)

```bash
# Navigate to repo root
cd e:\UzEngine

# Build everything (Zig by default)
scripts/build.ps1

# Or with MSVC compiler (for static lib builds)
scripts/build.ps1 --compiler=msvc

# Or with Clang
scripts/build.ps1 --compiler=clang
```

### CMD

```bash
cd e:\UzEngine
scripts/build.bat
scripts/build.bat --compiler=msvc
```

The script performs:
1. Pre-flight checks (locked files, dependencies)
2. Build runtime (`zig build -Ddll=true`)
3. Build N-API addon (`cmake-js compile`)
4. Deploy artifacts to extension
5. Build extension TypeScript

**Output**:
- `runtime/build/bin/uzengine.exe` — standalone runtime executable
- `extension/native/uzengine.node` — VSCode extension addon
- `extension/native/*.dll` — all runtime dependencies

---

## Manual Build (Step-by-Step)

### Step 1: Build Runtime as DLL

The runtime is built as a **DLL** (not static lib) because Zig's LLVM archive format is incompatible with MSVC's linker (LNK1143 COMDAT error). The DLL approach produces an MSVC-compatible import lib.

```bash
cd runtime
zig build -Ddll=true
```

**Output**:
- `runtime/build/bin/uzengine.exe` — standalone executable
- `runtime/build/bin/uzengine_runtime.dll` — runtime code (41MB)
- `runtime/build/lib/uzengine_runtime.lib` — import lib for MSVC (6KB)
- `runtime/build/bin/*.dll` — dependencies (d3dcompiler_47.dll, dxcompiler.dll, dxil.dll, lua51.dll, etc.)

### Step 2: Build N-API Addon

The N-API addon is compiled with MSVC and links against the runtime DLL import lib.

```bash
cd napi
npm install                  # Install node-addon-api + cmake-js
npx cmake-js compile --runtime=electron --runtime-version=39.6.0
```

**cmake-js arguments**:
- `--runtime=electron` — Target Electron (VSCode uses Electron)
- `--runtime-version=39.6.0` — Match VSCode 1.111.0 (Electron 39.6.0 = Node 22.22.0 = ABI 140)

**Output**:
- `napi/build/Release/uzengine.node` — N-API addon (162KB)
- `napi/build/Release/*.dll` — copies of runtime dependencies

### Step 3: Deploy to Extension

Copy the addon and all DLLs to the extension's native directory:

```bash
# Create native directory
mkdir -p extension/native/D3D12

# Copy N-API addon
cp napi/build/Release/uzengine.node extension/native/

# Copy runtime DLL and dependencies
cp napi/build/Release/*.dll extension/native/
cp napi/build/Release/D3D12/*.dll extension/native/D3D12/

# Verify files are in place
ls -la extension/native/
# Should show: uzengine.node, uzengine_runtime.dll, d3dcompiler_47.dll, etc.
```

### Step 4: Build Extension

Compile TypeScript and finalize the extension package:

```bash
cd extension
npm install                  # Install dependencies (vscode, TypeScript, etc.)
npm run build                # Compile TypeScript
```

**Output**:
- `extension/out/extension.js` — compiled extension
- `extension/native/uzengine.node` — verified in place
- All dependencies copied alongside

---

## Architecture: Why DLL?

```
┌──────────────────────────────────────────────────────────┐
│  UzEngine Runtime (Zig)                                  │
│  ├─ C++ engine core (via C API)                          │
│  └─ Produces uzengine_runtime.dll + import lib           │
└──────────────────────────────────────────────────────────┘
                         │
                         │ MSVC linker consumes .lib
                         ↓
┌──────────────────────────────────────────────────────────┐
│  N-API Bridge (C++, compiled by MSVC)                    │
│  ├─ napi_lifecycle.cpp (init/shutdown)                  │
│  ├─ napi_viewport.cpp (rendering)                       │
│  ├─ napi_scene.cpp (scene I/O)                          │
│  ├─ napi_input.cpp (input forwarding)                   │
│  └─ Produces uzengine.node (N-API addon)                │
└──────────────────────────────────────────────────────────┘
                         │
                         │ require('uzengine.node')
                         ↓
┌──────────────────────────────────────────────────────────┐
│  VSCode Extension (TypeScript)                           │
│  ├─ extension.ts (command registration)                 │
│  ├─ panels/* (UI components)                            │
│  └─ Loads .node addon in-process                        │
└──────────────────────────────────────────────────────────┘
                         │
                         │ Native rendering
                         ↓
┌──────────────────────────────────────────────────────────┐
│  GPU (Direct3D 12 / Vulkan / Metal)                      │
│  ├─ Native HWND viewport (zero readback)                │
│  └─ Frame output directly to screen                      │
└──────────────────────────────────────────────────────────┘
```

**Why DLL instead of static lib?**
- Zig produces libraries in LLVM archive format (`.lib` from `ar` command)
- MSVC's `link.exe` cannot read LLVM archives → LNK1143 COMDAT error
- Solution: Zig builds a **DLL** instead, producing an MSVC-compatible import lib
- Import lib is just a symbol table, small (6KB), MSVC-native format

---

## Clean Build

If you encounter stale artifacts or build errors, perform a clean build:

```bash
# Full clean (all caches)
cd runtime && zig build --clean
cd ../napi && rm -rf build/ node_modules/
cd ../extension && rm -rf out/ node_modules/

# Rebuild from scratch
cd ../runtime && zig build -Ddll=true
cd ../napi && npm install && npx cmake-js compile --runtime=electron --runtime-version=39.6.0
cd ../extension && npm install && npm run build
```

### Known Issues

**Issue**: `napi/build/` is stale, addon doesn't load
```bash
rm -rf napi/build && npx cmake-js compile --runtime=electron --runtime-version=39.6.0
```

**Issue**: `uzengine_runtime.dll` is locked (already loaded by VSCode)
- Close VSCode Extension Host
- Or run **Developer: Reload Window** in VSCode
- Script will auto-detect and error

**Issue**: MSVC compiler not found
- Install Visual Studio Build Tools 2022 with C++ workload
- Ensure `cl.exe` is on PATH: `"C:\Program Files\Microsoft Visual Studio\2022\BuildTools\VC\Tools\bin\Hostx64\x64\cl.exe"`

---

## Testing the Build

### Run Standalone Runtime

```bash
# Load a demo scene
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject

# With editor overlays (grid, stats)
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor

# Headless screenshot
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject \
  --headless \
  --screenshot output.png \
  --screenshot-frame 5 \
- `--screenshot-frames A-B[:step]` (with `--screenshot <base>.png`): captures every frame of the range from the same process as `<base>_<frame>.png` — for temporal analyses (consecutive frames share history, loading state and timing). Added 2026-09-08; the QA scenes `scripts/sponza-flicker-w-seq.*` use it. Since 2026-09-09 the file index is the render index of the frame it contains (matching `region_<N>.bin` of `--region-dump`); captures made before that carry file N = render N+1 because the request was issued after `RenderFrame(N)` and fulfilled by the next frame. `--screenshot-frame K` keeps its original semantics.
- `--region-dump <dir>` with `--regions "name:x,y,w,h;name2:..."` (up to 8 rectangles; the older
  `--region-x/y/w/h` still works for a single one), `--region-fields output|lighting|taa|full` and
  `--region-first F --region-last L`: writes `<dir>/region_<frame>.bin` for every frame in the range
  with per-pixel **linear HDR** records for each rectangle — the SDF reflection radiance and
  roughness, the world normal and view distance (`-1` marks sky/no geometry), and the TAA resolve's
  raw centre sample, plus-filtered colour, reprojected history before and after the clamp, blend
  weight, velocity and output. Format version 2 (`UZR2`): 64-byte header (frame, rect count, field
  mask, records per pixel, flags, render/output resolution, jitter index and offset, exposure), the
  rectangle table, the per-rectangle GPU frame stamps, then the records. The GPU stamps every
  rectangle with the frame it rendered and the readback rejects a file whose stamps disagree, so a
  stale or partial readback can never be analysed as data; frames still in flight are flushed at
  shutdown. Bindless (DX12 SM6.6+) only, three-frame delayed readback, no GPU stall. Read them with
  `scripts/flicker/regiondump.py`; the record layout is documented in
  `runtime/shaders/runtime/common/region_dump.hlsli`. Guard: `scripts/test_region_capture.py`.
- TAA diagnostic CVars (all default to the production behaviour, added 2026-09-09 for
  followup-10): `r.TAA.JitterScale` (0 = accumulation with no sub-pixel offset, which is not the same
  as `r.TAA.JitterSequenceLength=1`), `r.TAA.ReconstructionMode` (1 spatial weights only, 2 centre tap
  only, 3 full 3x3), `r.TAA.ClampMode` (1 none, 2 variance box only, 3 AABB only) and
  `r.TAA.BlendMode` (1 fixed `r.TAA.CurrentFrameWeight`). Guard: `scripts/test_taa_modes.py`.
- GI diagnostic CVars (default to production behaviour, added 2026-09-09 for followup-12):
  `r.GI.Reflections.FreezeSeed` (freezes the per-frame sampling seed of the SDF reflections **and**
  the world gather; -1 = frame count) and `r.GI.RadianceCache.FreezeAfterFrame` (from that absolute
  frame on, the probes stop updating and the converged SH buffer is held; -1 = never). The region
  dump preset `--region-fields components|full` adds the per-pixel lighting components and the
  active-path bitmask. Guard: `scripts/test_lighting_components.py`.
- Later followup-12 diagnostics (default to production behaviour; added 2026-09-10..16, listed here
  2026-10-08): `r.GI.FinalGather.FreezeAfterFrame` (after that absolute frame, i.e. frame index > N,
  the final gather is no longer dispatched, so its output texture holds its last contents while the
  deferred lighting keeps reading it; -1 = off; ledger H-28c), `r.GI.FinalGather.SampleRotation`
  (0 = the shipping radical inverse, 1 = Kronecker candidate, 2 = independent per frame, a reference
  generator only; H-31, H-34, H-36), `r.GI.SDF.SampleFrameOffset` (shifts only the diffuse gather's
  sample sequence by whole frames; H-31) and `r.Translucency.DepthTest` (0 builds the forward
  translucent pipeline without a depth test, which changes the image and is never a shipping
  setting; read when the pipeline is created; H-47). `--component-replay <file>` replays recorded AO
  and specular per frame and per pixel (built by `scripts/make_component_replay.py`; H-28c). The
  region dump carries what the translucent pass consumed in LIGHTING_EXTRA +5..+10 and the TAA
  plus-filter taps and weights in TAA_EXTRA +4..+10 (`region_dump.hlsli`); the gather studies
  (H-30..H-32) read the final gather's own stages and its eight rays through
  `--region-fields gather` (or `gather+extra`). `scripts/test_diagnostic_resources.py` guards the
  replay buffer's release; the CVars have no standing guard, and each study script checks its own
  controls.
- GDF diagnostic CVars (default to production behaviour, added 2026-09-09 for followup-13):
  `r.GI.GDF.FreezeCenter` (stop the clipmaps recentring; only valid while the measured geometry stays
  inside the coverage the field already had) and `r.GI.GDF.CenterPhaseOffset` (an offset in
  centimetres added to the camera position before snapping, so the same camera path crosses
  boundaries at different frames; since ce2caa5e the voxel grid itself does not move,
  `global_distance_field.cpp:204-214`).
  The region dump format is now version 3: a 144-byte camera block (unjittered view-projection, the
  previous frame's, camera position) follows the header, which is what lets an analysis track a world
  point across frames; `--region-fields motion` is the cheap preset for that (48 bytes per pixel).
  Guard: `scripts/test_gdf_controls.py`.
- Correction and material CVars (default to production behaviour, added 2026-09-09 for
  followup-14): `r.TAA.ReconstructionHdrWeightExponent` (1.0 = the production Karis 2014 HDR
  weighting inside the TAA reconstruction filter, 0 = no weighting and bit-identical to
  `r.TAA.ReconstructionMode=1`, values between the two trade recovered bright-feature energy
  against light bleeding into dark neighbours). It is **opt-in**: it failed four of the seven
  preregistered acceptance gates, so the default was not changed - see
  `docs/analysis/flicker-hypothesis-ledger.md` rows H-21..H-24.
  `r.Material.RoughnessOverride` and `r.Material.MetallicOverride` (-1 = off) force a material
  class into the G-buffer, which is the only way to measure a change on mirror or glossy surfaces
  in a scene that contains none. Guards: `scripts/test_correction_gates.py`,
  `scripts/diagnose_cvar_ulp.py`.
- `r.TAA.ClampFreezeAfterFrame` (default -1 = off, added 2026-09-10 for ledger H-25b): from that
  absolute frame on, the TAA resolve reuses the clamp box it captured up to then instead of
  recomputing it, so the bounds stop moving while the reconstruction, exposure and lighting are
  unchanged. Backed by two RGBA16Float textures (`TAA_ClampBox_Min` / `_Max`). It is a **static
  camera diagnostic**, not a candidate correction: screen-space bounds frozen in place are
  meaningless once the camera moves. Measured result: oscillation -3.4 % (below the ROI's decision
  threshold) and error against the correctly resampled image +14.3 %, so it was rejected.
  Guard: `scripts/run_clamp_freeze_study.py`, whose controls verify the freeze applied and the
  reconstruction stayed identical before any number is interpreted.
- Capture reproducibility: the renderer is **not** bit-reproducible across processes. Four captures
  of an identical configuration differ in up to 795 of 786 432 recorded values, by up to 1.6e-2 in
  linear radiance. Aggregates over a 256-frame run reproduce to ~1e-13, so run-to-run comparison of
  statistics is sound, but any assertion of bit-identity must instead compare against a measured
  per-configuration floor (`scripts/diagnose_cvar_ulp.py`). Measure that floor from repeats, not
  from one pair. Since 2026-10-08, `scripts/test_correction_gates.py` captures 4 interleaved repeats
  per configuration and judges an equivalence by max(between) <= max(within) on max |delta|, because
  its single-pair floor passed or failed depending on which pair formed it (followup-14 notes,
  Close-out (2026-10-08)). Processes land in a few discrete states: the eight captures of
  `40-default-vs-explicit` include two bit-identical pairs, and the rest are 9.5e-7 to 3.9e-3 apart
  in 57 to 5 751 of 1.57 M values (the RGB records of the window ROI, frames 237-252). An
  equivalence rule comparing a few processes therefore fails by chance when the states split by
  configuration: on those RGB records, max(between) <= max(within) on max |delta| fails on 16 of 280
  splits at 3 vs 3 processes and 1 of 35 at 4 vs 4 (on per-pixel luminance, which
  `test_correction_gates.py` compares, all 35 splits at 4 vs 4 pass; followup-14 notes). Read a
  failure as a change only if it repeats (`scripts/run_display_study.py`, `indistinguishable()`).
  Closing the render window aborts a capture with exit code 0 after a few frames;
  `runner.run_capture` now raises with that explanation. The engine's `headless` mode only creates a
  device and exits, so captures need a real 1920x1080 window.
- `--run-manifest <path>`: writes the run's manifest as JSON — executable path and hash, project and
  scene, output and render resolution, output encoding, frames rendered, CPU frame-time statistics
  (from `--timing-first N`, default 200), effective exposure, camera pose, region-dump file and
  rejection counts, the CLI arguments and every registered CVar with the subset that differs from
  its default. Use it whenever a capture will be compared with another one.
- Zig built a static `.lib` instead of DLL
- Solution: Use `zig build -Ddll=true` (note the flag)

**Error: `cmake-js: cmake not found`**
- CMake not installed or not on PATH
- Solution: Install [CMake 3.21+](https://cmake.org/) and add to PATH

**Error: `MSVC compiler not found`**
- Visual Studio Build Tools not installed
- Solution: Install from [Visual Studio Installer](https://visualstudio.microsoft.com/downloads/)
- Select "Desktop development with C++"

### Runtime Errors

**Error: `uzengine.node` not found**
- N-API addon not deployed to `extension/native/`
- Solution: Run manual deploy step (Step 3 above)

**Error: `uzengine_runtime.dll` not found**
- Runtime DLL not copied alongside addon
- Solution: Check `extension/native/` contains both `.node` and `.dll`

**Error: `Could not load HWND viewport`**
- Extension failed to create native window
- Check console logs: **View → Output → UzEngine**
- Verify DirectX 12 drivers are up to date

---

## Related Documentation

- [Runtime Architecture](../architecture.md) — system design
- [Extension Architecture](../../extension/README.md) — UI/UX design
- [N-API Bridge](../../napi/README.md) — C/JavaScript boundary
- [Specs 2.0 (N-API Foundation)](../specs/2.0-napi-foundation.md) — detailed design doc

---

*Last updated 2026-03-25*
