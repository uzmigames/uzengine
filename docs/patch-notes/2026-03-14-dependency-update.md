# Patch Notes — 2026-03-14 — Dependency Update + Gap Analysis

## Dependency Updates

15 thirdparty libraries updated to latest stable versions.

| Library | Before | After | Impact |
|---------|--------|-------|--------|
| directx_headers | v1.614.1 | v1.619.1 | SM6.9 header support |
| d3d12ma | v2.0.1 | **v3.1.0** | Breaking: new API (GetResourceAllocationInfo signature changed). Our usage (CreateAllocator, ALLOCATION_DESC) is compatible. |
| spdlog | v1.15.3 | v1.17.0 | Bumped fmt to 12 |
| mimalloc | v2.1.7 | v2.2.4 | Performance improvements |
| directxmath | oct2024 | apr2025 | Bug fixes |
| glm | 1.0.1 | 1.0.3 | Bug fixes |
| nlohmann/json | v3.11.3 | v3.12.0 | New features |
| googletest | v1.15.2 | v1.17.0 | Requires C++17 (already using) |
| libjpeg-turbo | 3.0.3 | 3.1.3 | SIMD improvements |
| vulkan_headers | sdk-1.4.309 | v1.4.342 | Vulkan 1.4.342 spec |
| volk | sdk-1.4.309 | sdk-1.4.304.1 | Latest tagged release |
| dxc | v1.8.2407 | v1.9.2602 | SM6.9, new optimizations |
| agility | 1.614.1 | 1.619.1 | DX12 runtime update |
| xxhash | v0.8.2 | v0.8.3 | Bug fixes |
| spirv-reflect | sdk-1.3.268 | sdk-1.4.309 | Vulkan 1.4 support |

### Not Updated (intentional)

| Library | Current | Latest | Reason |
|---------|---------|--------|--------|
| jemalloc | 5.3.0 | 5.3.0 | Already latest |
| luajit | 2.1 git | 2.1 git | Rolling release, up to date |
| stb | master | master | Rolling release, up to date |
| tracy | 0.13.1 | 0.13.1 | Already latest |
| tinyexr | v1.0.9 | v1.0.9 | Stable, no breaking changes needed |

## Bug Fixes

- **miniz.c/miniz.h restored for tinyexr** — `setup.ps1` was deleting miniz files needed by tinyexr for EXR decompression. Added explicit download from tinyexr v1.0.9 deps with `MINIZ_EXPORT` stub.
- **lua_vm.cpp include fix** — Changed `#include <engine/core/uz_log.h>` (non-existent) to `#include "log.h"`. Fixed `UZ_DEFINE_LOG_CATEGORY(Scripting)` → `UZ_DEFINE_LOG_CATEGORY(Scripting, Info)`. Fixed `Warn` → `Warning` log level.
- **Added `UZ_DECLARE_LOG_CATEGORY(Scripting)`** in `log.h` — was missing, causing undeclared identifier errors.
- **Tracy patch path fix** — `git apply --directory=` was duplicating the path. Removed `--directory` since patch files already contain full paths from repo root.
- **Agility SDK NuGet URL** — NuGet v2 API returns 404; switched to `globalcdn.nuget.org` direct URL.

## New Features

- **Gap Analysis Report** — Deep comparison of UzEngine vs UE5 Basic Scene. Identified 14 missing rendering systems. Report at `docs/analysis/ue5-basic-scene-gap-analysis-2026-03-14.md`.
- **7 new Rulebook tasks** created for identified gaps:
  - `phase3a-3.35.0-exponential-height-fog` (P1)
  - `phase3a-3.36.0-depth-prepass-hiz-pyramid` (P2)
  - `phase3a-3.37.0-reflection-probes` (P2)
  - `phase3a-3.38.0-lod-system` (P2)
  - `phase3a-3.39.0-gpu-instancing` (P2)
  - `phase3a-3.40.0-volumetric-clouds` (P3)
  - `phase3a-3.41.0-architecture-risk-fixes` (P1)
- **`/gap-analysis` skill** — Reusable skill for future deep gap analyses.
- **Godot rendering analysis** — 5 new docs in `docs/analysis/` covering Godot 4's forward clustering, architecture decisions, and UzEngine comparison.
- **UE5 default scene analysis** — 3200+ line analysis of all 17 UE5 rendering systems in a basic scene.

## Build / Infra

- **CLAUDE.md rule added** — "NEVER Manually Clone/Download Thirdparty Libraries". All thirdparty must go through `scripts/setup.ps1`.
- **setup.ps1 improvements** — Fixed patch application, added miniz auto-download for tinyexr, fixed NuGet URL for Agility SDK.
- **LuaJIT entry added** to `runtime/thirdparty/README.md`.

## Breaking Changes

- **D3D12MA v2 → v3** — Internal API changed (GetResourceAllocationInfo). Our public-facing usage (CreateAllocator, ALLOCATION_DESC, Allocation*) is binary-compatible. No engine code changes needed.
