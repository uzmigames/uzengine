<!-- doc-meta
title: VFX Viewport Controls Reference
category: guide
theme: editor
created: 2026-03-26
updated: 2026-03-27
status: active
author: claude
-->

# VFX Viewport Controls Reference

## Overview

The VFX Viewport demo (`demos/vfx_viewport`) is a standalone DX12 application for previewing PBR meshes, procedural sky, shadow mapping, HDR tonemapping, GPU-driven particles, trails, decals, and distortion effects. This guide covers all keyboard and mouse controls, command-line arguments, and debug view modes.

## Camera Controls

| Control | Action |
|---------|--------|
| WASD | Move camera (forward/left/back/right relative to camera) |
| Space | Move up |
| Ctrl | Move down |
| Shift (hold) | 3x sprint speed |
| RMB (hold) + mouse move | Mouse look (yaw/pitch, pitch clamped ±89°) |
| MMB drag | Orbit camera around center |
| Scroll wheel | Dolly forward/back |
| Ctrl + Scroll | Adjust FOV (30°-120°) |

Default camera position: (0, -40, 25), yaw=90°, pitch=-30°, move speed=15 units/sec.

## Mesh & Material Controls

| Control | Action |
|---------|--------|
| Left/Right arrows | Cycle material presets (Red Plastic, Gold Metal, Rough Stone, Polished Chrome, Rubber, Copper, Ceramic White, Emissive Blue) |
| Up/Down arrows | Cycle mesh (Sphere, Cube, Plane, Cylinder, Torus) |
| Alt + LMB drag | Rotate object (yaw from horizontal, pitch from vertical, clamped ±89°) |

## Lighting & Environment

| Control | Action |
|---------|--------|
| T / Shift+T | Increase / decrease sun elevation (-10° to 90°) |
| R / Shift+R | Rotate environment ±15° (sun/sky Y-axis rotation, wraps 0°-360°) |
| L | Toggle point lights (3 colored: red/blue/green with Karis 2013 attenuation) |
| C | Cycle background preset (Sky → Dark Gray → Black → White → Neutral Gray) |

## Post-Processing

| Control | Action |
|---------|--------|
| 9 / 0 | Increase / decrease exposure (×1.2 per step, range 0.1-10.0) |
| B | Toggle bloom (GPU compute threshold + downsample + upsample) |
| V | Toggle vignette (0 or 0.4 intensity) |
| P | Toggle post-processing (ACES tonemap on/off) |

## Shadow Mapping

| Control | Action |
|---------|--------|
| 7 / 8 | Decrease / increase shadow bias (×0.5/×2, range 0.0001-0.05) |
| G | Toggle shadow map debug overlay (256×256 corner view) |

## VFX System

| Control | Action |
|---------|--------|
| Tab | Cycle VFX emitter preset (Fire Fountain, Smoke Plume, Sparks, Magic Orb, Explosion Debris) |
| Shift+Tab | Toggle VFX on/off |
| 1 / 2 | Increase / decrease spawn rate (×1.5, range 10-5000/s) |
| 3 / 4 | Increase / decrease particle size (×1.5) |
| 5 / 6 | Decrease / increase gravity Y (±2.0) |
| Y | Toggle trail/ribbon rendering |
| D | Toggle decal projection |
| X | Toggle distortion (heat-haze) |

## Utility Controls

| Control | Action |
|---------|--------|
| H | Print keyboard shortcut reference to console |
| Alt+Enter | Toggle fullscreen (borderless) |
| F11 | Trigger RenderDoc frame capture (requires renderdoc.dll loaded) |
| F12 | Toggle stats overlay |

## Command-Line Arguments

### Common Arguments (DemoApplication)

