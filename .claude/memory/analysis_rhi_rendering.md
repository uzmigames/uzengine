---
name: RHI/Rendering Pipeline Analysis for Roadmap
description: Rendering dependency chain and build order analysis for UzEngine roadmap
type: project
---

# RHI / Rendering Pipeline — Dependency Analysis & Build Order

**Date**: 2026-03-12
**Context**: Post-milestone 1.2 (RHI DX12 SM6.6 complete), planning 1.3 Renderer

---

## 1. Current State Assessment

### DONE (Milestones 1.0-1.2, 1.5.0)

| System | Status | Key Files |
|--------|--------|-----------|
| **RHI DX12 SM6.6** | DONE | `runtime/core/rhi/dx12/` — device, swapchain, fences, descriptors, command lists, D3D12MA, DRED, breadcrumbs, GPU profiler |
| **Shader Pipeline** | DONE | `runtime/core/shader/` — compiler (DXC+FXC), VFS, cache (xxHash64), dependency graph, hot-reload, reflection (SPIRV-Reflect), permutations, archive |
| **Math** | DONE | `runtime/core/math/` — Vec2/3/4, Mat4, Quat, Transform, Color, AABB/Sphere |
| **Basic Renderer** | PARTIAL | `runtime/core/renderer/` — vertex.h, mesh.h/.cpp, camera.h/.cpp, constant_buffers, upload_context |
| **Asset Foundation** | PARTIAL | `runtime/core/asset/` — registry, cache, VFS, pak format, shader importer, manifest, sidecar |

### Key Observation
The project is further along than the roadmap suggests. There are already `mesh.h`, `camera.h`, `constant_buffers.h`, and `upload_context.h` in `runtime/core/renderer/`, meaning early 1.3.0 work (vertex/index buffers, camera, mesh loading) has started or been scaffolded.

---

## 2. Dependency Chain (What Blocks What)

```
RHI DX12 (DONE) ─────────────────────────────────────────────►
  │
  ├── Shader Pipeline (DONE) ──────────────────────────────────►
  │     │
  │     ├── Shader Reflection → auto root signatures (DONE)
  │     └── Hot-reload → PSO invalidation (DONE)
  │
  ├── 1.3.0 Triangle to Screen ◄── needs RHI + shaders
  │     ├── Vertex/Index buffer upload (upload_context exists)
  │     ├── Constant buffers (exists)
  │     ├── Camera (exists)
  │     ├── Texture sampling ◄── needs basic texture loading (stb_image + RHI upload)
  │     └── Mesh loading (.uzmesh) ◄── mesh.h exists, needs .uzmesh parser
  │
  ├── 1.3.1 Deferred Pipeline ◄── needs 1.3.0 + render graph
  │     ├── Render Graph (DAG, barriers, aliasing) ◄── needs RHI barriers working
  │     ├── GBuffer pass (3 RT + depth)
  │     ├── Lighting pass (fullscreen quad)
  │     ├── PBR shaders (Cook-Torrance BRDF)
  │     ├── Shadow maps (CSM) ◄── needs multiple render targets, depth-only pass
  │     └── Forward transparent pass
  │
  ├── 1.3.2 Post-Processing ◄── needs render graph + fullscreen passes
  │     ├── Tonemap (ACES)
  │     ├── Bloom (mip pyramid)
  │     ├── SSAO, SSR, TAA, DOF, Motion Blur
  │     └── Color grading (LUT)
  │
  └── 1.3.3 Advanced Rendering ◄── needs 1.3.2 + upscaling SDKs
        ├── DLSS/FSR/XeSS
        ├── Virtual shadow maps
        ├── Volumetric fog
        └── Sky/atmosphere
```

---

## 3. Resource System Dependencies for Rendering

