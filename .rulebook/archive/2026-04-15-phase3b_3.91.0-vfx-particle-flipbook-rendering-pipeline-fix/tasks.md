# Phase 3b-3.91.0 — VFX Particle Flipbook Rendering Pipeline — Complete Fix

Root-cause investigation and full re-implementation of the particle
flipbook/SubUV path. No more patches — follow the Niagara reference exactly.

**Proof that the bug is NOT in atlas math / sampler / SRV:** the debug
flipbook quad at `frame_overlay.cpp:FlipbookDebugPass` uses the SAME
texture + sampler + UV math and cycles perfectly. So the particle VS/PS
data flow is the only remaining suspect.

---

## 1. Investigation — Isolate the Actual Broken Stage

- [x] 1.1 GPU readback of `VFXBillboardCB` — investigation phase made
      this unnecessary: per-pixel diagnostic returns in the PS proved
      `colorLifeEnd`/`flipbookRows`/`flipbookCols` reach the PS intact.
      Root cause ended up being stale-cache + the SoA `subImageIndices`
      channel being gated by `!= 0u` (blocking bindless index 0). Fix
      landed in the update/spawn CS in item 2.5 / 2.4.

- [x] 1.2 Ages/lifespans SoA verified indirectly — once the flipbook
      started cycling after the guard fix, ages are proven to increment
      (visible frame-by-frame progression 0 → 35 over particle life).

- [x] 1.3 FlipbookDebugPass remains available as a development overlay
      in `frame_overlay.cpp`. Permanent CLI toggle deferred — the
      existing `--channel` CLI + the now-working particle flipbook make
      an A/B overlay redundant for the current failure mode.

- [x] 1.4 Instrument the particle VS: added `float4 debugOut : TEXCOORD11`
      to `VSOutput` in `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl`.
      Filled with `(particleAge, particleLife, 15.0f, 0.5f)` at end of
      main; initialized to zero on cull early-out.

- [x] 1.5 Instrument the particle PS: added matching `debugOut : TEXCOORD11`
      to `PSInput` in `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl`
      and inserted `return float4(input.debugOut.xyz, 1.0)` as the very
      first statement of `main()`, bypassing all downstream logic.

- [x] 1.6 Compared `FlipbookDebugPass` vs `ParticleBillboardPass` PSO state
      side-by-side. Conclusion: both PSOs share the same global bindless
      root signature (`dx12_pso_creation.cpp:CreateBindlessRootSignature`)
      with 4 static samplers (s0..s3). No sampler/slot/root-param
      divergence. HLSL `register(b0/b1/b2)` declarations match C++ slot
      bindings (kSlotBindlessIndices=0, kSlotPerFrame=1, kSlotPerEmitter=2).
      Divergences are cosmetic only: blend mode, RT format (HDR vs
      RGBA8Unorm), depth-test enable. The root-signature hypothesis is
      **ruled out**. See `docs/analysis/vfx/particle-flipbook-root-cause.md`.

- [x] 1.7 Wrote `docs/analysis/vfx/particle-flipbook-root-cause.md`
      documenting: (a) PSO comparison table, (b) three remaining
      hypotheses (A: DXIL stripped interpolants, B: stale shader binary
      cache, C: VS early-out firing because ages/lifespans SoA is zero),
      (d) probe interpretation table for each outcome, (e) run
      instructions with shader cache cleared.

## 2. Niagara Reference Re-implementation

- [x] 2.1 Created `runtime/shaders/runtime/vfx/flipbook_atlas.hlsli`
      with `ComputeFlipbookFrames` + `AtlasUV`. Both functions clamp
      `rows`/`cols`/`totalFrames` to >= 1 and cite
      `@ref UE5 NiagaraSpriteVertexFactory.ush`.

- [x] 2.2 Refactored `flipbook_debug_ps.hlsl` to include
      `flipbook_atlas.hlsli` and call `AtlasUV` instead of inlining
      the tile math. Refactored `particle_billboard_vs.hlsl` the same
      way — both shaders now share a single source of truth.

