# Phase 4.10.0 — VFX Particle Lights

Each VFX emitter can contribute up to 8 dynamic point lights driven by its brightest live
particles. A dedicated light-extraction compute pass scans all alive particles, selects the
top-N by `emissive * alpha * lightIntensity`, and writes their positions/colors to a readback
buffer. The CPU reads this buffer with one frame of latency and injects the results into the
scene light list via `LightRenderModule`.

Architecture: update CS runs first → light extraction CS runs second → CPU reads previous
frame's results and registers point lights → billboard draw call runs → frame ends.

---

## 1. Data Structures

- [ ] 1.1 Define `ParticleLightEntry` struct in `runtime/engine/vfx/vfx_viewport_vfx.h`:
  ```cpp
  struct ParticleLightEntry
  {
      float position[3]; // world position (cm)
      float radius;      // light radius (cm); filled from emitter config
      float color[3];    // RGB tint * particle color * emissive
      float intensity;   // luminance contribution; filled from emitter config
  };
  static_assert(sizeof(ParticleLightEntry) == 32, "ParticleLightEntry must be 32 bytes");
  ```
- [ ] 1.2 Add constants: `VFX_MAX_PARTICLE_LIGHTS = 8` (per emitter, 8 entries per extraction pass).
- [ ] 1.3 Add to `VFXEmitterConfig`:
  ```cpp
  uint  particleLightsEnabled; // 0=off 1=on; default 0
  float lightRadius;           // point light radius in cm; default 200.0f
  float lightIntensity;        // candela; default 1000.0f
  float _lightPad;
  ```

## 2. GPU Buffer — Light Extraction

- [ ] 2.1 Allocate a `StructuredBuffer<ParticleLightEntry>` with `VFX_MAX_PARTICLE_LIGHTS` entries on the GPU, and a matching CPU-readable readback buffer (triple-buffered for no stalls). Do this in the VFX system initialization, alongside existing particle buffers.
- [ ] 2.2 Allocate an atomic uint counter buffer (`lightCountBuffer`) — reset to 0 at the start of each extraction pass via `CopyResource` from a zero-initialized upload buffer.
- [ ] 2.3 Store buffer handles in the emitter/system struct alongside existing `particleBuffer`, `deadListBuffer`, etc.

## 3. New Compute Shader — particle_light_extract_cs.hlsl

- [ ] 3.1 Create `runtime/shaders/vfx/particle_light_extract_cs.hlsl` with header:
  ```hlsl
  /// @ref Drobot 2017 "Improved Culling for Tiled and Clustered Rendering" — per-particle light extraction
  /// @constants VFX_MAX_PARTICLE_LIGHTS=8, thread group 64x1x1
  ```
- [ ] 3.2 Inputs: `StructuredBuffer<Particle> particleBuffer`, `uint particleCount`. Outputs: `RWStructuredBuffer<ParticleLightEntry> lightBuffer`, `RWBuffer<uint> lightCountBuffer`.
- [ ] 3.3 Add a `VFXLightExtractCB` cbuffer with: `uint particleCount`, `float lightRadius`, `float lightIntensity`, `uint maxLights = VFX_MAX_PARTICLE_LIGHTS`.
- [ ] 3.4 Each thread checks one particle. If alive and `emissive * alpha > threshold (0.01)`:
  ```hlsl
  uint slot;
  InterlockedAdd(lightCountBuffer[0], 1, slot);
  if (slot < maxLights)
  {
      ParticleLightEntry e;
      e.position  = particle.position;
      e.radius    = lightRadius;
      e.color     = particle.color.rgb * particle.emissive;
      e.intensity = lightIntensity * particle.color.a;
      lightBuffer[slot] = e;
  }
  ```
- [ ] 3.5 Entry point named `main` (mandatory per shader-entry-point rule).

## 4. CPU — Extraction Pass Dispatch

- [ ] 4.1 In `LightRenderModule` (or wherever VFX is driven per frame), after the update CS dispatch and before the billboard draw, dispatch `particle_light_extract_cs` with `ceil(particleCount / 64)` thread groups.
- [ ] 4.2 Issue a `CopyResource` from the GPU light buffer to the readback buffer for this frame's results (reading N-1 frame's results, writing N frame's results).
- [ ] 4.3 Only dispatch if `config.particleLightsEnabled` is true; skip entirely otherwise (no zero-count dispatch).

## 5. CPU — Readback and Light Injection

- [ ] 5.1 Each frame (before the lighting pass), map the previous frame's readback buffer.
- [ ] 5.2 Read `min(lightCount, VFX_MAX_PARTICLE_LIGHTS)` entries from the buffer.
- [ ] 5.3 For each entry, call the existing point-light registration function (search for where `LightRenderModule` or `PerFrameCB` point lights are registered) with: `position`, `radius`, `color * intensity`.
- [ ] 5.4 Unmap the readback buffer.
- [ ] 5.5 Ensure readback buffer is triple-buffered — use `frameIndex % 3` to select the correct readback slot. Never read a buffer the GPU is still writing.

## 6. UI — Lighting Category

- [ ] 6.1 In the VFX inspector, add a "Lighting" collapsing header:
  ```cpp
  ImGui::SeparatorText("Particle Lights");
  ImGui::Checkbox("Enable Particle Lights", (bool*)&config.particleLightsEnabled);
  if (config.particleLightsEnabled)
  {
      ImGui::SliderFloat("Light Radius (cm)",  &config.lightRadius,    0.0f, 2000.0f);
      ImGui::SliderFloat("Light Intensity",    &config.lightIntensity, 0.0f, 10000.0f);
  }
  ```
- [ ] 6.2 Add tooltip: `"Each frame, the 8 brightest particles inject dynamic point lights into the scene. 1-frame latency due to GPU readback."`.

## 7. Visual Verification

- [ ] 7.1 Build runtime then demo.
- [ ] 7.2 Place a fire emitter near a grey wall. Enable particle lights with `lightRadius=300, lightIntensity=3000`. Capture at frame 30:
  ```
  vfx_demo.exe --screenshot-stdout --screenshot-frame 30 --frames 35
  ```
- [ ] 7.3 Visually verify: the wall receives warm orange/yellow illumination flickering with the particles. No illumination when `particleLightsEnabled = 0`.
- [ ] 7.4 Verify no GPU device removal or validation errors in the log (`UzEngine/logs/` directory).

## 8. Documentation

- [ ] 8.1 Add `/// @ref Drobot 2017 "Improved Culling for Tiled and Clustered Rendering"` citation at the top of `particle_light_extract_cs.hlsl`.
- [ ] 8.2 Update VFX guide with a "Particle Lights" section explaining the triple-buffered readback design, the 1-frame latency, and the 8-light limit per emitter.
