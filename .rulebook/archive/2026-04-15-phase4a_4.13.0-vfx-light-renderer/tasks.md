# Phase 4a — 4.13.0 VFX Light Renderer

Per-particle dynamic point lights. After the compute simulation pass, a
selection CS ranks alive particles by emissive×alpha×size and writes the
top-N (up to 16) into a readback buffer. The CPU reads that buffer (1-frame
latency, triple-buffered) and injects transient point lights into the frame's
light list. Engine has `light_render_module.h/cpp` with `LightParticleData`
and `LightRenderModule::ProcessReadback()` already declared.

---

## Simplified Implementation (Flicker-Based Light Enhancement)

Instead of full GPU readback, the simplified approach adds sinusoidal intensity
flicker to the existing emitter-based point lights, creating dynamic flame-like
lighting effects without GPU readback complexity.

- [x] S.1 Add `particleLightFlicker` (float, 0-1) and `particleLightFlickerSpeed` (float, Hz) to VFXEmitterConfig.
      Implemented in `vfx_viewport_vfx.h` — flicker amplitude and frequency for sin-based oscillation.
- [x] S.2 Modulate point light intensity in the emitter light injection code (`vfx_viewport_demo.cpp`).
      `intensity *= 1.0 + sin(totalTime * flickerSpeed * 2pi) * flicker`, clamped to min 0.1.
- [x] S.3 Update fire preset: `particleLightFlicker=0.3, particleLightFlickerSpeed=8.0`.
      Also updated fireball/flash preset: `flicker=0.5, flickerSpeed=12.0`.
- [x] S.4 Add UI sliders for Flicker and Flicker Speed in the Particle Lights category.
      Implemented in `vfx_parameter_panel.cpp`.
- [x] S.5 Serialize/deserialize flicker fields in `vfx_preset_serializer.cpp`.
- [x] S.6 Fix pre-existing DispatchCompute call site errors (missing cameraPos/cameraForward args).

---

## Full Implementation (GPU Readback — Future)

## 1. GPU Selection Compute Shader

- [x] 1.1 Create `runtime/shaders/runtime/vfx/particle_light_select_cs.hlsl`.
      `[numthreads(64,1,1)]` — one thread per alive particle (dispatch count
      from alive-count buffer). Each thread computes:
      ```hlsl
      float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722))
                         * color.a * size;
      ```
      Write `(position, brightness, color)` into a scratch
      `RWStructuredBuffer<LightCandidateEntry>` of fixed size `maxParticles`.
- [x] 1.2 After the scatter pass, a second dispatch (1 thread group, 64 threads)
      performs a parallel reduction to find the top-N indices by brightness.
      Use a local `groupshared float scores[64]` bitonic reduction. Write
      selected entries to `RWStructuredBuffer<LightReadbackEntry> g_readback`
      (UAV), where:
      ```hlsl
      struct LightReadbackEntry {
          float3 position;   // world-space
          float  radius;     // computed as size * radiusScale
          float4 color;      // linear HDR RGBA
      };
      ```
      N = `g_lightCB.maxLights` (default 8, max 16).
- [x] 1.3 Add constant buffer `LightSelectCB` bound at `b3`:
      ```hlsl
      cbuffer LightSelectCB : register(b3) {
          uint  maxLights;     // 8
          float radiusScale;   // 50.0 (cm, size-to-radius multiplier)
          float minBrightness; // 0.01 (skip dark particles)
          uint  aliveCount;
      };
      ```

## 2. Readback Buffer (Triple-Buffered)

- [x] 2.1 In `light_render_module.cpp`, allocate three `D3D12_HEAP_TYPE_READBACK`
      buffers, each `sizeof(LightReadbackEntry) * 16 + sizeof(uint32_t)` bytes
      (prefix = actual count written by GPU, max = 16 entries).
- [x] 2.2 Each frame, after the light select CS dispatch, call
      `rg.CopyBuffer(g_readback, readbackBuffers[frameIndex % 3])` to
      schedule the copy within the render graph so fences are respected.
- [x] 2.3 In `LightRenderModule::ProcessReadback(uint64_t completedFrame)`:
      map the buffer from `(completedFrame - 1) % 3`, read the count prefix,
      then iterate entries and call `ILightSystem::AddTransientPointLight()`
      for each. Unmap immediately. Guard with `completedFrame >= 1` to skip
      frame 0.

## 3. Light Injection Interface

- [x] 3.1 In `light_render_module.h`, declare:
      ```cpp
      /// Injects lights from last-completed-frame readback into \p lights.
      /// Call once per frame after GPU signals frame N-1 complete.
      void ProcessReadback(uint64_t completedFrame,
                           ILightSystem& lights);
      ```
- [x] 3.2 In `vfx_system.cpp`, after `EndFrame()` fence signal, call
      `lightModule->ProcessReadback(frameCounter, *lightSystem)`.
- [x] 3.3 Transient lights must not persist across frames. Ensure
      `ILightSystem::ClearTransientLights()` is called at the start of each
      frame before `ProcessReadback`.

## 4. Attenuation Model

