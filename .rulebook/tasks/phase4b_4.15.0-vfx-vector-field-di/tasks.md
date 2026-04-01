# Phase 4b — 4.15.0 VFX Vector Field DI

Sample a 3D flow texture to drive particle velocity. Engine has
`vector_field_module.h/cpp` and `vfx_vector_field.hlsli` already. This task
wires the full pipeline: file loading → 3D texture upload → SRV binding →
CS sampling → velocity influence — and exposes it in the demo.

---

## 1. 3D Texture Loading

- [ ] 1.1 In `vector_field_module.cpp`, implement `LoadFromFile(std::string_view path,
      IRHIDevice* device)`:
      - Support `.vf` binary format: 12-byte header `{ uint32 width, height, depth }`,
        followed by `width*height*depth` RGBA16F values (8 bytes each).
      - Allocate a `D3D12_RESOURCE_DIMENSION_TEXTURE3D` with format
        `DXGI_FORMAT_R16G16B16A16_FLOAT` via `IRHIDevice::CreateTexture3D()`.
      - Upload via a staging buffer using `IRHIDevice::UploadTexture3D()`.
      - Store the returned `RHITextureHandle` in `VectorFieldModule::fieldTexture`.
- [ ] 1.2 Add `LoadProceduralNoise(uint32_t size, IRHIDevice* device)` that generates
      a `size^3` 3D curl noise field on the CPU and uploads it. Use the formula from
      `vfx_curl_noise.hlsli` transposed to CPU float arithmetic (for seeding only).
      Default `size = 32` (32^3 = 32 768 cells, 262 KB RGBA16F).
- [ ] 1.3 Register the SRV in the bindless descriptor heap:
      ```cpp
      fieldSrvIndex = device->RegisterTexture3DSRV(fieldTexture);
      ```
      Store in `VectorFieldModule::fieldSrvIndex` (int32, -1 = unloaded).

## 2. HLSL Include Update

- [ ] 2.1 Open `runtime/shaders/runtime/vfx/vfx_vector_field.hlsli`. Confirm it
      declares:
      ```hlsl
      Texture3D<float4>  g_vectorField : register(t8);
      SamplerState       g_vfSampler   : register(s2); // trilinear wrap
      ```
      If not present, add these declarations.
- [ ] 2.2 Verify the sampling function:
      ```hlsl
      /// @ref Stam 1999 "Stable Fluids" — trilinear velocity advection
      float3 SampleVectorField(float3 worldPos, float3 fieldMin,
                               float3 fieldExtent, float intensity,
                               float tightness, float3 currentVel, float dt) {
          float3 uvw = saturate((worldPos - fieldMin) / fieldExtent);
          float3 field = g_vectorField.SampleLevel(g_vfSampler, uvw, 0).xyz;
          field = field * 2.0 - 1.0; // decode [-1,1] from [0,1]
          float3 fieldVel = field * intensity;
          float blend = saturate(tightness * dt);
          return lerp(currentVel + fieldVel * dt, fieldVel, blend);
      }
      ```
      If incorrect or absent, rewrite it exactly as shown.
- [ ] 2.3 Add a bounding-box guard: if `any(uvw < 0) || any(uvw > 1)` before
      the clamp, return `currentVel` unchanged (no force outside the field box).

## 3. Update CS Binding

- [ ] 3.1 In `particle_update_cs.hlsl`, verify `g_vectorField` is already bound
      at `t8`. If not, add:
      ```hlsl
      Texture3D<float4> g_vectorField : register(t8);
      SamplerState      g_vfSampler   : register(s2);
      ```
- [ ] 3.2 Add to `UpdateCB`:
      ```hlsl
      float3 vfFieldMin;     // world-space AABB min corner
      float  vfIntensity;    // default 1.0
      float3 vfFieldExtent;  // world-space AABB size
      float  vfTightness;    // 0.0 = additive, 1.0 = full replacement
      uint   vfEnabled;      // 0 or 1 — skip if disabled
      float3 _vfPad;
      ```
- [ ] 3.3 In the update thread body, after gravity/drag, conditionally call:
      ```hlsl
      if (vfEnabled) {
          vel = SampleVectorField(pos, vfFieldMin, vfFieldExtent,
                                  vfIntensity, vfTightness, vel, dt);
      }
      ```

## 4. CPU Module Parameter Upload

- [ ] 4.1 In `vector_field_module.h`, expose:
      ```cpp
      Vec3  fieldMin     = Vec3(-100.f, -100.f, -100.f); // cm
      Vec3  fieldExtent  = Vec3( 200.f,  200.f,  200.f); // cm
      float intensity    = 1.0f;
      float tightness    = 0.0f;
      ```
- [ ] 4.2 In `vector_field_module.cpp`, implement `UploadCB(UpdateCB& cb)`:
      fill the four `vf*` fields and set `vfEnabled = fieldSrvIndex >= 0 ? 1 : 0`.
- [ ] 4.3 Bind `g_vectorField` SRV in the update CS dispatch: look up the
      bindless index `fieldSrvIndex` and write it to the descriptor table
      at slot `t8` for this emitter's dispatch.

## 5. Serialization

- [ ] 5.1 `VectorFieldModule::Serialize` must emit:
      ```json
      { "fieldPath": "content/vfx/flow_field.vf",
        "fieldMin": [-100,-100,-100], "fieldExtent": [200,200,200],
        "intensity": 1.0, "tightness": 0.0 }
      ```
- [ ] 5.2 `Deserialize` calls `LoadFromFile(fieldPath)` if `fieldPath` is non-empty,
      or `LoadProceduralNoise(32)` if the path is empty/missing.

## 6. Demo Integration

- [ ] 6.1 In `vfx_viewport_demo.cpp`, add preset "Vector Field":
      `LoadProceduralNoise(32)`, `intensity=1.5`, `tightness=0.1`,
      field extents covering `[-200,-200,-100]` to `[200,200,300]` cm.
- [ ] 6.2 Add ImGui sliders for `intensity` (0–5), `tightness` (0–1), and
      a "Reload Field" button that calls `LoadProceduralNoise(32)` again.
- [ ] 6.3 Add a debug wireframe AABB drawn around the field extents (use the
      existing line-draw utility in the demo).

## 7. Build and Verification

- [ ] 7.1 `cd runtime && zig build --clean && zig build` — no errors.
- [ ] 7.2 `cd demos/vfx_viewport && zig build --clean && zig build` — no errors.
- [ ] 7.3 Capture `--screenshot-stdout --screenshot-frame 8 --frames 12` with
      the "Vector Field" preset. Verify particles curve/spiral visibly compared
      to a no-field baseline.
- [ ] 7.4 Test with `tightness=1.0`: particles should converge to field velocity
      within one frame (sharp direction snap).

## 8. Documentation

- [ ] 8.1 Add to `vfx_vector_field.hlsli` header:
      ```hlsl
      /// @ref Stam 1999 "Stable Fluids" — velocity advection via trilinear sampling
      /// @ref Bridson 2007 "Fluid Simulation for Computer Graphics" — boundary conditions
      ```
- [ ] 8.2 Update `vector_field_module.h` doc: describe the `tightness` parameter
      semantics (0 = additive force, 1 = velocity replacement) and the `.vf` file format.
