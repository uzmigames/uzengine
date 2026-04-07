# Phase 4a — 4.13.0 VFX Light Renderer

Per-particle dynamic point lights. After the compute simulation pass, a
selection CS ranks alive particles by emissive×alpha×size and writes the
top-N (up to 16) into a readback buffer. The CPU reads that buffer (1-frame
latency, triple-buffered) and injects transient point lights into the frame's
light list. Engine has `light_render_module.h/cpp` with `LightParticleData`
and `LightRenderModule::ProcessReadback()` already declared.

---

## 1. GPU Selection Compute Shader

- [ ] 1.1 Create `runtime/shaders/runtime/vfx/particle_light_select_cs.hlsl`.
      `[numthreads(64,1,1)]` — one thread per alive particle (dispatch count
      from alive-count buffer). Each thread computes:
      ```hlsl
      float brightness = dot(color.rgb, float3(0.2126, 0.7152, 0.0722))
                         * color.a * size;
      ```
      Write `(position, brightness, color)` into a scratch
      `RWStructuredBuffer<LightCandidateEntry>` of fixed size `maxParticles`.
- [ ] 1.2 After the scatter pass, a second dispatch (1 thread group, 64 threads)
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
- [ ] 1.3 Add constant buffer `LightSelectCB` bound at `b3`:
      ```hlsl
      cbuffer LightSelectCB : register(b3) {
          uint  maxLights;     // 8
          float radiusScale;   // 50.0 (cm, size-to-radius multiplier)
          float minBrightness; // 0.01 (skip dark particles)
          uint  aliveCount;
      };
      ```

## 2. Readback Buffer (Triple-Buffered)

- [ ] 2.1 In `light_render_module.cpp`, allocate three `D3D12_HEAP_TYPE_READBACK`
      buffers, each `sizeof(LightReadbackEntry) * 16 + sizeof(uint32_t)` bytes
      (prefix = actual count written by GPU, max = 16 entries).
- [ ] 2.2 Each frame, after the light select CS dispatch, call
      `rg.CopyBuffer(g_readback, readbackBuffers[frameIndex % 3])` to
      schedule the copy within the render graph so fences are respected.
- [ ] 2.3 In `LightRenderModule::ProcessReadback(uint64_t completedFrame)`:
      map the buffer from `(completedFrame - 1) % 3`, read the count prefix,
      then iterate entries and call `ILightSystem::AddTransientPointLight()`
      for each. Unmap immediately. Guard with `completedFrame >= 1` to skip
      frame 0.

## 3. Light Injection Interface

- [ ] 3.1 In `light_render_module.h`, declare:
      ```cpp
      /// Injects lights from last-completed-frame readback into \p lights.
      /// Call once per frame after GPU signals frame N-1 complete.
      void ProcessReadback(uint64_t completedFrame,
                           ILightSystem& lights);
      ```
- [ ] 3.2 In `vfx_system.cpp`, after `EndFrame()` fence signal, call
      `lightModule->ProcessReadback(frameCounter, *lightSystem)`.
- [ ] 3.3 Transient lights must not persist across frames. Ensure
      `ILightSystem::ClearTransientLights()` is called at the start of each
      frame before `ProcessReadback`.

## 4. Attenuation Model

- [ ] 4.1 The injected point light uses inverse-square attenuation:
      ```
      attenuation = 1.0 / max(d^2, 0.0001)
      falloff     = saturate(1.0 - (d / radius)^4)^2
      L           = color.rgb * attenuation * falloff
      ```
      This matches the UE4 inverse-square model (Karis 2013 "Real Shading in
      Unreal Engine 4"). Set this in the deferred lighting pass that consumes
      transient lights — no changes to the VFX selection CS needed.
- [ ] 4.2 Confirm `ILightSystem::AddTransientPointLight` accepts
      `(Vec3 pos, Vec3 colorLinear, float radius, float intensity)` — if not,
      add the overload.

## 5. CPU Module Configuration

- [ ] 5.1 In `light_render_module.h`, add:
      ```cpp
      uint32_t maxLights    = 8;      // up to 16
      float    radiusScale  = 50.0f;  // cm per unit size
      float    minBrightness = 0.01f;
      bool     castShadows  = false;  // shadows from particle lights (expensive)
      ```
- [ ] 5.2 Ensure `Serialize` / `Deserialize` round-trips all four fields through JSON.
- [ ] 5.3 The module's `EmitHLSL()` override returns an empty string — this module
      does not contribute to the particle update CS. Ensure this is documented.

## 6. Demo Integration

- [ ] 6.1 In `vfx_viewport_demo.cpp`, add preset "Fire (Lit)": orange/red
      emissive particles with `LightRenderModule` attached, `maxLights=8`,
      `radiusScale=60.0f`.
- [ ] 6.2 Add preset "Magic (Lit)": blue/purple particles, `maxLights=4`,
      `radiusScale=40.0f`.
- [ ] 6.3 Add ImGui display showing live count of active transient lights each frame
      (read from `lightSystem->GetTransientLightCount()`).
- [ ] 6.4 Verify lights illuminate nearby geometry: place a white ground plane at Z=0
      and confirm warm/cool colored glow is visible on it beneath the emitter.

## 7. Build and Shader Registration

- [ ] 7.1 Register `particle_light_select_cs.hlsl` in `runtime/build.zig`
      shader compile list, entry = `"main"`, stage = `"cs"`.
- [ ] 7.2 Create PSO for the select CS in `vfx_system.cpp` under key
      `"particle_light_select"`.
- [ ] 7.3 Clean build: `cd runtime && zig build --clean && zig build`.
- [ ] 7.4 Demo build: `cd demos/vfx_viewport && zig build --clean && zig build`.

## 8. Visual Verification and Documentation

- [ ] 8.1 Capture `--screenshot-stdout --screenshot-frame 10 --frames 14`: fire
      preset should cast warm glow on ground plane visible as a bright patch
      beneath the particle cluster.
- [ ] 8.2 Confirm no frame pacing spike from triple-buffered readback (profile
      with `UZ_PROFILE_SCOPE("LightRenderModule::ProcessReadback")`).
- [ ] 8.3 Add `/// @ref Karis 2013 "Real Shading in Unreal Engine 4"` to the
      attenuation formula in `light_render_module.cpp`.
- [ ] 8.4 Update `light_render_module.h` class doc to describe triple-buffer scheme
      and 1-frame readback latency rationale.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