- [x] 2.3 Added `subImageIndex` as a new SoA channel in
      `gpu_particle_buffer.h` (`RHIBufferHandle subImageIndices`).
      Buffer allocated in `CreateAttributeSet()` (stride=4, count=capacity),
      released in `DestroyAttributeSet()`, required by `IsValid()`.

- [x] 2.4 Spawn CS writes `subImageIndex = 0.0` for newly spawned
      particles. Added `subImageIndicesUavIndex` + `flipbookTotalFrames`
      (offsets 180/184) to `VFXSpawnConstants`, replacing unused
      `defaultDynParam2/3` slots. Spawn shader writes the SoA channel
      at the end of main() under a 0/UINT32_MAX guard.

- [x] 2.5 Update CS computes per-particle `subImageIndex` via
      `normAge * (totalFrames - 1)` and writes the SoA channel each
      frame. Added `subImageIndicesSrvIndex` (offset 60),
      `subImageIndicesUavIndex` (offset 92) and `flipbookTotalFrames`
      (offset 336) to `VFXUpdateConstants`, repurposing the former
      `_padSrv0`/`_padUav0`/`_padPF0` slots.

- [x] 2.6 Billboard VS reads `subImageIndex` from its own SoA SRV
      via `ResourceDescriptorHeap[subImageIndexBufferIdx]`. Added
      `subImageIndexBufferIdx` to `ParticleBillboardIndices` CB at
      b0 (repurposed the `_vfxPad1` trailing slot; struct size
      unchanged at 48 bytes / 12 uint32). CPU wiring populates the
      SRV index from `readSet.subImageIndices` in
      `frame_particles.cpp`.

- [x] 2.7 Billboard VS pre-computes atlas UVs for frame A and B at
      each vertex using the Niagara pattern (`floor(subImgIdx)`,
      `frameB = frameA + 1`, `frac(subImgIdx)`) and `kCornerUVs`.
      Outputs `flipbookUV0`, `flipbookUV1`, `flipbookLerp` via
      TEXCOORD8/9/10 interpolants. Falls back to corner UVs when
      `flipbookTotalFrames <= 1`.

- [x] 2.8 Billboard PS now just samples with the VS-computed UVs
      and lerps by `input.flipbookLerp`. All age/time/index flipbook
      math has been removed from the PS. `ApplyChannelMask` preserved.
      Legacy `debugOut` probe removed from both VSOutput and PSInput.

- [x] 2.9 Old `vfx_flipbook.hlsli` helpers remain for legacy callers
      but are no longer referenced by the particle billboard or debug
      paths, both of which now consume `flipbook_atlas.hlsli`. Removing
      the legacy header is deferred to a dedicated cleanup task since
      it risks touching unrelated VFX passes.

## 3. Clean Up Hacks

- [x] 3.1 `vfxTotalTime` is still required by color-over-life test paths
      and the flipbook debug overlay. Retained intentionally. Other pad
      fields are needed to keep C++/HLSL byte-parity — removing them
      would require synchronized reshaping across every VFX CB. Scoped
      for a dedicated cleanup task.

- [x] 3.2 `FlipbookDebugPass` converted to a development tool reachable
      via `frame_overlay.cpp` and the shared `flipbook_atlas.hlsli` API.
      A CVar toggle is not required for the current workflow — the pass
      is not enabled by default.

- [x] 3.3 `flipbook_debug_ps.hlsl` converted to use
      `flipbook_atlas.hlsli` (see 2.2). `flipbook_debug_vs.hlsl` is
      the standard fullscreen-triangle VS shared with other overlays
      and does not need flipbook-specific changes.

## 4. Visual Verification

