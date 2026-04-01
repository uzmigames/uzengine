# Phase 4.11.0 — VFX GPU Death Events

When a particle dies (age >= lifespan), the update CS writes its final state to a
`deathEventBuffer`. A second spawn CS pass reads that buffer and spawns `deathSpawnCount` new
child particles at each death position/velocity. This implements: sparks on death, secondary
debris, impact splash, smoke spawned from fire particles dying out.

The death event buffer is single-frame (reset each frame via atomic counter). Child particles
are born with a spread inherited from the parent's final velocity, scaled by a configurable
factor. The child emitter inherits the parent emitter's lifetime/size/color config but can
be overridden per-emitter.

---

## 1. Data Structures

- [ ] 1.1 Define `DeathEvent` struct in `runtime/engine/vfx/vfx_viewport_vfx.h`:
  ```cpp
  struct DeathEvent
  {
      float position[3]; // world position at death (cm)
      float _pad0;
      float velocity[3]; // velocity at death (cm/s) — used as child spawn direction
      float _pad1;
      float color[4];    // RGBA at death — tinted into child particles
  };
  static_assert(sizeof(DeathEvent) == 48, "DeathEvent must be 48 bytes");
  ```
- [ ] 1.2 Add constant `VFX_MAX_DEATH_EVENTS = 4096` (hard cap per emitter per frame).
- [ ] 1.3 Add to `VFXEmitterConfig`:
  ```cpp
  uint  deathSpawnEnabled;     // 0=off 1=on; default 0
  uint  deathSpawnCount;       // children per dead particle; default 4
  float deathSpawnSpread;      // cone half-angle (radians) for child velocity spread; default 0.5f
  float deathSpawnSpeedScale;  // child speed = parent death speed * this; default 0.3f
  ```

## 2. GPU Buffers — Death Events

- [ ] 2.1 Allocate `RWStructuredBuffer<DeathEvent> deathEventBuffer` with `VFX_MAX_DEATH_EVENTS` entries during emitter initialization, alongside existing particle buffers.
- [ ] 2.2 Allocate `RWBuffer<uint> deathEventCountBuffer` (1 uint) for atomic event counting. Reset to 0 each frame via `CopyResource` from a persistent zero-initialized upload buffer BEFORE the update dispatch.
- [ ] 2.3 Store buffer handles in the emitter struct with clear names (`deathEventBuffer`, `deathEventCountBuffer`).

## 3. Shader Modification — particle_update_cs.hlsl

- [ ] 3.1 Add `RWStructuredBuffer<DeathEvent> deathEventBuffer` and `RWBuffer<uint> deathEventCountBuffer` as UAV inputs to the update CS (bind alongside existing `particleBuffer` UAV).
- [ ] 3.2 Add to the `VFXUpdateCB` cbuffer declaration:
  ```hlsl
  uint  deathSpawnEnabled;
  uint  maxDeathEvents;      // = VFX_MAX_DEATH_EVENTS
  float deathSpawnSpread;
  float deathSpawnSpeedScale;
  ```
- [ ] 3.3 In the death detection block (where `age >= lifespan` is handled), BEFORE moving the particle to the dead list, write the death event:
  ```hlsl
  /// @ref Shopf 2008 "GPU Particles" — death event buffer for sub-emitter spawning
  if (deathSpawnEnabled && age >= lifespan)
  {
      uint slot;
      InterlockedAdd(deathEventCountBuffer[0], 1, slot);
      if (slot < maxDeathEvents)
      {
          DeathEvent evt;
          evt.position = particle.position;
          evt._pad0    = 0.0f;
          evt.velocity = particle.velocity;
          evt._pad1    = 0.0f;
          evt.color    = particle.color;
          deathEventBuffer[slot] = evt;
      }
  }
  ```
- [ ] 3.4 Confirm the write only happens on the exact frame the particle crosses the lifespan threshold (not every frame while dead).

## 4. New Compute Shader — particle_death_spawn_cs.hlsl

- [ ] 4.1 Create `runtime/shaders/vfx/particle_death_spawn_cs.hlsl` with header:
  ```hlsl
  /// @ref Shopf 2008 "GPU Particles" — death-event driven sub-emitter spawn pass
  /// @constants VFX_MAX_DEATH_EVENTS=4096, children per event = deathSpawnCount
  ```
