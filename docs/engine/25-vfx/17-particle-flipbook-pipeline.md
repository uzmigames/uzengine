# Particle Flipbook Pipeline

Niagara-parity SubUV / sprite-sheet animation for billboard particles.

## Data Flow

```
┌───────────────────┐  CPU preset
│ VfxEmitterConfig  │  flipbookRows, flipbookCols, flipbookTotalFrames,
│                   │  flipbookFPS, spriteTexturePath
└────────┬──────────┘
         │ scene_vfx_sync.cpp (sprite-load path)
         ▼
┌───────────────────┐  GPU SoA channel
│ subImageIndices   │  float[maxParticles] — per-particle frame position
│ (structured buf)  │  Allocated in GpuParticleBuffer::CreateAttributeSet.
└────────┬──────────┘
         │ update CS writes each frame
         ▼
┌───────────────────┐  shaders/runtime/vfx/particle_update_cs.hlsl
│ subImages[i] =    │     normAge = age/lifetime
│   normAge * (N-1) │     subImages[i] = normAge * (totalFrames - 1)
└────────┬──────────┘
         │
         ▼
┌───────────────────┐  shaders/runtime/vfx/particle_billboard_vs.hlsl
│ Billboard VS      │  Reads subImages[particleID], decomposes into
│                   │  (frameA, frameB, lerpT) via ComputeFlipbookFrames,
│                   │  pre-computes atlas UVs via AtlasUV for each of the
│                   │  quad's 4 corners. Emits TEXCOORD8 / TEXCOORD9 /
│                   │  TEXCOORD10 interpolants.
└────────┬──────────┘
         │
         ▼
┌───────────────────┐  shaders/runtime/vfx/particle_billboard_ps.hlsl
│ Billboard PS      │  Samples atlas texture twice at the VS-provided UVs
│                   │  and lerps by flipbookLerp. No per-pixel flipbook
│                   │  arithmetic. Output goes through colorLifeScale +
│                   │  channel mask + blend-mode routing.
└───────────────────┘
```

## Shared atlas helper — `flipbook_atlas.hlsli`

Single source of truth for the atlas math. Used by **both** the particle
billboard VS and the flipbook debug overlay so the two never diverge.

```hlsl
// Decompose a floating subImageIndex into (frameA, frameB, lerpT).
void ComputeFlipbookFrames(
    float  subImageIndex,
    uint   totalFrames,
    out uint frameA,
    out uint frameB,
    out float lerpT);

// Atlas UV for one quad corner of a given frame in an R×C sprite sheet.
float2 AtlasUV(
    uint   frameIndex,
    uint   rows,
    uint   cols,
    float2 cornerUV);
```

Both functions clamp `rows`/`cols`/`totalFrames` to `>= 1` to avoid
divide-by-zero on unconfigured atlases. Frame layout is row-major,
left-to-right, top-to-bottom.

## Why pre-compute in the VS

Niagara's reference implementation
(`NiagaraSpriteVertexFactory.ush`) moves the flipbook math out of the
PS and into the VS. Reasons:

1. **Rasterizer interpolates the two atlas UVs cheaply** — no need to
   evaluate `floor`/`frac`/`mod` per pixel.
2. **PS stays stateless** — the PS does not know about age, lifetime,
   FPS, or frame counts. It only does two texture samples and a lerp.
3. **Matches the UE artist model** — the `SubUV` material node evaluates
   once per vertex and feeds the texture sampler at rasterization time.

## Preset wiring

Presets configure the flipbook in `emitter_presets.h` (CPU) — example:

```cpp
e.spriteTexturePath    = "vfx/fireball_6x6.png";
e.flipbookRows         = 6;
e.flipbookCols         = 6;
e.flipbookTotalFrames  = 36;
e.flipbookFPS          = 30.0f;
e.spriteChannelMask    = 2; // Green channel only (for single-channel masks)
```

`scene_vfx_sync.cpp` loads the sprite via `VFXMaterial::LoadTexture`
(which resolves the path against `GetExeDir()` and `GetExeDir()/assets`
as fallbacks), assigns the bindless SRV index to the emitter, and wires
the CB fields. Both the single-emitter `VfxEmitterComponent` path and
the compound `VfxSystemComponent` path run the same sprite-load code.

## Persistent IDs (phase 7.4.0 — opt-in extension)

Emitters that need stable particle identity (event payload correlation
in 7.3.0, cross-emitter reads in 7.12.0, deterministic replay/debugging)
can opt in to a `uint64` `persistentIDs` SoA channel:

```cpp
ParticleEmitter e;
e.emitsPersistentIDs = true;
e.Initialize(device, budget);  // Allocates the extra channel on both sets.

// Each frame, before the spawn dispatch:
const uint64_t baseID = e.ReservePersistentIDRange(spawnThisFrame);
// Upload baseID to VFXSpawnCB — GPU computes id = baseID + threadIndex
// and writes to persistentIDs[slot].
```

Design contract:

- **Monotonic IDs.** `ReservePersistentIDRange(count)` always advances
  the counter; IDs are never reused even after slot recycle. The 64-bit
  space is effectively infinite (see
  `test_persistent_particle_ids.cpp::SixtyFourBitSpaceCoversPracticalLifetimes`).
- **0 is a reserved sentinel.** The counter starts at 1 so dead slots
  (cleared to 0 by the update CS in 7.4.1) cannot collide with live
  particles.
- **Opt-in memory cost.** Zero overhead for emitters that don't set
  `emitsPersistentIDs = true`; the dual 8-bytes/slot buffers are only
  allocated when explicitly requested.

GPU spawn-CS write path + readback query API (`VfxEmitter::QueryParticleByID`
via 4.26.0 readback framework) land in the follow-up
`phase7_7.4.1-vfx-persistent-ids-gpu-path`. The CPU API shipped in
7.4.0 is frozen — the follow-up only fills in the shader side.

## See also

- Unit tests: `runtime/tests/engine/vfx/test_flipbook_atlas.cpp`
  (`ComputeFlipbookFrames`, `AtlasUV`).
- Reference: UE5 Niagara `NiagaraSpriteVertexFactory.ush`
  (`W:\UE_5.6_Source\Engine\Plugins\FX\Niagara\...`).
- Related doc: `07-uzvfx-format.md` — preset JSON schema for flipbook
  fields.
