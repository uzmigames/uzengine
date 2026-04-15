# Proposal: phase3b_3.90.0-vfx-pr7-review-fixes

## Why
PR #7 (`feat/vfx-system`) adds a GPU VFX particle system but has critical issues
found in code review (docs/analysis/pr7-vfx-system-review.md): wrong coordinate
system (Y-up in Z-up engine), wrong units (metres in cm engine), broken shader
entry points, dead code paths, missing material integration, missing vfx_common.hlsli,
and non-VFX changes mixed into the PR.

## What Changes
Fix all review issues so the VFX system integrates cleanly with falcor-parity:
- Coordinate/unit corrections (Y-up -> Z-up, metres -> cm)
- Shader entry point fix (CSMain -> main)
- DXC runtime compilation for composed module shaders
- VFXMaterial inherits MaterialInterface
- Flipbook SubUV blending in billboard pixel shader
- Config deserialization and curve texture upload
- Create missing vfx_common.hlsli (RNG helpers)
- Demo compatibility with falcor-parity engine (remove inline CB lights, grid reflection)
- GPU particle pipeline debugging (particles not yet rendering)

## Impact
- Affected specs: docs/specs/particle-system.md
- Affected code: engine/vfx/, shaders/runtime/vfx/, demos/vfx_viewport/
- Breaking change: NO
- User benefit: Merge-ready VFX particle system with correct engine conventions
