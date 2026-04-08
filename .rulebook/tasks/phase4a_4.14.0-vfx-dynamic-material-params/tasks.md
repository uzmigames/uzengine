# Phase 4a — 4.14.0 VFX Dynamic Material Params

Per-particle `float4` custom data channel ("dynamicParams") that flows from
spawn through simulation to the pixel shader. Enables fully custom per-particle
effects driven by the particle's own evolving state (heat shimmer, wear, magic
phase, dissolve). This is the 8th SoA channel added to `GpuParticleBuffer`.

---

## 1. SoA Buffer Extension

- [x] 1.1 Added channel 7 = dynamicParams (float4, stride=16) to VFXParticleSystem in `vfx_viewport_vfx.h`. kChannelCount bumped from 7 to 8. ChannelStride case 7 returns 16. Buffer creation/destruction handled by existing kChannelCount loop.
- [x] 1.2 All loops (Init, UploadInitialFreeList, Shutdown) use kChannelCount=8, so dynamicParams buffers are automatically created, zero-initialized, and destroyed.
- [x] 1.3 Both ping-pong sets allocated automatically via the kChannelCount loop in Init().
- [x] 1.4 Memory per particle increased by 16 bytes per set (channel 7 = float4).

## 2. Spawn Compute Shader

- [x] 2.1 Added dynamicParamsUavIndex and defaultDynParam0-3 to VFXSpawnCB and HLSL cbuffer. Bindless + SM5 paths. Write float4(defaults) at particleIdx after rotation write. VFXSpawnCB grew to 192 bytes (12 x float4).
- [x] 2.2 VFXEmitterConfig gains dynamicParam0-3 fields (default 0.0f). CPU fills VFXSpawnCB from config in DispatchCompute().

## 3. Update Compute Shader

- [x] 3.1 Added readDynParamsSrv and writeDynParamsUav to VFXUpdateCB and HLSL cbuffer. Bindless + SM5 paths (t7 SRV, u10 UAV). VFXUpdateCB grew to 304 bytes (19 x float4).
- [x] 3.2 Fields added to existing VFXUpdateCB (no separate cbuffer needed). CPU fills from particle buffer SRV/UAV indices.
- [x] 3.3 Update CS reads dynParams from read set, writes .x = normalizedAge (age/lifespan), passes through .yzw unchanged. Both bindless and SM5 paths.

## 4. Vertex Shader Passthrough

- [x] 4.1 Added dynamicParamsBufferIdx to VFXBillboardConstants (b0, replaces _vfxPad0). Added float4 dynamicParams : TEXCOORD7 to VSOutput. VS reads from ResourceDescriptorHeap[dynamicParamsBufferIdx] and outputs to interpolant. SM5 fallback zeroes the field. Dead particle path also zeroes it.
- [ ] 4.2 Apply the same change to `particle_mesh_vs.hlsl` (reads by `SV_InstanceID`). — Deferred to mesh renderer task.

## 5. Pixel Shader Usage

- [x] 5.1 Added float4 dynamicParams : TEXCOORD7 to PSInput in particle_billboard_ps.hlsl. Updated b0 cbuffer to match VS (dynamicParamsBufferIdx replaces _vfxPad0). Field declared but not consumed yet — available for future effects.
- [ ] 5.2 Add `float4 dynamicParams : TEXCOORD7;` to `particle_mesh_ps.hlsl` as well. — Deferred to mesh renderer task.

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

- [x] 8.1 All four updated billboard shaders compile without error: particle_spawn_cs, particle_update_cs, particle_billboard_vs, particle_billboard_ps. Also updated particle_death_spawn_cs to write zeroed dynamic params for child particles.
- [x] 8.2 Runtime builds successfully (24/27 steps, 1 pre-existing test linker failure).
- [x] 8.3 `cd demos/vfx_viewport && zig build` succeeds with zero errors.
- [ ] 8.4 Confirm `GpuParticleBuffer::GetMemoryBytesPerParticle()` returns 144
      in a unit test or log assertion at startup.
- [ ] 8.5 Add `/// @ref` to `dynamic_params_module.h`: cite the concept of
      per-particle custom data from GDC 2014 "Destiny's Multithreaded Rendering
      Architecture" as a design reference for per-particle payload channels.

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [ ] 9.2 Write tests covering the new behavior
- [ ] 9.3 Run tests and confirm they pass