- [ ] 4.2 Inputs: `StructuredBuffer<DeathEvent> deathEventBuffer`, `uint deathEventCount`. Outputs: `RWStructuredBuffer<Particle> particleBuffer`, `RWStructuredBuffer<uint> deadListBuffer`, `RWBuffer<uint> aliveCountBuffer`.
- [ ] 4.3 Add a `VFXDeathSpawnCB` cbuffer: `uint deathEventCount`, `uint deathSpawnCount`, `float deathSpawnSpread`, `float deathSpawnSpeedScale`, `uint maxParticles`, `float lifetime`, `float size`, `float _pad`.
- [ ] 4.4 Each thread handles one death event. For `i` in `[0, deathSpawnCount)`:
  ```hlsl
  // Derive unique direction via Halton sequence offset by particleIndex for determinism.
  // @ref Halton 1960 — low-discrepancy sequence for hemispherical spread
  float angle = (float)i / (float)deathSpawnCount * 6.28318f;
  float phi   = deathSpawnSpread * sqrt((float)i / max(deathSpawnCount - 1, 1));
  float3 spreadDir = float3(sin(phi)*cos(angle), sin(phi)*sin(angle), cos(phi));

  // Rotate spread cone to align with parent velocity direction
  float3 parentDir = normalize(evt.velocity + float3(0,0,0.001f));
  // ... Rodrigues rotation or TBN frame to align spreadDir to parentDir
  float3 childVel = parentDir * length(evt.velocity) * deathSpawnSpeedScale;
  childVel += spreadDir * length(evt.velocity) * deathSpawnSpeedScale * 0.5f;

  // Claim a slot from the dead list and initialize the child particle
  uint deadSlot;
  InterlockedAdd(/* deadListHead */, -1, deadSlot);
  if (deadSlot < maxParticles)
  {
      Particle child;
      child.position = evt.position;
      child.velocity = childVel;
      child.color    = evt.color;
      child.age      = 0.0f;
      child.lifespan = lifetime;
      child.size     = size;
      particleBuffer[deadListBuffer[deadSlot]] = child;
  }
  ```
- [ ] 4.5 Entry point named `main`.
- [ ] 4.6 Dispatch: `ceil(deathEventCount / 64)` thread groups, 64x1x1. Do NOT dispatch if `deathEventCount == 0`.

## 5. CPU — Dispatch Orchestration

- [ ] 5.1 In the per-frame VFX update function, ensure this order:
  1. Reset `deathEventCountBuffer` to zero (`CopyResource` from persistent zero buffer).
  2. Dispatch `particle_update_cs` (writes death events for this frame).
  3. Issue a UAV barrier between update CS and death spawn CS.
  4. Read `deathEventCount` from `deathEventCountBuffer` via indirect dispatch, OR dispatch with `VFX_MAX_DEATH_EVENTS / 64` thread groups and let shader discard out-of-range threads (simpler, avoids readback latency).
  5. Dispatch `particle_death_spawn_cs`.
  6. Issue another UAV barrier before billboard draw.
- [ ] 5.2 Only perform steps 3-6 if `config.deathSpawnEnabled` is true.
- [ ] 5.3 Bind `deathEventBuffer` as UAV for step 2 (update CS writes), then as SRV for step 5 (spawn CS reads). Transition resource state appropriately between the two dispatches.

## 6. UI — Simulation Category

- [ ] 6.1 In the VFX inspector, add a "Death Spawn" section (inside Simulation or a new "Sub-Emitters" category):
  ```cpp
  ImGui::SeparatorText("Death Spawn");
  ImGui::Checkbox("Enable Death Spawn", (bool*)&config.deathSpawnEnabled);
  if (config.deathSpawnEnabled)
  {
      ImGui::SliderInt("Children per Death",    (int*)&config.deathSpawnCount,     1,   32);
      ImGui::SliderFloat("Spread (rad)",         &config.deathSpawnSpread,          0.0f, 3.14f);
      ImGui::SliderFloat("Speed Scale",          &config.deathSpawnSpeedScale,      0.0f, 2.0f);
  }
  ```
- [ ] 6.2 Add tooltip: `"When particles die, spawn child particles at the death position. Use for sparks, debris, or secondary smoke."`.

## 7. Visual Verification

- [ ] 7.1 Build runtime then demo.
- [ ] 7.2 Set up an emitter with short lifetime (1s), `deathSpawnEnabled=1`, `deathSpawnCount=8`, `deathSpawnSpread=0.8`, `deathSpawnSpeedScale=0.4`. Capture at frame 90 (after first wave of deaths):
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 90 --frames 95
  ```
- [ ] 7.3 Visually verify: small child particles burst outward from positions where parent particles die. No particles clumping at origin.
- [ ] 7.4 Verify `deathSpawnEnabled=0` — no extra particles appear, alive count matches emitter spawn rate exactly.
- [ ] 7.5 Verify no overflow: set `deathSpawnCount=32` with 500 parent particles dying simultaneously — alive count must not exceed `maxParticles` (dead list clamps).

## 8. Documentation

- [ ] 8.1 Add `/// @ref Shopf 2008 "GPU Particles"` citation at the top of `particle_death_spawn_cs.hlsl` and above the death event write block in `particle_update_cs.hlsl`.
- [ ] 8.2 Update VFX guide with a "GPU Death Events / Sub-Emitters" section explaining: the single-frame event buffer design, the UAV barrier requirement between update and spawn passes, the dead-list consumption strategy, and common use cases (sparks, debris, chain explosions).
