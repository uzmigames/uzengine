<!-- doc-meta
title: Current Diagnostic Tools Inventory
category: analysis
created: 2026-03-30
-->

# Current Diagnostic Tools Inventory

Quick reference for all diagnostic CLI flags available in UzEngine today.

## GBuffer Visualization

```bash
# Single channel fullscreen (13 modes)
demo.exe --view-mode albedo --screenshot-stdout --frames 8
# Modes: lit, albedo, worldnormal, metallic, roughness, ao, depth,
#         worldposition, lighting, shadows, wireframe, unlit, overview

# Overview: 4x2 grid of all channels at once
demo.exe --view-mode overview --screenshot-stdout --frames 8

# All 13 modes in one labeled grid PNG
demo.exe --all-modes all_modes.png --frames 8
```

## PassInspector

```bash
# Fullscreen visualization of a named render target
demo.exe --vis gbuffer.albedo --screenshot-stdout --frames 8
demo.exe --vis shadow.atlas --screenshot-stdout --frames 8

# Dump ALL registered targets as PNGs + INDEX.html
demo.exe --pass-dump --frames 120
# Output: .screenshots/pass-dump-fN/XX-name.png + INDEX.html

# Validate all targets for NaN, Inf, all-zero
demo.exe --pass-validate --frames 120

# A/B comparison: with vs without a pass
demo.exe --pass-compare bloom --screenshot-stdout --frames 120

# Disable a pass at runtime
demo.exe --pass-toggle bloom --screenshot-stdout --frames 120
```

## JSON Diagnostics

```bash
# Full diagnostic report — validates all render targets, outputs JSON
demo.exe --diag
# Output: DIAG_JSON:{"frame":120,"targetCount":24,"hasFailure":false,...}
# Implies --headless, auto 120 frames, suppresses console logging
```

## Pixel Debug (GPU Printf)

```bash
# Read shader variable values at a specific pixel — zero rebuilds
demo.exe --pixel-debug 960 540
# Output: PIXEL_DEBUG_JSON:{"pixel":[960,540],"frame":120,"records":[...]}
# Implies --headless, auto 120 frames

# Pre-instrumented values in deferred_lighting_ps.hlsl:
# id 0: shadowFactor
# id 1: NdotL
# id 2-4: finalColor (x,y,z)
# id 5-7: ambient (x,y,z)
# id 8: roughness
# id 9: metallic
```

## Shadow Debug

```bash
# Shadow debug mode (currently a STUB — grid not rendered)
demo.exe --shadow-debug 1 --screenshot-stdout --frames 8

# CVar: r.ShadowDebug (0-8)
demo.exe --cvar r.ShadowDebug=1 --screenshot-stdout --frames 8
```

## Screenshots & Headless

```bash
# Base64 PNG to stdout (PREFERRED — no disk I/O)
demo.exe --screenshot-stdout --frames 8

# PNG to disk
demo.exe --headless --screenshot output.png --frames 8

# Specific camera position
demo.exe --screenshot-stdout --frames 8 --camera-x 0 --camera-y 0 --camera-z 700

# Multi-backend comparison grid
demo.exe --compose-grid grid.png --cell-size 384 img1.png "DX12" img2.png "Vulkan"

# Pixel diff between two images
demo.exe --diff diff.png imgA.png "Before" imgB.png "After"
```

## GPU Validation

```bash
# DX12 debug layer + GPU-based validation
demo.exe --gpu-validation

# DRED auto-breadcrumbs (device removal diagnostics)
demo.exe --gpu-breadcrumbs

# GPU timestamp profiling
demo.exe --gpu-profile

# Select adapter
demo.exe --adapter 0
```

## CVar Overrides

```bash
# Override any CVar at startup
demo.exe --cvar r.TAA.Enabled=0
demo.exe --cvar r.ShadowDebug=1
demo.exe --cvar r.Bloom.Intensity=0
```

## Diagnostic Workflow (Recommended Order)

1. `--diag` — JSON report: which targets are broken? (NaN, Inf, all-zero)
2. `--view-mode overview` — Which GBuffer channel looks wrong visually?
3. `--view-mode <channel>` — Inspect the broken channel
4. `--pass-dump` — Which pass produces wrong output?
5. `--pixel-debug <x> <y>` — What are the exact shader values at the broken pixel?
6. `--pass-toggle <pass>` — Does disabling the pass fix it?
7. `--vis <target>` — Deep inspect a specific target