### Mesh Pipeline (blocking 1.3.0 — textured mesh)
- **Minimum needed**: `.uzmesh` binary loader (header + vertex streams + index buffer)
- **NOT needed yet**: meshoptimizer LOD, mikktspace tangents, FBX/OBJ import
- **Already exists**: `mesh.h` scaffolding in `runtime/core/renderer/`
- **Recommendation**: Hardcode a cube/plane procedurally for 1.3.0 triangle demo, add .uzmesh parser as second step

### Texture Pipeline (blocking 1.3.0 — texture sampling)
- **Minimum needed**: stb_image PNG/TGA load + RHI texture upload (no compression)
- **NOT needed yet**: BC7/BC5 compression, mipmaps, virtual texturing, .uztexture format
- **Recommendation**: Load raw RGBA via stb_image, upload to default heap, create SRV. Add BC compression later.

### Material System (blocking 1.3.1 — deferred)
- **Minimum needed**: Hardcoded PBR shader with albedo/normal/metallic/roughness texture slots
- **NOT needed yet**: Node graph compiler, material instances, permutation system
- **Recommendation**: Start with a `SimplePBRMaterial` struct (texture handles + scalar params), compile to hardcoded PSO. Full material graph is a 2.0 feature.

---

## 4. Shader Pipeline vs Render Graph — Order

**Shader Pipeline is DONE.** This is not a question anymore.