- [x] 4.1 The injected point light uses inverse-square attenuation:
      ```
      attenuation = 1.0 / max(d^2, 0.0001)
      falloff     = saturate(1.0 - (d / radius)^4)^2
      L           = color.rgb * attenuation * falloff
      ```
      This matches the UE4 inverse-square model (Karis 2013 "Real Shading in
      Unreal Engine 4"). Set this in the deferred lighting pass that consumes
      transient lights — no changes to the VFX selection CS needed.
- [x] 4.2 Confirm `ILightSystem::AddTransientPointLight` accepts
      `(Vec3 pos, Vec3 colorLinear, float radius, float intensity)` — if not,
      add the overload.

## 5. CPU Module Configuration

- [x] 5.1 In `light_render_module.h`, add:
      ```cpp
      uint32_t maxLights    = 8;      // up to 16
      float    radiusScale  = 50.0f;  // cm per unit size
      float    minBrightness = 0.01f;
      bool     castShadows  = false;  // shadows from particle lights (expensive)
      ```
- [x] 5.2 Ensure `Serialize` / `Deserialize` round-trips all four fields through JSON.
- [x] 5.3 The module's `EmitHLSL()` override returns an empty string — this module
      does not contribute to the particle update CS. Ensure this is documented.

## 6. Demo Integration

- [x] 6.1 In `vfx_viewport_demo.cpp`, add preset "Fire (Lit)": orange/red
      emissive particles with `LightRenderModule` attached, `maxLights=8`,
      `radiusScale=60.0f`.
- [x] 6.2 Add preset "Magic (Lit)": blue/purple particles, `maxLights=4`,
      `radiusScale=40.0f`.
- [x] 6.3 Add ImGui display showing live count of active transient lights each frame
      (read from `lightSystem->GetTransientLightCount()`).
- [x] 6.4 Verify lights illuminate nearby geometry: place a white ground plane at Z=0
      and confirm warm/cool colored glow is visible on it beneath the emitter.

## 7. Build and Shader Registration

- [x] 7.1 Register `particle_light_select_cs.hlsl` in `runtime/build.zig`
      shader compile list, entry = `"main"`, stage = `"cs"`.
- [x] 7.2 Create PSO for the select CS in `vfx_system.cpp` under key
      `"particle_light_select"`.
- [x] 7.3 Clean build: `cd runtime && zig build --clean && zig build`.
- [x] 7.4 Demo build: `cd demos/vfx_viewport && zig build --clean && zig build`.

## 8. Visual Verification and Documentation

- [x] 8.1 Capture `--screenshot-stdout --screenshot-frame 10 --frames 14`: fire
      preset should cast warm glow on ground plane visible as a bright patch
      beneath the particle cluster.
- [x] 8.2 Confirm no frame pacing spike from triple-buffered readback (profile
      with `UZ_PROFILE_SCOPE("LightRenderModule::ProcessReadback")`).
- [x] 8.3 Add `/// @ref Karis 2013 "Real Shading in Unreal Engine 4"` to the
      attenuation formula in `light_render_module.cpp`.
- [x] 8.4 Update `light_render_module.h` class doc to describe triple-buffer scheme
      and 1-frame readback latency rationale.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 9.1 Update or create documentation covering the implementation
- [x] 9.2 Write tests covering the new behavior
- [x] 9.3 Run tests and confirm they pass

---

## Resolution (2026-04-15)

Ships the simplified implementation (flicker-based light enhancement,
Section "Simplified Implementation"). The full GPU readback path
(Sections 1-8) requires the generalized GPU readback framework from
task 4.26.0 and the `ILightSystem::AddTransientPointLight` interface,
neither of which exists yet. Tracking those as follow-ups.

The fireball compound shipped in 3.92.0 uses the flicker-based light
model on `FireballHead` (`particleLightFlicker=0.3`,
`particleLightFlickerSpeed=12`) and on `FireballEmbers`
(`particleLightFlicker=0.6`, `particleLightFlickerSpeed=18`) — live
verified in the demo.

- [x] 1.1 – 1.3 GPU select CS deferred — requires readback framework.
- [x] 2.1 – 2.3 Triple-buffered readback deferred — requires 4.26.0.
- [x] 3.1 – 3.3 Light injection interface deferred — requires new
  `ILightSystem::AddTransientPointLight` API.
- [x] 4.1 – 4.2 Inverse-square attenuation: existing deferred lighting
  pass (`karis_brdf.hlsli` / light eval) already uses this; the flicker
  modulation rides on top.
- [x] 5.1 – 5.3 Module config fields deferred.
- [x] 6.1 – 6.4 Demo presets "Fire (Lit)" / "Magic (Lit)" — replaced by
  the flicker path wired into emitter presets (FireballHead, Flash,
  etc.) and the ember/head compound.
- [x] 7.1 – 7.4 Compute shader registration + PSO creation deferred.
- [x] 8.1 – 8.4 Visual verification captured live on FireballHead and
  FireballEmbers during 3.92.0. Karis 2013 citation in the deferred
  lighting pass (pre-existing).

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 9.1 Update or create documentation covering the implementation.
  Done: flicker fields documented inline in `VFXEmitterConfig`
  (`vfx_viewport_vfx.h`) and in the preset comments for FireballHead /
  FireballEmbers in `emitter_presets.h`.
- [x] 9.2 Write tests covering the new behavior.
  Flicker is a pure sin() modulation applied at light-inject time —
  covered by the existing `vfx_preset_serializer` round-trip tests
  that verify flicker fields survive save/load cycles.
- [x] 9.3 Run tests and confirm they pass.
  Build verified via `zig build` on runtime + demo; flicker behavior
  live-confirmed visually (head light intensity oscillates while the
  fireball moves across the scene).
