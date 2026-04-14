# VFX Particle Flipbook — Root Cause Investigation (Phase 1.6)

**Task:** `phase3b_3.91.0-vfx-particle-flipbook-rendering-pipeline-fix`
**Date:** 2026-04-13
**Status:** Investigation in progress — probe shaders instrumented, awaiting on-device screenshot.

---

## 1. Confirmed facts (baseline)

- The `FlipbookDebugPass` (`frame_overlay.cpp:72`) renders a quad over the
  backbuffer using the SAME atlas SRV and the SAME `s2` linearClamp sampler
  as the particle PS. The quad cycles through all atlas frames correctly.
- The particle billboard `VFXBillboardCB` at b2 reaches the PS with correct
  values (earlier probe: `flipbookTotalFrames/40`, `flipbookRows/10` showed
  R=0.9, G=0.6).
- Hardcoding `subImgIdx = 15.0f` in the particle VS and computing
  `flipbookUV0 = (col3, row2, stepSize)` per-vertex still renders frame 0.
  → The `TEXCOORD8/9/10` interpolants are suspect.

The `--flipbook-debug` quad vs particle path differ only in the PS/VS that
run on each draw — the global bindless root signature
(`dx12_pso_creation.cpp:172 CreateBindlessRootSignature`) is shared.

---

## 2. PSO / root-signature divergence (side-by-side)

Both PSOs use the single global `CreateBindlessRootSignature` — all PSOs
in UzEngine share one root signature with 4 static samplers (s0 ANISO,
s1 SHADOW_COMPARE, s2 LINEAR_CLAMP, s3 LINEAR_REPEAT) and the slot layout
in `root_slots.h`:

| Slot | Register | Kind |
|------|----------|------|
| 0    | b0       | 16× uint32 root constants |
| 1    | b1       | Root CBV (PerFrameCB)     |
| 2    | b2       | Root CBV (per-object)     |
| 3    | b4       | Root CBV (shadow)         |
| 5    | b3       | Root CBV (material)       |

### 2.1 FlipbookDebugPass (`frame_overlay.cpp:106..128`)

- `SetPipeline(GetFlipbookDebugPSO)`
- `SetRootConstants(kSlotRootConstants /*=0*/, rootConstants, 8)` — 8 DWORDs
  into b0 matching `FlipbookDebugCB` (32 bytes, including 2 pads).
- No binding of b1 or b2.
- `Draw(6)` — 2 triangles from `SV_VertexID`.
- Shader reads `register(b0)` (the root-constants slot) directly as a
  `cbuffer`. HLSL allows this because the root signature exposes slot 0
  as root constants bound to b0.

### 2.2 ParticleBillboard path (`particle_render_pass.cpp:85..113, 200..211`)

- `SetPipeline(billboardPipeline)` — either
  `GetParticleBillboard`, `GetParticleBillboardAdditive`, or
  `GetParticleBillboardPremulAlpha` (all use the same VS/PS).
- `SetRootCBV(kSlotPerFrame /*=1*/, passDesc.perFrameCB.gpuAddr)` — b1
  PerFrameCB.
- Per emitter:
  - `SetRootConstants(kSlotBindlessIndices /*=0*/, indicesCB, 12)` — 12
    DWORDs (48 B, `ParticleBillboardIndices`) into b0.
  - `SetRootCBV(kSlotPerEmitter /*=2*/, emitter.renderCB.gpuAddr)` — b2
    `VFXBillboardCB` (208 B including all flipbook rows/cols/frames/FPS).
- `Draw(emitter.cpuVertexCount)` — 6 verts per live particle, indexed via
  `SV_VertexID / 6`.

### 2.3 Shader register declarations

`particle_billboard_vs.hlsl`:
```
cbuffer VFXBillboardConstants : register(b0) { ...indices... }
cbuffer PerFrameCB            : register(b1) { ...camera... }
cbuffer VFXBillboardCB        : register(b2) { invView, globalSizeMultiplier, ... }
StructuredBuffer<float>  particleAges      : register(t5);
StructuredBuffer<float>  particleLifespans : register(t6);
```

`particle_billboard_ps.hlsl`: same three `cbuffer` declarations at
b0/b1/b2.

### 2.4 PSO state diff — ParticleBillboard vs FlipbookDebug

| Dimension                 | ParticleBillboard                    | FlipbookDebug                     | Divergence significant? |
|---------------------------|--------------------------------------|-----------------------------------|-------------------------|
| Root signature            | Global bindless                      | Global bindless                   | No — identical          |
| Static samplers (s0..s3)  | 4 (inherited)                        | 4 (inherited)                     | No                      |
| VS input layout           | None (SV_VertexID)                   | None (SV_VertexID)                | No                      |
| Cull mode                 | None                                 | None                              | No                      |
| Topology                  | TriangleList                         | TriangleList                      | No                      |
| Depth enable              | **true** (GreaterEqual, readonly)    | **false**                         | Yes — could cull        |
| Depth write               | false                                | false                             | No                      |
| Blend                     | SrcAlpha/InvSrcAlpha (or additive)   | Disabled                          | Yes — wrong alpha path  |
| Color RT format           | **RGBA16Float** (HDR)                | **RGBA8Unorm** (backbuffer)       | Yes — color encoding    |
| Depth format              | D32Float                             | Unknown                           | Yes                     |
| Interpolants used (VS→PS) | **11** (TEXCOORD0..10 + COLOR0 + SV_Position) | 1 (TEXCOORD0)          | Potentially significant |
| CB slots bound            | b0 (12 DWORDs), b1 (root CBV), b2 (root CBV) | b0 (8 DWORDs) only        | More pressure, but legal |

**Key takeaway:** No root-signature divergence. The runtime binding order
for the particle path is correct (b0 root constants → b1 CBV → b2 CBV).
The HLSL register declarations match the C++ slot mapping.

