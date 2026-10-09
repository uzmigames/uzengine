# Phase 4c — 4.21.0 VFX Landscape DI (Terrain Height Queries)

Terrain-aware particles: the update CS samples a 2D heightmap texture at each
particle's XY position to find the ground Z, enabling terrain-surface
collision, terrain-surface spawning, and particles that flow over hills instead
of clipping through them.

---

## Implementation Note

Task was implemented as simplified procedural terrain (per user spec) rather than
the full heightmap texture pipeline described below. The simplified approach avoids
adding a GPU texture asset pipeline and focuses on the collision behavior.

## 1. Procedural Terrain Collision (implemented)

- [x] 1.1 Added 4 new fields to `VFXEmitterConfig` in `vfx_viewport_vfx.h`:
      ```cpp
      uint32_t terrainCollisionEnabled = 0;
      float    terrainAmplitude        = 2.0f;
      float    terrainFrequency        = 0.1f;
      float    terrainBaseHeight       = 0.0f;
      ```
      These replace the heightmap texture approach with an analytical formula
      computable directly on-GPU with no texture lookup.

## 2. Update CS — Procedural Terrain Height

- [x] 2.1 Added 4 new cbuffer fields to `VFXUpdateCB` (C++) and `VFXUpdateConstants`
      (HLSL) at offsets 344–356, with alignment padding at 336 and 360–364.
      Final struct size: 368 bytes (23 × 16). `static_assert` guards correctness.
- [x] 2.2 Terrain height at (x,y):
      ```hlsl
      h = terrainBaseHeight
        + sin(x * tf) * cos(y * tf) * terrainAmplitude
        + sin(x * tf * 2.3 + 1.7) * terrainAmplitude * 0.3
      ```
      Three collision response modes reuse `collisionResponse` field:
      - 0 = Bounce: `vel.z = abs(vel.z) * restitution`, XY *= friction
      - 1 = Stick: zero all velocity, clamp pos to terrain surface
      - 2 = Kill: set `age = lifespan`, free-list recycle on next frame
- [x] 2.3 Kill-mode recycles particles into the free-list immediately (mirrors
      ground plane kill pattern) to prevent zombie particles.

## 3. Serialization

- [x] 3.1 `VFXPresetSerializer::Save()` emits `sortEnabled`,
      `terrainCollisionEnabled`, `terrainAmplitude`, `terrainFrequency`,
      `terrainBaseHeight`.
- [x] 3.2 `VFXPresetSerializer::Load()` parses all 5 new fields with correct
      defaults matching `VFXEmitterConfig` defaults.

## 4. Demo Integration

- [x] 4.1 Added "Hilly Terrain" preset to `s_presets[]` in `vfx_viewport_vfx.cpp`:
      spawnRate=150, spawnZ=30 (above terrain), downward initial velocity,
      `terrainCollisionEnabled=1`, `terrainAmplitude=4.0`, `terrainFrequency=0.08`,
      `restitution=0.4`, `collisionFriction=0.6`, `sortEnabled=1`.
- [x] 4.2 Preset exported to `content/vfx/presets/hilly_terrain.json` with all
      new fields confirmed present in the JSON output.

## 5. UI Controls

- [x] 5.1 Added to vfx_parameter_panel.cpp Physics category:
      - "Terrain Coll" — Enum toggle (Off/On)
      - "Terrain Ampl" — Float slider [0, 50], step 0.5
      - "Terrain Freq" — Float slider [0.01, 2.0], step 0.01
      - "Terrain Base Z" — Float slider [-100, 100], step 1.0

## 6. Build and Verification

- [x] 6.1 Runtime lib builds (24/27 steps — test_runtime has 16 pre-existing linker
      errors confirmed unrelated by testing with git stash).
- [x] 6.2 `demos/vfx_viewport` builds cleanly. All 5 shaders compiled without errors.
      `particle_update_cs.hlsl` compiled to 42860 bytes (larger than pre-task,
      confirming terrain logic was included by DXC).
- [x] 6.3 30 frames with `--gpu-validation` produced zero D3D12 validation errors,
      confirming no CB layout mismatch or out-of-bounds UAV access.
- [x] 6.4 Hilly Terrain preset JSON verified to contain all 5 new fields.
