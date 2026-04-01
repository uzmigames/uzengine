# Phase 4a — 4.12.0 VFX Mesh Renderer

Instanced 3D mesh particles. Each particle renders a full mesh instead of a
billboard quad. A compute shader builds per-instance float4x4 transforms from
the SoA particle buffers; the render pass issues a single
`DrawIndexedInstancedIndirect` call. Engine already has
`mesh_render_module.h/cpp` and `particle_mesh_instance_cs.hlsl`/`particle_mesh_vs.hlsl`/`particle_mesh_ps.hlsl`.

---

## 1. Compute Shader — Per-Instance Transform Generation

- [ ] 1.1 Open `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl`.
      Verify it reads SoA SRVs: `positions` (float3), `rotations` (float),
      `sizes` (float), `colors` (float4), and the alive-list index buffer.
      Each thread computes a `float4x4` TRS matrix and writes it to
      `RWStructuredBuffer<float4x4> g_instanceTransforms[UAV slot 0]`.
- [ ] 1.2 Add `MeshRotationMode` constant buffer field (0=None, 1=VelocityAligned,
      2=Random) consumed by the CS. For `VelocityAligned`, read the
      `velocities` SoA channel and compute a look-at matrix (`forward = normalize(vel)`,
      `up = float3(0,0,1)`). For `Random`, derive rotation from `particleSeed XOR index`.
- [ ] 1.3 Confirm thread group is `[numthreads(64,1,1)]`; dispatch count comes
      from the alive-count buffer via `particle_indirect_args_cs.hlsl`.
- [ ] 1.4 Add a second UAV slot for a `RWStructuredBuffer<float4>` that stores
      per-instance color (needed by `particle_mesh_ps.hlsl` for per-particle tinting).

## 2. Render Pass — DrawIndexedInstancedIndirect

- [ ] 2.1 In `runtime/engine/vfx/modules/mesh_render_module.cpp`, implement
      `AddParticleMeshRenderPass(RenderGraph& rg, EmitterHandle emitter)`.
      The pass must:
      - Bind `g_instanceTransforms` as SRV on vertex shader slot `t4`.
      - Bind `g_instanceColors` as SRV on pixel shader slot `t5`.
      - Issue `DrawIndexedInstancedIndirect` from the emitter's indirect-args buffer.
- [ ] 2.2 Bind the mesh index buffer (`MeshRenderModule::meshIndexBuffer`) and vertex
      buffer (`MeshRenderModule::meshVertexBuffer`) set during `MeshRenderModule::Load`.
- [ ] 2.3 Vertex shader (`particle_mesh_vs.hlsl`) must read:
      ```hlsl
      StructuredBuffer<float4x4> g_transforms : register(t4);
      float4x4 world = g_transforms[SV_InstanceID];
      ```
      and output world-space position into the existing VS→PS interpolant struct.
- [ ] 2.4 Pixel shader (`particle_mesh_ps.hlsl`) reads `g_instanceColors[SV_InstanceID]`
      and multiplies with the material base color before output.
- [ ] 2.5 Ensure the render pass state uses `D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST`
      and the correct depth-stencil state (depth write enabled for opaque meshes,
      depth write disabled for translucent with alpha blending).

## 3. CPU-Side Module Wiring

- [ ] 3.1 In `mesh_render_module.h`, add `uint32_t maxInstances = 4096;` and
      `MeshRotationMode rotationMode = MeshRotationMode::None;`.
- [ ] 3.2 In `mesh_render_module.cpp`, implement `CreateGPUBuffers(IRHIDevice*)`:
      allocate `g_instanceTransforms` as `STRUCTURED_BUFFER | UNORDERED_ACCESS`,
      element stride = 64 bytes (float4x4), count = `maxInstances`.
      Allocate `g_instanceColors` as stride = 16 bytes.
- [ ] 3.3 Implement `MeshRenderModule::Load(std::string_view meshPath, IRHIDevice*)`:
      load OBJ/UZM mesh from disk, upload vertex data and index data to
      `D3D12_HEAP_TYPE_DEFAULT` via staging upload. Store buffer handles.
- [ ] 3.4 Ensure `MeshRenderModule::Serialize` / `MeshRenderModule::Deserialize`
      round-trip `meshPath`, `maxInstances`, and `rotationMode` through JSON.

## 4. Procedural Mesh Generation for Demo

- [ ] 4.1 In `runtime/demos/vfx_viewport/vfx_viewport_mesh.cpp`, add
      `GenerateCubeMesh(IRHIDevice*, RHIBufferHandle& vb, RHIBufferHandle& ib)`:
      24 vertices, 36 indices, 1cm half-extent, normals and UVs packed as
      `struct MeshVertex { float3 pos; float3 normal; float2 uv; }` (stride = 32 bytes).
- [ ] 4.2 Add `GenerateSphereMesh(IRHIDevice*, ...)` with 8 stacks × 16 slices
      (same vertex layout as cube).
- [ ] 4.3 Expose both in `vfx_viewport_mesh.h` so the demo can load them into
      a `MeshRenderModule` without disk I/O.

## 5. Demo Integration

- [ ] 5.1 In `vfx_viewport_demo.cpp`, add a UI toggle "Render Mode: Billboard | Mesh".
      When "Mesh" is active, swap the emitter's render module from
      `SpriteRenderModule` to `MeshRenderModule` loaded with the cube mesh.
- [ ] 5.2 Add a second preset "Debris" that uses `MeshRenderModule` + sphere mesh
      with `rotationMode = VelocityAligned` and `gravity = -980 cm/s²`.
- [ ] 5.3 Add ImGui controls for `rotationMode` (combo) and `meshPath` (text input).
- [ ] 5.4 Screenshot pass: run `--screenshot-stdout --screenshot-frame 8 --frames 12`
      and verify mesh particles are visible as distinct 3D shapes (not flat quads).

## 6. Build and Shader Registration

- [ ] 6.1 Verify `particle_mesh_instance_cs.hlsl`, `particle_mesh_vs.hlsl`, and
      `particle_mesh_ps.hlsl` are listed in `runtime/build.zig` shader compile step.
- [ ] 6.2 Confirm PSO for the mesh render pass is created in `vfx_system.cpp`
      under the `MeshPass` PSO cache key (separate from `BillboardPass`).
- [ ] 6.3 Run `cd runtime && zig build` (clean build) to verify no compile errors.
- [ ] 6.4 Run `cd demos/vfx_viewport && zig build` and confirm the demo launches.

## 7. Visual Verification

- [ ] 7.1 Capture screenshot at default camera: confirm cube-shaped particles
      distinct from billboard smoke.
- [ ] 7.2 Capture with `rotationMode = VelocityAligned`: confirm mesh forward axis
      tracks velocity direction (elongated cubes should point in motion direction).
- [ ] 7.3 Confirm no z-fighting or depth artifacts between mesh particles.

## 8. Documentation

- [ ] 8.1 Add `/// @ref` cites in `particle_mesh_instance_cs.hlsl` header:
      `@ref Lasseter 1987 "Principles of Traditional Animation Applied to 3D
      Computer Animation"` for orientation alignment.
- [ ] 8.2 Update `mesh_render_module.h` doc comment to describe the two-pass
      design (CS transform generation → DrawIndexedInstancedIndirect).