| Argument | Description |
|----------|-------------|
| `--backend dx12\|dx11\|vulkan` | Select RHI backend (default: dx12) |
| `--sm 6.6\|6.0\|5.0` | Force shader model |
| `--screenshot <path>` | Capture screenshot to file |
| `--screenshot-frame <N>` | Capture at frame N (default: 5) |
| `--frames <N>` | Exit after N frames (0 = unlimited) |
| `--view-mode <mode>` | Initial debug view mode |
| `--all-modes <path>` | Capture all debug views and compose grid |
| `--camera-x/y/z <val>` | Set initial camera position |
| `--camera-yaw/pitch <deg>` | Set initial camera angles |
| `--cvar <name=value>` | Override CVar at startup |

### VFX Viewport-Specific Arguments

| Argument | Description |
|----------|-------------|
| `--albedo <path>` | Load albedo PBR texture from file |
| `--normal <path>` | Load normal map PBR texture from file |
| `--roughness <path>` | Load roughness PBR texture from file |

### Utility Modes

| Argument | Description |
|----------|-------------|
| `--compose-grid <output> [--cell-size W] [--label-height H] <img1> ...` | Compose multiple screenshots into labeled grid |
| `--diff <output> <imgA> <labelA> <imgB> <labelB> [--cell-size N]` | Pixel-diff two screenshots |

## Debug View Modes

The VFX Viewport supports multiple debug view modes for visual analysis. Cycle through modes using `--view-mode` or the in-app view switching:

- **Lit** — Final lit scene with all post-processing
- **Albedo** — Base color only (diffuse texture)
- **WorldNormal** — Per-pixel world-space normal vectors (RGB encoded)
- **Metallic** — Metallic parameter visualization
- **Roughness** — Roughness parameter visualization
- **Depth** — Linear depth visualization (black=near, white=far)
- **Wireframe** — Triangle wireframe overlay on lit scene
- **Unlit** — Geometry without lighting computation

Use `--all-modes` to capture all views and automatically compose them into a labeled grid for comparison.

## Example Usage

### Basic Launch
```bash
cd demos/vfx_viewport
zig build
zig-cache/bin/vfx_viewport.exe
```

### Custom Camera Position
```bash
vfx_viewport.exe --camera-x 5 --camera-y 8 --camera-z 4 --camera-yaw -45 --camera-pitch -30
```

### Load Custom PBR Textures
```bash
vfx_viewport.exe --albedo my_texture.png --normal my_normal.png --roughness my_roughness.png
```

### Screenshot for Documentation
```bash
vfx_viewport.exe --frames 100 --screenshot-frame 30 --screenshot output.png
```

### Batch Grid Comparison
```bash
vfx_viewport.exe --all-modes grid.png --cell-size 512 screenshot1.png screenshot2.png
```

### Pixel-Diff Analysis
```bash
vfx_viewport.exe --diff diff_result.png screenshot_a.png "DX12" screenshot_b.png "DX11" --cell-size 512
```

## Key Takeaways

- **Camera**: WASD for movement, RMB for mouse look, scroll to dolly, Shift for sprint
- **Materials**: Left/Right arrows to cycle presets, Up/Down for meshes, Alt+LMB to rotate
- **Lighting**: T/R adjust sun elevation and rotation, L toggles point lights, C cycles backgrounds
- **Post-FX**: 9/0 adjust exposure, B/V/P toggle bloom/vignette/tonemapping
- **VFX**: Tab cycles emitters, 1-6 adjust spawn rate/size/gravity, Y/D/X toggle trails/decals/distortion
- **Debug**: G shows shadow map, H prints shortcuts, F12 shows stats, F11 captures RenderDoc frame

## References

- [VFX System Design](../analysis/vfx-system-design.md)
- [PBR Material Reference](../guides/pbr-materials.md)
- [Shadow Mapping Guide](../guides/shadow-mapping.md)
- [Post-Processing Effects](../guides/post-processing.md)

## Changelog

| Date | Change |
|------|--------|
| 2026-03-26 | Initial creation with full control reference and command-line arguments |
