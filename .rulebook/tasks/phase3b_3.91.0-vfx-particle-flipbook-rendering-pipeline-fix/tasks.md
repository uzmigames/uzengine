# Phase 3b-3.91.0 — VFX Particle Flipbook Rendering Pipeline — Complete Fix

Root-cause investigation and full re-implementation of the particle
flipbook/SubUV path. No more patches — follow the Niagara reference exactly.

**Proof that the bug is NOT in atlas math / sampler / SRV:** the debug
flipbook quad at `frame_overlay.cpp:FlipbookDebugPass` uses the SAME
texture + sampler + UV math and cycles perfectly. So the particle VS/PS
data flow is the only remaining suspect.

---

## 1. Investigation — Isolate the Actual Broken Stage

- [ ] 1.1 GPU readback of `VFXBillboardCB` right before DrawBillboards.
      Create a small CPU-side readback pass that:
      - Allocates a `READBACK` buffer the size of `VFXBillboardCB`.
      - Copies the CB's GPU address's contents via `CopyBufferRegion`.
      - Maps on CPU (3 frames later) and logs every field.
      Confirm `flipbookRows`, `flipbookCols`, `flipbookTotalFrames`,
      `flipbookFPS`, `vfxTotalTime` match the CPU-side values exactly.

- [ ] 1.2 GPU readback of the first 4 ages and lifespans from the
      SoA buffer used by the VS (`gpuBuf.WriteSet().ages`). Log 5 frames
      in a row. Expected: ages should strictly increase each frame.

- [ ] 1.3 Pipeline visualizer frame — ship the `FlipbookDebugPass`
      permanently as a `--flipbook-debug` CLI toggle. It renders the
      debug quad over the scene so the user can A/B compare cycling
      quad vs static particles in real time.

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

- [ ] 2.1 Create `runtime/shaders/runtime/vfx/flipbook_atlas.hlsli`
      with:
      ```hlsl
      float2 AtlasUV(uint frameIndex, uint rows, uint cols, float2 cornerUV);
      void ComputeFlipbookFrames(float subImageIndex, uint totalFrames,
                                 out uint frameA, out uint frameB,
                                 out float lerpT);
      ```
      - `AtlasUV`: single helper used by BOTH particle shaders AND
        `flipbook_debug_ps.hlsl`. No duplication.
      - `ComputeFlipbookFrames`: Niagara pattern
        (`SubImageLerp = frac(subImageIndex)`, `SubImageA = floor(...)`,
        `SubImageB = SubImageA + 1`).
      - `@ref UE5 NiagaraSpriteVertexFactory.ush`.

- [ ] 2.2 Refactor `flipbook_debug_ps.hlsl` to use `flipbook_atlas.hlsli`.
      Verify debug quad still cycles after refactor.

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

- [ ] 2.9 Remove the old flipbook paths from `vfx_flipbook.hlsli`
      (`ComputeFlipbookFrame`, `SampleFlipbookUV`). Their callers
      now use `flipbook_atlas.hlsli`.

## 3. Clean Up Hacks

- [ ] 3.1 Remove `vfxTotalTime`, `_vfxBillboardPad0/1`, `_cbsPad0-4`,
      `_cbsFlipbookFPS`, `_cbsSpriteChannelMask`, `_cbsVfxTotalTime`
      from `VFXBillboardCB`. Grow the struct explicitly if new fields
      are needed. Keep byte-for-byte parity between HLSL and C++.

- [ ] 3.2 Remove the FlipbookDebugPass or convert it to a clean CVar-
      gated development tool in `cvar_vfx.h`. Default: off.

- [ ] 3.3 Delete `flipbook_debug_vs.hlsl` and `flipbook_debug_ps.hlsl`
      if the pass is removed, or convert them to the shared
      `flipbook_atlas.hlsli` API.

## 4. Visual Verification

- [ ] 4.1 Screenshot: fire preset with `fireball_6x6.png` sprite, first
      10 frames. Each frame's screenshot must show different atlas
      tile visible on at least one particle.

- [ ] 4.2 Screenshot: smoke preset (4.5s lifetime) with same atlas.
      Confirm particles progress from frame 0 to frame 35 during life.

- [ ] 4.3 A/B comparison screenshot: single-frame with `--flipbook-debug`
      enabled. Debug quad and particles must show the same atlas tile
      when particles' `subImageIndex == quad's frameI`.

- [ ] 4.4 `--channel 2` (green channel) screenshot on fire preset.
      Confirm channel mask still works after rewrite.

## 5. Tests

- [ ] 5.1 Unit test: `ComputeFlipbookFrames(5.3, 36)` returns
      `(frameA=5, frameB=6, lerpT=0.3)`.

- [ ] 5.2 Unit test: `AtlasUV(15, rows=6, cols=6, cornerUV=(0,0))`
      returns `(3/6, 2/6)` (frame 15 = col 3 row 2 in a 6x6 atlas).

- [ ] 5.3 Unit test: `AtlasUV(15, rows=6, cols=6, cornerUV=(1,1))`
      returns `(4/6, 3/6)` (top-right corner of frame 15's tile).

- [ ] 5.4 CPU test: emit a particle with `age=0.5 lifetime=1.0
      totalFrames=10` → update CS writes `subImageIndex = 4.5`. Read
      back the subImageIndices buffer and confirm.

- [ ] 5.5 Integration test: run `demos/vfx_viewport_app/` with
      `--sprite fireball_6x6.png --frames 200 --screenshot-frame 150`
      and verify the captured frame has non-frame-0 atlas tile visible.

## 6. Documentation

- [ ] 6.1 Create `docs/engine/25-vfx/17-particle-flipbook-pipeline.md`
      documenting:
      - CPU side: `subImageIndex` SoA channel.
      - Update CS side: `normAge * (totalFrames - 1)` formula.
      - VS side: `flipbook_atlas.hlsli` API usage.
      - PS side: stateless interpolant sample.

- [ ] 6.2 Update `docs/guides/vfx-particle-material-pipeline.md` with
      the new VS-based SubUV pipeline (replacing the old PS-based doc).

- [ ] 6.3 Add `/// @ref UE5 NiagaraSpriteVertexFactory.ush` citations
      in `particle_billboard_vs.hlsl` and `flipbook_atlas.hlsli`.

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