**Render Graph** should come AFTER 1.3.0 (triangle to screen) but BEFORE 1.3.1 (deferred). Rationale:
- 1.3.0 can use manual command list recording (it's just a triangle/mesh)
- 1.3.1 deferred needs 3+ render targets with barrier transitions — manual barrier management is error-prone
- The render graph automates barrier insertion and resource aliasing, which is essential for GBuffer + lighting + post-process chain

**Build order**: 1.3.0 (manual) -> Render Graph -> 1.3.1 (render graph managed)

---

## 5. GPU Memory Management Readiness

**D3D12MA is integrated** (milestone 1.2). Current state:
- `dx12_d3d12ma.cpp` — allocator init, buffer/texture allocation
- `dx12_descriptor_allocator.cpp` — descriptor heap management
- Upload heap via `upload_context.h`

**What's missing for rendering**:
- **Transient resource pool** — needed by render graph for GBuffer/post-process textures (allocate per-frame, alias memory)
- **Texture streaming** — NOT needed until 2.0+
- **VRAM budget tracking** — nice-to-have, not blocking

**Verdict**: GPU memory is ready for 1.3.0-1.3.2. Transient pool is a render graph concern.

---

## 6. Minimum Viable Rendering Path

### Step 1: Colored Triangle (1.3.0a) — ~1-2 days
- Hardcoded vertex buffer (3 vertices, position + color)
- Simple VS/PS shader pair (pass-through)
- Single PSO, manual command list recording
- **Validates**: vertex upload, shader compilation, PSO creation, draw call, present

### Step 2: Textured Quad (1.3.0b) — ~2-3 days
- stb_image load PNG -> upload to GPU texture
- SRV descriptor in bindless heap
- Sampler creation
- UV-mapped quad with texture sampling
- **Validates**: texture pipeline, descriptor binding, sampler state

### Step 3: 3D Mesh + Camera (1.3.0c) — ~3-4 days
- .uzmesh loader OR procedural cube/sphere
- Perspective camera with view/projection matrices
- Constant buffer for MVP transform
- WASD + mouse look
- Depth buffer (reversed-Z D32_FLOAT)
- **Validates**: 3D pipeline, depth testing, camera math, per-object transforms

### Step 4: Render Graph Foundation (bridge) — ~5-7 days
- RgBuilder, RgPass, RgTexture, RgBuffer abstractions
- Automatic barrier insertion from declared read/write states
- Pass culling (unused outputs)
- Transient resource aliasing (memory reuse)
- **Validates**: barrier correctness, resource lifetime, DAG execution

### Step 5: GBuffer + Deferred Lighting (1.3.1a) — ~5-7 days
- GBuffer pass: Albedo (RGBA8), Normal (R10G10B10A2), MetalRough (RGBA8), Depth (D32)
- Lighting pass: fullscreen quad, reconstruct position from depth
- Single directional light, Cook-Torrance BRDF
- **Validates**: MRT rendering, deferred shading, PBR math

### Step 6: Shadows + Multiple Lights (1.3.1b) — ~5-7 days
- Directional light CSM (4 cascades)
- Point lights (up to 64, tiled or clustered)
- Shadow map atlas
- **Validates**: shadow rendering, light culling

### Step 7: Basic Post-Processing (1.3.2a) — ~3-5 days
- Tonemap (ACES filmic)
- Bloom (downsample mip chain + blur + composite)
- **Validates**: post-process chain in render graph, HDR pipeline

### Step 8: Full Post-Processing (1.3.2b) — ~7-10 days
- SSAO (GTAO, half-res compute)
- TAA (jitter + history)
- SSR, DOF, Motion Blur (can be deferred further)

**Total to "PBR scene on screen"**: Steps 1-6 = ~21-30 days
**Total to "shippable visual quality"**: Steps 1-8 = ~31-45 days

---

## 7. Exact Milestone Order with Dependencies

```
Week 1-2:  1.3.0a  Colored Triangle          [needs: RHI, Shader Pipeline]
Week 2-3:  1.3.0b  Textured Quad             [needs: stb_image, SRV binding]
Week 3-4:  1.3.0c  3D Mesh + Camera          [needs: .uzmesh or proc mesh, depth buffer]
Week 4-6:  RG      Render Graph Foundation   [needs: RHI barriers, resource creation]
Week 6-8:  1.3.1a  GBuffer + Deferred        [needs: Render Graph, PBR shaders]
Week 8-10: 1.3.1b  Shadows + Lights          [needs: 1.3.1a, depth-only pass]
Week 10-11: 1.3.2a Basic Post (Tonemap+Bloom) [needs: Render Graph, fullscreen pass]
Week 11-14: 1.3.2b Full Post (SSAO/TAA/SSR)  [needs: 1.3.2a, velocity buffer]
```

---

## 8. What Can Be Deferred

| Feature | Defer Until | Reason |
|---------|-------------|--------|
| Material node graph compiler | 2.0 (Editor) | Hardcoded PBR material sufficient for 1.3 |
| Mesh optimization (meshoptimizer) | 1.5.1 (Asset Processor) | Raw meshes work fine for development |
| Texture compression (BC7/BC5) | 1.5.1 (Asset Processor) | Uncompressed RGBA works, just uses more VRAM |
| Virtual texturing | 2.5+ | Only needed for large worlds |
| .uztexture binary format | 1.5.1 | stb_image direct load sufficient for dev |
| DLSS/FSR/XeSS | 1.3.3 | Post-processing works without upscaling |
| Volumetric fog, sky/atmosphere | 1.3.3 | Not on critical path |
| Vulkan/Metal backends | 2.0+ | DX12 is primary, others follow same vtable |
| Forward+ (clustered) lighting | 1.3.1b | Simple tiled lighting first |
| SSR, DOF, Motion Blur | Late 1.3.2 | Tonemap + Bloom + SSAO + TAA are higher priority |
| Async compute passes | 1.3.2+ | Sequential execution first, optimize later |

---

## 9. Critical Blockers Summary

1. **Nothing is blocking 1.3.0** — RHI and shader pipeline are done, renderer scaffolding exists
2. **Render Graph blocks 1.3.1** — manual barriers for a full deferred pipeline would be unmaintainable
3. **Basic texture loading blocks textured rendering** — need stb_image + GPU upload (trivial)
4. **PBR shaders block deferred** — need Cook-Torrance BRDF HLSL (well-understood, not risky)
5. **Nothing in the resource system blocks early rendering** — defer .uzmesh/.uztexture formats, use raw data
