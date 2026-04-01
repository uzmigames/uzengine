# Phase 4a — 4.14.0 VFX Dynamic Material Params

Per-particle `float4` custom data channel ("dynamicParams") that flows from
spawn through simulation to the pixel shader. Enables fully custom per-particle
effects driven by the particle's own evolving state (heat shimmer, wear, magic
phase, dissolve). This is the 8th SoA channel added to `GpuParticleBuffer`.

---

## 1. SoA Buffer Extension

- [ ] 1.1 In `runtime/engine/vfx/core/gpu_particle_buffer.h`, add to
      `ParticleAttributeSet`:
      ```cpp
      /// Per-particle float4 custom data (dynamicParams). Stride = 16 bytes.
      /// Channel 0 (x): general-purpose scalar (heat, wear, phase, etc.)
      /// Channel 1 (y): secondary scalar (dissolve amount, age-normalized)
      /// Channel 2 (z): tertiary scalar (user-defined)
      /// Channel 3 (w): quaternary scalar (user-defined)
      RHIBufferHandle dynamicParams = RHIBufferHandle::Invalid();
      ```
- [ ] 1.2 Update `ParticleAttributeSet::IsValid()` to include `dynamicParams`.
- [ ] 1.3 In `gpu_particle_buffer.cpp`, allocate `dynamicParams` in both ping-pong
      sets: `stride = 16`, `count = maxParticles`,
      `flags = STRUCTURED_BUFFER | UNORDERED_ACCESS`.
- [ ] 1.4 Update `GpuParticleBuffer::GetMemoryBytesPerParticle()` to return the
      updated total (was 56 bytes per set → now 72 bytes per set, 144 bytes both).

## 2. Spawn Compute Shader

- [ ] 2.1 In `particle_spawn_cs.hlsl`, add:
      ```hlsl
      RWStructuredBuffer<float4> g_dynamicParamsW : register(u7);
      cbuffer SpawnCB { // existing + new fields
          float4 dynamicParamsDefault; // default value for new particles
      };
      ```
      In the spawn thread body, after writing color:
      ```hlsl
      g_dynamicParamsW[newIndex] = g_spawnCB.dynamicParamsDefault;
      ```
- [ ] 2.2 In the CPU spawn module (`spawn_module.h`), add:
      ```cpp
      Vec4 dynamicParamsDefault = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
      ```
      and upload it to `SpawnCB` each frame via the existing CB update path.

## 3. Update Compute Shader

- [ ] 3.1 In `particle_update_cs.hlsl`, bind both read and write slots:
      ```hlsl
      StructuredBuffer<float4>   g_dynamicParamsR : register(t7);
      RWStructuredBuffer<float4> g_dynamicParamsW : register(u7);
      ```
- [ ] 3.2 Add constant buffer fields in `UpdateCB` (or a new `DynamicParamsCB`
      at `b4`):
      ```hlsl
      cbuffer DynamicParamsCB : register(b4) {
          uint  evolveMode;     // 0=constant, 1=lerp-to-zero, 2=age-normalized
          float evolveRate;     // lerp speed per second (mode 1 only)
          float2 _pad;
      };
      ```
- [ ] 3.3 In the update thread body, after reading `dynamicParamsR`:
      ```hlsl
      float4 dp = g_dynamicParamsR[idx];
      if (evolveMode == 1) dp = lerp(dp, float4(0,0,0,0), evolveRate * dt);
      if (evolveMode == 2) dp.x = age / lifespan; // normalized lifetime in .x
      g_dynamicParamsW[idx] = dp;
      ```

## 4. Vertex Shader Passthrough

- [ ] 4.1 In `particle_billboard_vs.hlsl`, bind:
      ```hlsl
      StructuredBuffer<float4> g_dynamicParams : register(t7);
      ```
      Read `float4 dp = g_dynamicParams[particleIndex];` and add it to the
      VS output struct:
      ```hlsl
      struct VSOut {
          // ... existing fields ...
          float4 dynamicParams : TEXCOORD3;
      };
      output.dynamicParams = dp;
      ```
- [ ] 4.2 Apply the same change to `particle_mesh_vs.hlsl` (reads by `SV_InstanceID`).

## 5. Pixel Shader Usage

- [ ] 5.1 In `particle_billboard_ps.hlsl`, receive `float4 dynamicParams : TEXCOORD3`.
      Add a compile-time flag `#define USE_DYNAMIC_PARAMS 1` controlled by a
      shader permutation macro. When enabled, apply a simple heat-tint:
      ```hlsl
      #if USE_DYNAMIC_PARAMS
          float heat = dynamicParams.x; // 0..1
          float3 heatColor = lerp(float3(1,0.5,0), float3(1,1,0.3), heat);
          color.rgb = lerp(color.rgb, color.rgb * heatColor, heat * 0.6);
      #endif
      ```
      This demonstrates a correct usage pattern; real effects will override it.
- [ ] 5.2 Add `float4 dynamicParams : TEXCOORD3;` to `particle_mesh_ps.hlsl` as well.

## 6. CPU Module and Serialization

- [ ] 6.1 Create `runtime/engine/vfx/modules/dynamic_params_module.h/cpp`
      implementing `IVFXModule`. Fields:
      ```cpp
      Vec4    defaultValue = Vec4(0.f, 0.f, 0.f, 0.f);
      uint8_t evolveMode   = 0;  // 0=constant, 1=lerp-to-zero, 2=age-normalized
      float   evolveRate   = 1.0f;
      ```
- [ ] 6.2 `EmitHLSL()` returns `""` — the module only uploads CB data, no inline
      HLSL snippet needed.
- [ ] 6.3 `Serialize` / `Deserialize` round-trips all three fields via JSON.
- [ ] 6.4 Register the module in `register_builtin_modules.cpp` under the key
      `"DynamicParams"`.

## 7. Demo Integration

- [ ] 7.1 Add preset "Heat Shimmer" in `vfx_viewport_demo.cpp`:
      `evolveMode=2` (age-normalized, dp.x=t/lifespan), particle shader uses
      `dynamicParams.x` to drive a turbulence UV offset in the PS
      (simulating heat distortion intensity increasing with age).
- [ ] 7.2 Add ImGui controls in `vfx_parameter_panel.cpp` for `defaultValue`
      (float4 drag), `evolveMode` (combo: Constant / Lerp-To-Zero / Age-Normalized),
      and `evolveRate` (drag float).
- [ ] 7.3 Screenshot: capture with "Heat Shimmer" preset and verify color variation
      is visible across particle lifetimes.

## 8. Build and Verification

- [ ] 8.1 Verify all six updated shaders compile without error:
      `particle_spawn_cs`, `particle_update_cs`, `particle_billboard_vs`,
      `particle_billboard_ps`, `particle_mesh_vs`, `particle_mesh_ps`.
- [ ] 8.2 Run `cd runtime && zig build --clean && zig build`.
- [ ] 8.3 Run `cd demos/vfx_viewport && zig build --clean && zig build`.
- [ ] 8.4 Confirm `GpuParticleBuffer::GetMemoryBytesPerParticle()` returns 144
      in a unit test or log assertion at startup.
- [ ] 8.5 Add `/// @ref` to `dynamic_params_module.h`: cite the concept of
      per-particle custom data from GDC 2014 "Destiny's Multithreaded Rendering
      Architecture" as a design reference for per-particle payload channels.
