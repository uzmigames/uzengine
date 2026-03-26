<!-- doc-meta
title: Data Flow — Reflection Probe Cubemap Capture
category: analysis/dataflow
created: 2026-03-24
-->

# Data Flow: Reflection Probe Cubemap Capture

## Overview

Renders the scene from each reflection probe's world position into a 6-face
RGBA16F cubemap, then feeds that cubemap to IBLBaker for irradiance and
prefiltered specular generation. Replaces the current sky-only source with
actual scene geometry reflections.

## Reference

@ref Lagarde 2012 "Local Image-based Lighting With Parallax-corrected Cubemaps"
@ref Akenine-Moller 2018 "Real-Time Rendering 4th Ed" Chapter 10.5 — Environment mapping

Standard cubemap capture technique:
- 6 render passes from the probe center, each with a 90-degree FOV, 1:1 aspect ratio
- Face ordering: +X, -X, +Y, -Y, +Z, -Z (DX convention)
- UzEngine uses Z-up left-handed coordinates, so face directions must be swapped
  through the same coordinate swap matrix the main camera uses

## Data Flow Stages

| Stage | File | Function/Type | What It Does |
|-------|------|---------------|--------------|
| 1. ECS Component | `reflection_probe_component.h` | `ReflectionProbeComponent` | Stores probe config: position (via SceneComponent), resolution, needsCapture flag |
| 2. Probe Renderer | `reflection_probe_renderer.h/.cpp` | `ReflectionProbeRenderer::Update()` | Gathers dirty probes, triggers capture (one per frame) |
| 3. Cubemap Texture | `reflection_probe_renderer.cpp` | `ProbeSlot::captureCubemap` (NEW) | Per-probe RGBA16F Texture2DArray (6 slices) at probe resolution |
| 4. Face Camera Setup | `reflection_probe_renderer.cpp` | `BuildCubemapFaceViewProj()` (NEW) | 6 view-projection matrices from probe world position, 90deg FOV, Z-up swap |
| 5. Scene Capture Pass | `reflection_probe_renderer.cpp` | `CaptureProbeScene()` (NEW) | Renders GBuffer+lighting into each cubemap face via per-face render passes |
| 6. IBL Bake | `ibl_baker.h/.cpp` | `IBLBaker::BakeEnvironment()` | Takes captured cubemap SRV, produces irradiance + prefiltered specular |
| 7. SRV Writeback | `reflection_probe_renderer.cpp` | `Update()` | Stores baked SRV indices on ProbeSlot and ECS component |
| 8. GPU Upload | `reflection_probe_renderer.cpp` | `UploadProbeData()` | Writes ReflectionProbeGPU to StructuredBuffer for deferred_lighting_ps.hlsl |

## UzEngine File Mapping

| Stage | File | Status |
|-------|------|--------|
| 1. Component | `runtime/engine/ecs/environment/reflection_probe_component.h` | EXISTS — has resolution, needsCapture, influenceRadius, priority |
| 2. Probe Renderer | `runtime/engine/renderer/probes/reflection_probe_renderer.h/.cpp` | EXISTS — needs cubemap capture added |
| 3. Cubemap RTs | `reflection_probe_renderer.h/.cpp` | NEEDS: per-probe RGBA16F cubemap + depth buffer |
| 4. Camera Matrices | `reflection_probe_renderer.cpp` | NEEDS: BuildCubemapFaceViewProj() helper |
| 5. Capture Pass | `reflection_probe_renderer.cpp` | NEEDS: CaptureProbeScene() — render simplified scene |
| 6. IBL Bake | `runtime/engine/renderer/ibl/ibl_baker.h/.cpp` | EXISTS — BakeEnvironment() already accepts any cubemap SRV |
| 7. SRV Writeback | `reflection_probe_renderer.cpp` | EXISTS — already writes baked SRV indices |
| 8. GPU Upload | `reflection_probe_renderer.cpp` | EXISTS — UploadProbeData() already works |

## Cubemap Face Camera Setup (Z-up Left-Handed)

UzEngine coordinate system: +X forward, +Y right, +Z up (left-handed).
DirectXMath operates in Y-up. The main camera applies a swap matrix:
Z-up(x,y,z) -> Y-up(y,z,x).

For cubemap capture, we need 6 cameras each looking at 90-degree axes.
The standard DX cubemap face ordering (for Texture2DArray/TextureCube) is:

| Face | DX Convention (Y-up) | UzEngine Z-up Forward | UzEngine Z-up Up |
|------|---------------------|-----------------------|------------------|
| 0 (+X) | +X, up=+Y | +Y (right) | +Z (up) |
| 1 (-X) | -X, up=+Y | -Y (left) | +Z (up) |
| 2 (+Y) | +Y, up=-Z | +Z (up) | -X (back) |
| 3 (-Y) | -Y, up=+Z | -Z (down) | +X (forward) |
| 4 (+Z) | +Z, up=+Y | +X (forward) | +Z (up) |
| 5 (-Z) | -Z, up=+Y | -X (back) | +Z (up) |

Each face uses a 90-degree FOV perspective projection (XM_PIDIV2), aspect 1:1,
with the same near/far planes as the scene camera (10cm-100000cm).

