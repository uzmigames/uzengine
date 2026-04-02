# Phase 4c — 4.19.0 VFX Decal Renderer (Particle Death Decals)

When a particle dies (age ≥ lifespan), it can leave a persistent decal on
nearby surfaces. The death CS writes death events to a GPU event buffer; each
frame the CPU reads completed events and injects them into the existing
`DecalManager`. Engine already has `decal_manager`, `decal_pool`,
`decal_deferred_vs/ps.hlsl`, and `particle_death_spawn_cs.hlsl`.

---

## 1. Death Event Buffer

- [ ] 1.1 In `runtime/engine/vfx/core/gpu_particle_buffer.h`, declare:
      ```cpp
      /// Ring buffer for particle death events written by the update CS.
      /// CPU reads and clears this each frame (1-frame latency allowed).
      RHIBufferHandle deathEventBuffer = RHIBufferHandle::Invalid();
      RHIBufferHandle deathEventCount  = RHIBufferHandle::Invalid(); // uint32 UAV
      ```
- [ ] 1.2 Allocate `deathEventBuffer` as `STRUCTURED_BUFFER | UNORDERED_ACCESS`,
      stride = `sizeof(ParticleDeathEvent)`, count = `maxParticles`. Define:
      ```cpp
      struct ParticleDeathEvent {
          float3 position;   // 12 bytes
          float  size;       //  4 bytes
          float4 color;      // 16 bytes
          float3 normal;     // 12 bytes  (estimated surface normal)
          float  _pad;       //  4 bytes
      };                     // total: 48 bytes
      ```
- [ ] 1.3 Allocate `deathEventCount` as an unordered-access `uint32` counter
      buffer, reset to 0 each frame via `ClearUAV`.

## 2. Update CS Death Event Write

- [ ] 2.1 In `particle_update_cs.hlsl`, when a particle's age ≥ lifespan:
      ```hlsl
      RWStructuredBuffer<ParticleDeathEvent> g_deathEvents : register(u5);
      RWBuffer<uint>                          g_deathCount  : register(u6);
      cbuffer DeathDecalCB : register(b6) {
          uint  emitDeathDecals;    // 0 or 1
          float minSizeForDecal;    // only emit if size > this (default 2.0 cm)
          float2 _pad;
      };
      // Inside "particle just died" branch:
      if (emitDeathDecals && size > minSizeForDecal) {
          uint slot;
          InterlockedAdd(g_deathCount[0], 1, slot);
          if (slot < maxParticles) {
              ParticleDeathEvent ev;
              ev.position = pos;
              ev.size     = size;
              ev.color    = color;
              ev.normal   = float3(0, 0, 1); // default up; override via surface normal if available
              g_deathEvents[slot] = ev;
          }
      }
      ```
- [ ] 2.2 Add `ClearUAV` on `g_deathEventCount` at the start of each frame
      (before the update dispatch) within the render graph.

## 3. CPU Readback and Decal Injection

- [ ] 3.1 In `runtime/engine/vfx/core/sub_emitter_readback.cpp` (or create a
      new `death_event_readback.h/cpp`), add triple-buffered readback for
      `deathEventBuffer` and `deathEventCount` (same pattern as
      `SubEmitterReadback`).
- [ ] 3.2 Each frame, in `VFXSystem::ProcessDeathEvents(DecalManager& decals,
      uint64_t completedFrame)`:
      - Map the readback buffer from frame N-1.
      - Read `count = readbackCount[0]` (clamped to `maxDeathDecalsPerFrame = 64`).
      - For each event, call:
        ```cpp
        DecalDesc desc;
        desc.position    = event.position;
        desc.normal      = event.normal;
        desc.size        = Vec2(event.size * decalSizeScale);
        desc.color       = LinearToGamma(Vec3(event.color));
        desc.lifetime    = decalLifetime;
        desc.materialIdx = decalMaterialIndex;
        decals.SpawnDecal(desc);
        ```
- [x] 3.3 Add to `VFXEmitterConfig`: Added simplified `deathDecalEnabled` (uint32_t, default 0) config field to VFXEmitterConfig with serialization support. Full decal parameter set (minSizeForDecal, decalSizeScale, decalLifetime, etc.) deferred to GPU readback implementation.
      ```cpp
      bool     emitDeathDecals      = false;
      float    minSizeForDecal      = 2.0f;  // cm
      float    decalSizeScale       = 3.0f;  // decal radius = particle size * scale
      float    decalLifetime        = 10.0f; // seconds
      uint32_t decalMaterialIndex   = 0;     // index into DecalManager material list
      uint32_t maxDeathDecalsPerFrame = 64;
      ```

## 4. Decal Manager Interface Verification

- [ ] 4.1 Confirm `DecalManager::SpawnDecal(const DecalDesc&)` exists and accepts
      at minimum `position`, `normal`, `size`, `color`, `lifetime`, `materialIdx`.
      If missing any parameter, extend `DecalDesc` and update `SpawnDecal`.
- [ ] 4.2 Confirm `DecalManager` enforces a total pool limit (e.g., 256 decals)
      via LRU eviction — oldest decal is removed when the pool is full.
- [ ] 4.3 Confirm `decal_deferred_ps.hlsl` samples the decal color texture and
      applies it to G-buffer albedo correctly (deferred decal projection).

## 5. Demo Integration

- [ ] 5.1 Add preset "Explosion Scorch" in `vfx_viewport_demo.cpp`:
      short-lived bright particles (lifetime=0.8s), `emitDeathDecals=true`,
      `decalSizeScale=4.0`, `decalLifetime=20.0`, scorch material (dark brown).
- [ ] 5.2 Add preset "Blood Splatter":
      red particles, `emitDeathDecals=true`, `minSizeForDecal=1.0`,
      `decalSizeScale=2.5`, `decalLifetime=30.0`.
- [ ] 5.3 Add ImGui controls for `emitDeathDecals` (checkbox), `decalSizeScale`
      (0.5–10 slider), `decalLifetime` (1–60 slider), `maxDeathDecalsPerFrame`
      (1–128 int drag).
- [ ] 5.4 Display live decal pool count in the panel:
      `ImGui::Text("Active decals: %u", decalManager->GetActiveCount())`.

## 6. Build and Verification

- [ ] 6.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 6.2 `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 6.3 Capture "Explosion Scorch" at frame 30 (after particles have died):
      confirm dark scorch marks visible on the ground plane.
- [ ] 6.4 Spawn 5 repeated bursts and verify decal pool LRU eviction does not crash
      or corrupt when pool reaches capacity.

## 7. Documentation

- [ ] 7.1 In `particle_update_cs.hlsl`, document the death event write block:
      ```hlsl
      /// @ref Lagarde & de Rousiers 2014 "Moving Frostbite to PBR" — deferred
      ///      decal projection for particle death marks
      ```
- [ ] 7.2 Document `maxDeathDecalsPerFrame` purpose: prevents GPU→CPU readback
      from becoming a bandwidth bottleneck on dense emitters.
- [ ] 7.3 Document `normal` default (`float3(0,0,1)`): particles currently emit
      world-up normal; future improvement would ray-cast for true surface normal
      (see task 4.23.0 GPU raycasting).