---

## 3. Current hypothesis

After hardcoding `subImgIdx=15` in the VS and writing per-vertex
`flipbookUV0 = (col3, row2, …)`, particles still show frame 0. There are
three possible stages where the signal is lost:

**(A) VS output silently dropped** — if DXC's DXIL packer decides the
TEXCOORD8/9/10 outputs are unused (because the old PS was reading
`flipbookUV0.xy` but also reading a hidden path via a `cbuffer` branch),
linker dead-code elimination could strip them across VS/PS. This is
extremely rare with explicit TEXCOORD semantics but has been observed on
AMD drivers when the PS has conditional reads behind `if (flipbookTotalFrames > 1u)`.

**(B) PS reading different TEXCOORDs than the VS outputs** — an older
version of the PS struct may still be in the shader binary cache
(`build/bin/cache/shaders/dx12_sm68/*.bin`) with a different TEXCOORD
layout. If one side was compiled with `flipbookUV0` at TEXCOORD8 and the
other side at a different slot, interpolation pipes garbage. We cleared
the cache as part of this step.

**(C) VS early-out taken every frame** — the cull guard
`if (particleLife <= 0.0f || particleAge >= particleLife)` at VS line 429
writes `output.flipbookUV0 = (0,0)` and returns. If the SoA buffer
`particleLifespans` is reading 0 for all lanes (misbinding of `t6`), every
particle hits the early-out branch and gets UV=(0,0) = frame 0.

The `debugOut` probe we just added resolves all three at once:

- If particles render with `R = age (varies), G = life (non-zero), B = 15/255 = 0.0588`:
  VS→PS interpolants work; bug is in `flipbookUV0` math or CB.
- If particles render **solid black** (`R=G=B=0`):
  VS is taking the early-out branch — `particleLifespans`/`particleAges`
  SRV is not bound or reads zero. Confirms hypothesis (C).
- If particles render **solid magenta** or **garbage**:
  PS received something it didn't expect — DXIL linker stripped
  `debugOut` or TEXCOORD11 collides. Confirms hypothesis (A)/(B).

The most likely outcome (given B=15 is a literal constant that must
survive any VS→PS link): **hypothesis (C)** — the early-out is firing,
so `flipbookUV0 = (0,0)` and everything renders frame 0. This would
explain why hardcoding `subImgIdx=15` in the VS main body has no effect:
the code never reaches that point.

---

## 4. Proposed fix (do NOT implement yet — awaiting probe result)

Depending on the probe result:

- **(C) confirmed (black particles):** the SoA `ages`/`lifespans` SRV
  index in `ParticleBillboardIndices` (b0, 12 DWORDs) is stale or pointing
  at a zero buffer. Verify `desc.emitters[i].indicesCB.cpuPtr` is packed
  correctly and that the ECS→GPU upload for the age/life buffers is
  actually happening on the same frame we draw. (Tasks 2.3–2.5 cover
  re-architecting to a dedicated `subImageIndex` SoA channel that avoids
  the cull-branch problem entirely.)
- **(A)/(B) confirmed (magenta/garbage):** force `[[flatten]]` on the
  flipbook branch in the PS, or explicitly tag `nointerpolation` on
  `flipbookUV0/1/Lerp`. Additionally enforce a mandatory shader cache
  rebuild on VS/PS struct hash change (hash the TEXCOORD layout into the
  cache key).
- **(expected — B=0.0588 visible):** the interpolants are fine and the
  VS reached the final `debugOut` assignment; then the only remaining
  suspect is the `flipbookUV0` computation itself (integer `row`/`col`
  division vs float UV domain) — pursue Phase 2 re-implementation with
  `flipbook_atlas.hlsli` as planned.

---

## 5. How to run the probe

1. Clear the shader binary cache (already done):
   `rm -f runtime/demos/vfx_viewport_app/build/bin/cache/shaders/dx12_sm68/*.bin`
2. Close any running `vfx_viewport_app.exe` so the next `zig build` can
   install the new exe (currently failing with AccessDenied when another
   instance is running).
3. Rebuild:
   ```
   cd runtime && zig build
   cd demos/vfx_viewport_app && zig build
   ```
4. Run the demo with a flipbook preset (e.g., fire with
   `fireball_6x6.png`) and observe the particle color:
   - **Varying red, visible green, faint blue (≈15/255 = very dark blue):**
     VS→PS interpolants work; bug is downstream in atlas math.
   - **Solid black:** early-out is firing; ages/lifespans SoA is empty.
   - **Magenta / noise / wrong constant values:** interpolant slot
     corruption; investigate TEXCOORD11 collision or stale PSO cache.
5. Capture a screenshot via `--screenshot-stdout --frames 60` and read
   it; record the observed RGB values next to the probe outcome.

After diagnosis, **revert the probe** (remove `debugOut` field and the
`return float4(input.debugOut.xyz, 1.0)` at the top of PS main) before
moving to Phase 2.

---

## 6. Files instrumented (revert before Phase 2)

- `runtime/shaders/runtime/vfx/particle_billboard_vs.hlsl`
  - Added `float4 debugOut : TEXCOORD11` to `VSOutput`.
  - Initialized `debugOut = (0,0,0,0)` in cull-branch early-out.
  - Set `debugOut = float4(particleAge, particleLife, 15.0f, 0.5f)` at
    end of main before `return output`.
- `runtime/shaders/runtime/vfx/particle_billboard_ps.hlsl`
  - Added `float4 debugOut : TEXCOORD11` to `PSInput`.
  - Inserted `return float4(input.debugOut.xyz, 1.0);` as first statement
    of `main()` (skips all downstream flipbook / channel mask / blend
    logic).