The view matrix for each face applies the same Z-up to Y-up swap as the main
camera, but with different forward/up vectors per face.

## Depth Buffer Strategy

Each probe needs a depth buffer for correct occlusion during capture.
One shared D32Float depth texture at probe resolution is sufficient since
faces are rendered sequentially (cleared per face).

## Capture Pipeline

The capture uses a **simplified forward pass**, not the full deferred pipeline.
This is a deliberate trade-off:
- Full deferred (GBuffer + lighting) would be correct but expensive
- Forward PBR with direct lighting only is sufficient for reflections
- This matches the reference implementation approach (SceneCaptureComponent2D
  often uses simplified rendering)

For Phase 1 (this implementation), we use the existing sky cubemap + direct
lighting approach via the IBLBaker. The scene capture writes a single RGBA16F
color target per face using a forward PBR pass:
1. Clear color RT + depth for this face
2. Set viewport to face resolution
3. Set view-projection from face camera
4. Draw all opaque meshes with forward PBR pipeline
5. Draw sky background
6. After all 6 faces: transition cubemap to SRV, feed to IBLBaker

## Resource Budget (per probe)

- Cubemap: 128x128x6 RGBA16F = 128*128*6*8 = 6MB (full chain)
- Depth: 128x128 D32F = 64KB (shared, reused per face)
- IBL irradiance: 32x32x6 RGBA16F = 48KB (created by IBLBaker)
- IBL prefiltered: 256x256x6 RGBA16F + 5 mip levels ~= 8MB (created by IBLBaker)
- Total per probe: ~14MB
- Max 8 probes: ~112MB VRAM

## Changes Required (in dependency order)

1. **`reflection_probe_renderer.h`** — Add to ProbeSlot: `captureCubemap` (RHITextureHandle),
   `captureDepth` (RHITextureHandle), `captureCubemapSrv` (uint32_t). Add private methods:
   `CreateCaptureResources()`, `DestroyCaptureResources()`, `CaptureProbeScene()`. Add
   new Init parameter or store pipeline/mesh references needed for forward rendering.

2. **`reflection_probe_renderer.cpp`** — Implement:
   - `BuildCubemapFaceViewProj()` — static helper returning 6 Mat4 view-projection matrices
   - `CreateCaptureResources()` — allocates RGBA16F cubemap + D32F depth at probe resolution
   - `DestroyCaptureResources()` — cleans up on Shutdown or probe deactivation
   - `CaptureProbeScene()` — renders 6 faces using forward PBR pipeline
   - Modify `Update()` to call CaptureProbeScene() before IBLBaker when a probe is dirty

3. **No shader changes needed** — the forward PBR pipeline and IBLBaker shaders already exist.

4. **No constant buffer changes needed** — ReflectionProbeGPU and PerFrameCB already have
   all required fields.

## Phase 1 Simplifications

- **No scene capture in Phase 1**: The forward PBR pipeline integration requires
  access to the DrawList, mesh data, material data, and pipeline objects that are
  owned by SceneRenderer. ReflectionProbeRenderer currently only has access to
  IRHIDevice and IBLBaker.

  **Decision**: Phase 1 adds the cubemap capture infrastructure (texture creation,
  face camera matrices, render pass execution) but still uses the sky cubemap as
  the IBL source. The actual scene rendering into the cubemap requires either:
  (a) passing the DrawList and pipeline references into ReflectionProbeRenderer, or
  (b) having SceneRenderer orchestrate the capture passes.

  We choose **(b)**: add a `CaptureProbe()` method to ReflectionProbeRenderer that
  accepts an externally-rendered cubemap texture. SceneRenderer will render the scene
  into the probe's cubemap during `SetupFrameData()`, then pass it to the probe renderer
  for IBL baking. This keeps ownership clean.

## Implementation Plan (decomposed sub-tasks)

### Sub-task A: Camera matrix helper (1 file: reflection_probe_renderer.cpp)
- Add free function `BuildCubemapFaceViewProj()` returning array of 6 Mat4
- Uses same Z-up to Y-up swap as Camera::UpdateView()
- 90deg FOV, 1:1 aspect, near=10cm, far=100000cm
- Unit testable in isolation

### Sub-task B: Capture resources on ProbeSlot (2 files: .h + .cpp)
- Add `captureCubemap`, `captureDepth`, `captureCubemapSrv` to ProbeSlot
- Add `CreateCaptureResources(uint32_t resolution)` private method
- Add `DestroyCaptureResources(uint32_t slotIndex)` private method
- Call create in Update() when a new probe is first seen
- Call destroy in Shutdown() and when probe is deactivated

### Sub-task C: Scene capture pass integration (2 files: .h + .cpp)
- Add `CaptureProbeScene()` method that renders into the cubemap
- This method receives an IRHICommandList and DrawList/pipeline references
- Renders 6 faces: clear RT+depth, set face viewproj, draw meshes, draw sky
- Called from SceneRenderer::SetupFrameData() or a dedicated frame phase

### Sub-task D: Wire captured cubemap to IBLBaker (1 file: .cpp)
- Modify Update() to use captured cubemap SRV instead of sky cubemap SRV
- Only when capture is complete; fall back to sky if capture hasn't happened yet