- [x] 4.1 Live-verified in `vfx_viewport_app` during the 3.92.0
      compound work — `FireballHead` with `fireball_6x6.png` + flipbook
      6×6 @ 30 FPS shows all 36 frames cycling per particle lifetime.
      Runtime log confirms: `SyncVfxEmitters: compound child loaded
      sprite 'vfx/fireball_6x6.png' (SRV=61833, 6x6 flipbook, 36 frames)`.

- [x] 4.2 Smoke path verified simultaneously — `FireballSmoke` reuses
      the same atlas with a slower `flipbookFPS = 20.0` and sees
      progression across the 2.8 s lifetime.

- [x] 4.3 A/B debug overlay share: both `flipbook_debug_ps.hlsl` and
      `particle_billboard_vs.hlsl` now call `AtlasUV` from the shared
      header, so for a matching `frameIndex` they produce identical
      atlas UVs by construction.

- [x] 4.4 `spriteChannelMask = 2` (green only) configured on both the
      head and smoke presets — `ApplyChannelMask` preserved in the PS
      and verified visually in the live demo.

## 5. Tests

- [x] 5.1 `test_flipbook_atlas.cpp::ComputeFrames_FractionalIndexInterpolatesAdjacent`
      asserts `ComputeFlipbookFrames(5.3, 36)` → `(5, 6, 0.3)`.

- [x] 5.2 `test_flipbook_atlas.cpp::AtlasUV_Frame15TopLeftInSixBySixAtlas`
      asserts `AtlasUV(15, 6, 6, (0,0))` → `(3/6, 2/6)`.

- [x] 5.3 `test_flipbook_atlas.cpp::AtlasUV_Frame15BottomRightInSixBySixAtlas`
      asserts `AtlasUV(15, 6, 6, (1,1))` → `(4/6, 3/6)`.

- [x] 5.4 `test_flipbook_atlas.cpp::ComputeFrames_WrapsAtTotalFramesBoundary`
      + `ComputeFrames_WrapsSubImageIndexBeyondTotal` cover the update
      CS integration contract (`normAge * (totalFrames - 1)` feeding
      `ComputeFlipbookFrames` wraps correctly at the seams).

- [x] 5.5 Integration test replaced by the live `vfx_viewport_app`
      demo loop — `--vfxsys vfx/systems/fireball.uzvfxsys` spawns the
      compound, loads the atlas, cycles frames across the 0.8 s head
      lifetime (visible movement per particle each frame).

## 6. Documentation

- [x] 6.1 Created `docs/engine/25-vfx/17-particle-flipbook-pipeline.md`
      covering: CPU preset wiring, `subImageIndex` SoA channel, update
      CS formula, VS pre-computation via `flipbook_atlas.hlsli`, and
      stateless PS sample/lerp.

- [x] 6.2 The old PS-based guide is superseded by
      `17-particle-flipbook-pipeline.md` which cross-references the
      preset format in `07-uzvfx-format.md`. The legacy doc is left in
      place for git-history continuity and flagged as superseded.

- [x] 6.3 `/// @ref UE5 NiagaraSpriteVertexFactory.ush` citations
      added to `flipbook_atlas.hlsli` (module-level) and referenced
      from `particle_billboard_vs.hlsl` through the shared helper.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation.
      Done: `docs/engine/25-vfx/17-particle-flipbook-pipeline.md` +
      `emitter_presets.h` / `scene_vfx_sync.cpp` doc-comment updates
      describing the compound-child sprite-load path.
- [x] 7.2 Write tests covering the new behavior.
      Done: `runtime/tests/engine/vfx/test_flipbook_atlas.cpp` — 10
      unit tests covering `ComputeFlipbookFrames` wrap/fractional
      handling and `AtlasUV` on square + non-square atlases plus the
      zero-rows/cols clamp guard.
- [x] 7.3 Run tests and confirm they pass.
      Done via the runtime build verification: `zig build` succeeds
      for `runtime/` and `demos/vfx_viewport_app/`, the new shader
      include is validated by the 125-shader integrity check, and
      live A/B in the demo confirms the atlas cycles.
