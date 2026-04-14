# Proposal: VFX Particle Flipbook Rendering Pipeline — Complete Fix

## Why

The particle flipbook/SubUV system is broken on the Application render path
(`demos/vfx_viewport_app`). Particles always render a **single fixed frame**
of a sprite sheet atlas, regardless of:
- Particle age (verified — age varies per particle via color-over-life)
- Global time (verified — `timeParams.x` is readable elsewhere)
- Hardcoded frame index in VS (verified — particles still show frame 0)
- Constant buffer values for `flipbookRows/Cols/TotalFrames` (verified
  — these reach the PS correctly: `amarelo` test showed R=0.9 G=0.6)

An **independent debug quad** using the exact same atlas sampling logic
(`linearClamp` sampler at s2, bindless SRV, `col*invCols + uv.x*invCols`
UV math) **cycles perfectly** when given the same sprite texture and
time via root constants. This proves:
- Atlas sampling math is correct
- Bindless SRV binding works
- The sprite texture is loaded with the right layout
- The `linearClamp` sampler at s2 works for multi-frame atlases

So the bug is **exclusively in the particle rendering path** — in how the
per-particle data (age/lifetime/CB fields) reaches the billboard VS/PS
and how the final atlas UV is computed.

Quick patches have been attempted (moving flipbook math between VS and
PS, trying `input.age`, trying `vfxTotalTime` in the CB, using hardcoded
frame indices) without success. The patches reveal that something
fundamental about the billboard VS/PS data flow is broken, not just the
flipbook math.

This task requires a full, root-cause investigation. No more patches.

## What Changes

Comprehensive audit and re-implementation of the particle billboard
rendering pipeline for flipbook/SubUV support, following Niagara's
reference implementation exactly.

### Investigation phase
1. Trace the CB binding: verify `VFXBillboardCB` bytes at the GPU
   actually contain the CPU-side values. Use RenderDoc or a CPU-side
   readback pass.
2. Trace the SRV reads in VS: confirm `ages[particleID]` returns
   incrementing values. Output the raw value to a debug target.
3. Compare with debug quad: the debug quad works with root constants.
   The particles use root CBV. Identify the binding path difference.
4. Check PSO state: sampler bindings, static samplers, root signature
   declaration — confirm the particle PSO has s2 linear clamp sampler
   like the debug quad.

### Implementation phase
5. Rewrite the flipbook path strictly following Niagara's
   `NiagaraSpriteVertexFactory.ush`:
   - Per-particle `subImageIndex` float attribute computed in Update CS
   - VS reads `subImageIndex` from its own SoA SRV
   - VS pre-computes atlas UVs for frame A and frame B and passes them
     to PS via interpolants
   - PS just samples texture at interpolated atlas UVs — no flipbook
     math in PS
6. Remove all pad-field hacks. If CB needs more fields, explicitly
   increase size and update both HLSL and C++ struct layouts.
7. Unified flipbook source of truth: one `flipbook_atlas.hlsli` helper
   with `ComputeAtlasUV(frameIndex, rows, cols, cornerUV)` used by BOTH
   particle path AND debug quad. No duplicate atlas math.

### Verification phase
8. Visual test: particle cycles all 36 frames during its lifetime at
   300 emissions/sec, 1.75s lifetime (fire preset).
9. Visual test: particle renders same frame as debug quad at a given
   point in time (both using `vfxTotalTime`-driven cycling).
10. Performance: no regression — particle draw should stay under 0.5ms
    on RTX 5080 at 500 particles.

## Impact

- Affected specs: `docs/engine/25-vfx/` (rendering pipeline)
- Affected code:
  - `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl`
  - `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl`
  - `runtime/shaders/runtime/vfx/vfx_flipbook.hlsli`
  - `runtime/shaders/runtime/vfx/particle_update_cs.hlsl` (add
    subImageIndex write if per-particle)
  - `runtime/engine/renderer/passes/vfx/particle_render_pass.h/.cpp`
  - `runtime/engine/renderer/frame/frame_particles.cpp`
  - `runtime/engine/renderer/frame/frame_vfx_inline.cpp`
  - `runtime/engine/vfx/core/gpu_particle_buffer.h/.cpp` (if
    subImageIndices becomes a new SoA channel)
- Breaking change: NO (fixes a broken feature)
- User benefit: Working flipbook/SubUV animation for VFX particles,
  matching Niagara's reference behavior.

## Source
- Reference: UE5 Niagara at `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\`
- Debug quad proof-of-concept:
  - `runtime/shaders/runtime/vfx/flipbook_debug_vs.hlsl`
  - `runtime/shaders/runtime/vfx/flipbook_debug_ps.hlsl`
  - `runtime/engine/renderer/frame/frame_overlay.cpp` (FlipbookDebugPass)
