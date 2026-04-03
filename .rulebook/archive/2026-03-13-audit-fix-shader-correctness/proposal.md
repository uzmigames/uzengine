# Proposal: Shader Correctness Fixes

## Why
Internal audit (2026-03-13) found 3 CRITICAL and 3 HIGH shader bugs causing incorrect PBR rendering,
wrong cascade selection, and register mismatches between HLSL and C++. These produce visible artifacts
(fireflies, washed-out colors, wrong shadows) and will cause hard-to-debug issues as complexity grows.

## What Changes
- Fix register mismatches between constant_buffers.h and HLSL declarations (F-01, F-02)
- Fix PBR geometry term k formula — use Karis 2013 direct light formula (F-06)
- Add sRGB decode for albedo textures (F-05)
- Fix cascade selection to use view-space Z instead of Euclidean distance (F-04)
- Clarify cascade selection loop logic (F-03)
- Create shared `.hlsli` includes for PBR and shadow functions (F-07)
- Fix per-channel Reinhard tonemapping hue shift (F-08)
- Fix tangent transformation to use normalMatrix (F-14)
- Add PCF boundary clamping per cascade tile (F-15)

## Impact
- Affected specs: rendering pipeline, PBR shading, shadow mapping
- Affected code: runtime/shaders/runtime/*.hlsl, runtime/engine/renderer/passes/*.cpp, constant_buffers.h
- Breaking change: NO (visual improvement, same API)
- User benefit: Correct PBR rendering, correct shadows, maintainable shader code
