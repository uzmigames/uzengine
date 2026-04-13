# Phase 4a — 4.12.0 VFX Mesh Renderer

Instanced 3D mesh particles. Each particle renders a full mesh instead of a
billboard quad. A compute shader builds per-instance float4x4 transforms from
the SoA particle buffers; the render pass issues a single
`DrawIndexedInstancedIndirect` call. Engine already has
`mesh_render_module.h/cpp` and `particle_mesh_instance_cs.hlsl`/`particle_mesh_vs.hlsl`/`particle_mesh_ps.hlsl`.

---

## 1. Compute Shader — Per-Instance Transform Generation

- [x] 1.1 Verified CS reads SoA SRVs (positions, velocities, ages, lifespans, colors).
      Each thread computes float4x4 TRS and writes to RWStructuredBuffer<float4x4>.
- [x] 1.2 CB already has `rotationMode` (0=None, 1=VelocityAligned, 2=Random).
      VelocityAligned builds look-at from velocity. Random uses Wang hash Euler angles.
- [x] 1.3 Thread group is [numthreads(256,1,1)] (MESH_INSTANCE_GROUP_SIZE=256).
      CPU dispatches ceil(aliveCount/256) groups.
- [x] 1.4 Added u3 RWStructuredBuffer<float4> instanceColors UAV + t4 colors SRV.
      CS now reads particle color and writes it to the instance color buffer.

## 2. Render Pass — DrawIndexedInstancedIndirect

- [ ] 2.1 In `runtime/engine/vfx/modules/mesh_render_module.cpp`, implement
      `AddParticleMeshRenderPass(RenderGraph& rg, EmitterHandle emitter)`.
      The pass must:
      - Bind `g_instanceTransforms` as SRV on vertex shader slot `t4`.
      - Bind `g_instanceColors` as SRV on pixel shader slot `t5`.
      - Issue `DrawIndexedInstancedIndirect` from the emitter's indirect-args buffer.
- [ ] 2.2 Bind the mesh index buffer (`MeshRenderModule::meshIndexBuffer`) and vertex
      buffer (`MeshRenderModule::meshVertexBuffer`) set during `MeshRenderModule::Load`.
- [x] 2.3 VS reads g_instanceTransforms[SV_InstanceID], transforms position and
      normal to world space, outputs clipPos/worldPos/worldNormal/uv/instanceID.
- [x] 2.4 PS reads g_instanceColors[SV_InstanceID], applies half-Lambert N-dot-L
      lighting with directional + ambient, multiplies by instance color.
- [ ] 2.5 Ensure the render pass state uses `D3D12_PRIMITIVE_TOPOLOGY_TRIANGLELIST`
      and the correct depth-stencil state (depth write enabled for opaque meshes,
      depth write disabled for translucent with alpha blending).

## 3. CPU-Side Module Wiring

- [x] 3.0 Added `uint32_t meshParticleEnabled = 0;` to VFXEmitterConfig in vfx_viewport_vfx.h. 0=billboard (default), 1=mesh particles (future). Config field only — no rendering path yet.
- [x] 3.1 Added `uint32_t maxInstances = 4096;` to MeshRenderModuleConfig.
      MeshRotationMode and rotationMode already existed.
- [x] 3.2 Implemented `CreateGPUBuffers(IRHIDevice*)`: allocates instanceTransformBuffer_
      (stride=64, UAV) and instanceColorBuffer_ (stride=16, UAV). Error handling
      with partial cleanup on failure. DestroyGPUBuffers cleans up.
- [x] 3.3 Implemented `Load(meshPath, device)`: stores path in config, marks loaded.
      Actual mesh loading from disk deferred to asset pipeline task.
- [x] 3.4 ParseConfig now handles `max_instances` JSON field with range validation
      [1, 1048576]. Tests cover valid, zero, negative, and too-large values.

## 4. Procedural Mesh Generation for Demo

- [x] 4.1 Created `runtime/engine/vfx/mesh/vfx_procedural_mesh.h/.cpp` with
      `GenerateCubeMesh(IRHIDevice*)`: 24 vertices, 36 indices, 0.5cm half-extent.
      VfxMeshVertex: float3 pos, float3 normal, float2 uv (32 bytes). Z-up LH.
- [x] 4.2 Implemented `GenerateSphereMesh(IRHIDevice*)`: 8 stacks x 16 slices
      (153 vertices, 768 indices). Same VfxMeshVertex layout.
- [x] 4.3 Both in engine code (not demos) at `engine/vfx/mesh/`, usable by
      any module without disk I/O. build.zig updated with vfx/mesh dir.

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

- [x] 6.1 All three shaders exist in `runtime/shaders/runtime/vfx/` and are
      auto-discovered by uzembed (build verifies: "Shader integrity OK").
- [ ] 6.2 Confirm PSO for the mesh render pass is created in `vfx_system.cpp`
      under the `MeshPass` PSO cache key (separate from `BillboardPass`).
- [x] 6.3 `cd runtime && zig build` — clean build, 0 errors, 0 warnings.
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

## 9. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 9.1 Update or create documentation covering the implementation
- [x] 9.2 Tests written: 20+ new tests in test_mesh_render_module.cpp (GPU buffers,
      Load, maxInstances parsing) + 18 tests in test_vfx_procedural_mesh.cpp (cube,
      sphere, destroy, null/failing device). MockRHIDevice updated with MapBuffer support.
- [ ] 9.3 Run tests and confirm they pass
