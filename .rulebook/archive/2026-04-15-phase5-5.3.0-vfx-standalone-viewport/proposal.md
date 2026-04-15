<!-- doc-meta
title: VFX Standalone Viewport — Full Material Preview Parity
id: 5.3.0
phase: 5
status: approved
created: 2026-03-26
-->

# 5.3.0 — VFX Standalone Viewport (Full Material Preview Parity)

## Why

The engine's full `SceneRenderer` cannot build as a standalone executable due to massive dependency chains (ECS, VFS, Lua, ScriptSystem, all excluded in MSVC CMake preset). A minimal `uz_viewport.exe` exists but only renders a hardcoded PBR sphere with no interaction — it has none of the features the real material viewport has, blocking VFX iteration on a focused harness.

## Goal

Replicate **every feature** of the existing material/scene viewport in the standalone `uz_viewport.exe`, then integrate the VFX particle/trail/decal system on top. The viewport must be a self-contained DX12 application with zero engine library dependencies.

## Scope

### Features to Replicate (from SceneRenderer + Editor)

**Camera System** (from `camera.h` + `fps_camera_controller.h`):
- FPS camera: WASD + Space/Ctrl + Shift sprint + mouse look (RMB)
- Orbit mode: middle mouse drag, scroll to zoom
- Camera inertia/smoothing
- FOV control (scroll wheel with Ctrl held)

**Input System** (from `input.h`):
- Full keyboard state (256 keys, press/release/held)
- Mouse buttons (L/R/Middle), delta, scroll
- Raw mouse input (WM_INPUT for unaccelerated delta)
- Mouse capture/release (ClipCursor when RMB held)

**Sky & Environment**:
- Procedural atmosphere (Hillaire-like Rayleigh/Mie gradient)
- Sun disc + glow halo
- Horizon haze
- Time-of-day control (key to rotate sun direction)

**Editor Grid** (from `editor_grid.h` + `grid_vs/ps.hlsl`):
- Infinite XZ-plane grid (no vertex buffer, fullscreen technique)
- Anti-aliased lines with fwidth
- Minor (1m) + major (10m) grid lines
- Distance fade (100m)
- Axis-colored center lines (R=X, B=Z)

**Lighting**:
- Directional light (sun) with adjustable direction
- Point light support (up to 4)
- PBR Cook-Torrance (GGX NDF + Smith-Joint + Schlick Fresnel)
- Hemisphere ambient (sky color top, ground color bottom)
- IBL approximation (spherical harmonics from sky)

**Shadows**:
- Single cascade shadow map (directional light)
- PCF soft filtering
- Shadow bias (slope-scaled + constant)

**Materials & Meshes**:
- Multiple procedural meshes: sphere, cube, plane, cylinder, torus
- Material parameters: baseColor, metallic, roughness, AO, emissive
- Texture loading (PNG via stb_image for albedo/normal/roughness maps)
- Material hot-swap (keyboard shortcuts to cycle materials)

**Post-Processing**:
- ACES filmic tonemapping
- Exposure control (manual, +/- keys)
- Bloom (threshold + 3-level downsample/upsample)
- Vignette
- Gamma correction

**Debug Views** (from `debug_view_mode.h`):
- Lit (default)
- Albedo only
- Normals visualization
- Metallic/Roughness
- Depth (linearized)
- Wireframe overlay
- Cycle with F1-F8 keys

**Stats Overlay** (from `stats_overlay.h`):
- FPS counter (EMA smoothed)
- Frame time (ms)
- Draw call count
- Triangle count
- Camera position + yaw/pitch
- GPU adapter name
- Toggle with F12

**Screenshot**:
- F5 to capture PNG to disk
- --screenshot-stdout for headless capture
- --headless mode (hidden window)
- --frames N to auto-exit

**VFX Integration** (Phase 2 — after viewport parity):
- GPU particle compute (spawn/update/indirect)
- Billboard particle rendering (alpha blend + soft depth fade)
- Trail/ribbon rendering
- Decal projection
- Particle sort (back-to-front)
- Flipbook animation
- Distortion/refraction

## Architecture

Single file: `runtime/tools/uz_viewport.cpp` (will grow to ~2000-3000 lines).
All shaders embedded as string literals.
Runtime compilation via `D3DCompile`.
Zero external file dependencies (meshes procedural, textures optional).

## Build

```bash
cd runtime
cmake --preset msvc-debug
cmake --build build/msvc-debug --target uz_viewport -j 4
```

Links only: `d3d12.lib dxgi.lib dxguid.lib d3dcompiler.lib`
