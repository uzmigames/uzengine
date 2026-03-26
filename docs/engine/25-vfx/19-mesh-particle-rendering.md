# Instanced Mesh Particle Rendering

> **Status**: Implemented (Phase 3.65.0)
> **References**: [VFX Modules](03-vfx-modules.md), [Particle System](02-particle-system.md), [.uzvfx Format](07-uzvfx-format.md)
> **Source**: `runtime/engine/vfx/modules/mesh_render/`, `runtime/shaders/runtime/vfx/particle_mesh_instance_cs.hlsl`, `runtime/shaders/runtime/vfx/particle_mesh_vs.hlsl`, `runtime/shaders/runtime/vfx/particle_mesh_ps.hlsl`

## Purpose

The Mesh Render Module replaces camera-facing billboard quads with full 3D mesh instances. Each alive particle becomes an independent mesh instance (e.g., debris chunk, crystal shard, leaf) with position, rotation, and scale. This enables richer visual effects while maintaining GPU efficiency through instancing.

Unlike sprite rendering (which uses a single quad per particle), mesh rendering issues **one DrawIndexedInstancedIndirect call per emitter**, making draw-call cost independent of particle count.

## Conceptual Overview

```
┌──────────────────────────────────────────────────────────────────────────┐
│ Mesh Particle Rendering Pipeline                                         │
├──────────────────────────────────────────────────────────────────────────┤
│                                                                           │
│ 1. Module Composition (Frame Setup)                                     │
│    CPU: Prepare MeshRenderModule with meshPath, materialPath, scale     │
│                                                                           │
│ 2. GPU Particle Simulation (Compute Dispatch)                           │
│    GPU: Run particle update CS                                          │
│    - Update positions, velocities, ages (standard)                      │
│    - Dead particles skip further processing                             │
│                                                                           │
│ 3. Instance Transform Compute (particle_mesh_instance_cs.hlsl)          │
│    GPU: For each alive particle (parallel):                             │
│    - Read position, velocity, age, lifespan from SRV buffers            │
│    - Compute float4x4 TRS (translate × rotate × scale):                 │
│      * Translation: particle world position                             │
│      * Rotation: based on rotationMode (None / VelocityAligned / Random)│
│      * Scale: meshScale applied to mesh dimensions                      │
│    - Write transform to RWStructuredBuffer<float4x4>                    │
│    - Atomically increment instance count                                │
│    Dispatch: ceil(aliveCount / 256) groups, 256 threads each            │
│                                                                           │
│ 4. Indirect Args Preparation                                           │
│    GPU: CS thread 0 writes DrawIndexedInstancedIndirect args:           │
│    - IndexCountPerInstance: mesh's index count (from config)            │
│    - InstanceCount: total alive particles (from instance CS)            │
│    - StartIndexLocation, BaseVertexLocation, StartInstanceLocation: 0   │
│                                                                           │
│ 5. Graphics Pass (AddParticleMeshRenderPass)                            │
│    GPU: For each batch (per emitter):                                   │
│    - Bind graphics PSO (VS + PS)                                        │
│    - Bind per-pass CB (view-projection matrix)                          │
│    - Bind instance transforms SRV (t0)                                  │
│    - Bind mesh vertex & index buffers                                   │
│    - DrawIndexedIndirect using pre-filled args buffer                   │
│    Output: Particles rendered as transformed mesh instances             │
│                                                                           │
└──────────────────────────────────────────────────────────────────────────┘
```

## Rotation Modes

The `MeshRotationMode` enum determines how each particle orients itself:

### None

**Identity rotation** — all particles face the same way (+Z in world space):

```hlsl
float4x4 transform = BuildIdentityTRS(position, scaleX, scaleY, scaleZ);
// Result: mesh +Z = world +Z for all particles
```

**Use case**: Debris that doesn't need directional alignment (random rubble).

### VelocityAligned

**Velocity-tracking rotation** — particle forward axis (+Z) aligns with velocity direction:

```hlsl
float3 forward = normalize(velocity);
float3 worldUp = abs(forward.y) < 0.999f ? float3(0,1,0) : float3(1,0,0);
float3 right = normalize(cross(worldUp, forward));
float3 up = cross(forward, right);
// Mesh +Z = velocity direction, +Y = world up (or fallback)
```

**Fallback**: When velocity is near-vertical (|vy| > 0.999), world X is used as the up vector to avoid degenerate cross products.

**Use case**: Sparks flying away from source, arrow trails, water droplets.

### Random

**Deterministic random rotation** — each particle has a fixed pseudo-random orientation derived from its index:

```hlsl
float angleX = RandomAngle(particleIdx);
float angleY = RandomAngle(particleIdx * knuthHash1);
float angleZ = RandomAngle(particleIdx * knuthHash2);
// Rz * Ry * Rx intrinsic XYZ Euler composition
```

**Note**: Rotation is constant per particle at spawn (index-derived seed). Particles do **not** spin in flight. To add spinning, use separate angular velocity attributes in the particle update shader.

**Use case**: Randomly-oriented leaves, ice shards, debris chunks.

## Configuration

### MeshRenderModuleConfig

| Field | Default | Description |
|-------|---------|-------------|
| `meshPath` | "" | VFS-relative path to mesh asset (e.g., `"meshes/debris.uzmesh"`) |
| `materialPath` | "" | VFS-relative path to material asset (e.g., `"materials/debris.uzmat"`) |
| `meshScale` | (1, 1, 1) | Per-axis non-uniform scale applied to every instance |
| `castShadow` | false | Whether instances cast shadows (expensive for dense systems) |
| `rotationMode` | VelocityAligned | Orientation strategy (None / VelocityAligned / Random) |

### Example .uzvfx Configuration

```json
{
  "emitter": {
    "name": "debris_explosion",
    "spawn_rate": 200,
    "max_particles": 512
  },
  "modules": [
    {
      "type": "mesh_render",
      "config": {
        "mesh_path": "meshes/rock_chunk.uzmesh",
        "material_path": "materials/rock.uzmat",
        "mesh_scale": [0.5, 0.5, 0.7],
        "cast_shadow": false,
        "rotation_mode": "Random"
      }
    },
    {
      "type": "gravity",
      "config": {
        "strength": 1.0
      }
    },
    {
      "type": "drag",
      "config": {
        "drag_coefficient": 0.2
      }
    }
  ]
}
```

## GPU Pipeline Details

### Compute Shader: particle_mesh_instance_cs.hlsl

**Input**:
- `aliveCount` — Number of alive particles
- Particle attributes from SRVs: positions, velocities, ages, lifespans
- `rotationMode` — Selected orientation strategy
- `meshScale[3]` — Per-axis scale factors

**Processing**:
1. Each thread processes one particle index
2. Skip dead particles (age >= lifespan)
3. Compute TRS transform based on rotation mode
4. Atomically claim a slot in output instance buffer
5. Write transform to `RWStructuredBuffer<float4x4> InstanceTransforms`
6. Update indirect args instance count

**Output**:
- `InstanceTransforms[0..N]` — float4x4 transforms, one per alive particle
- `IndirectArgs` — DrawIndexedInstancedIndirect argument buffer (20 bytes)

**Thread Group**: [256, 1, 1]

**Dispatch**: (ceil(aliveCount / 256), 1, 1)

### Graphics Pass: AddParticleMeshRenderPass

**Input**:
- Instance transforms SRV
- Mesh vertex & index buffers
- Material textures (via SRV descriptor table)
- Per-pass CB with view-projection matrix

**Rendering**:
- For each batch:
  1. Bind graphics PSO (VS + PS)
  2. Set per-pass CB (b0)
  3. Bind SRV descriptor table (t0: instances, t1+: material)
  4. Bind vertex & index buffers
  5. `DrawIndexedIndirect` with pre-filled args

**Output**: Mesh particles composited into HDR color buffer

### Vertex Shader Responsibilities

The VS receives:
- `SV_VertexID` — Original mesh vertex index
- `SV_InstanceID` — Which transform to apply

```hlsl
// Pseudo-shader
float4 vs_main(
    float3 position : POSITION,
    uint vertexId : SV_VertexID,
    uint instanceId : SV_InstanceID
) : SV_POSITION {
    StructuredBuffer<float4x4> transforms = ResourceDescriptorHeap[instanceTransformsSrvIndex];
    float4x4 transform = transforms[instanceId];

    float4 worldPos = float4(position, 1.0);
    worldPos = mul(transform, worldPos);  // Apply instance transform

    float4 clipPos = mul(worldPos, cbViewProj);
    return clipPos;
}
```

## Material Requirements

Materials used for mesh particles must:

1. **Support instanced rendering** — Material shader must read from instance transform buffer (t0)
2. **Define vertex/index buffers** — Material is bound to a specific mesh geometry
3. **Support transparency (optional)** — Via `isTransparent` flag in render pass config

### Material Configuration

Example `.uzmat` for mesh particles:

```json
{
  "format": "uzmat",
  "version": 1,
  "name": "debris_material",
  "shader": "shaders/mesh_particle_instanced.uzshader",
  "texture_maps": {
    "albedo": "textures/rock_albedo.dds",
    "normal": "textures/rock_normal.dds",
    "roughness": "textures/rock_roughness.dds"
  },
  "parameters": {
    "metallic": 0.0,
    "ior": 1.5
  }
}
```

## Rendering Modes

### Opaque Rendering

For solid debris, leaf particles, etc.:

```cpp
ParticleMeshPassDesc desc;
desc.isTransparent = false;
// Result: depth writes enabled, particles depth-tested against scene
```

### Transparent Rendering

For ghostly crystals, translucent ice shards, etc.:

```cpp
ParticleMeshPassDesc desc;
desc.isTransparent = true;
// Result: depth writes disabled, alpha blending with background
```

## Performance Analysis

### GPU Cost per Frame

| Operation | Cost (RTX 3080) |
|-----------|-----------------|
| Instance CS dispatch (100k particles) | 2–3 ms |
| Graphics pass (100k instances, simple mesh) | 3–5 ms |
| **Total** | 5–8 ms @ 1080p |

### Scalability

- **Low** (125k particles, Medium LOD): 2–4 ms
- **Medium** (250k particles, High LOD): 4–7 ms
- **High** (500k particles, Ultra LOD): 8–12 ms

### Memory Overhead

| Per Emitter | Size |
|-------------|------|
| Instance transform buffer (512 particles) | 512 × 64 B = 32 KB |
| Indirect args buffer | 20 B (negligible) |
| **Total** | ~32 KB per emitter |

For 50 mesh emitters at 512 particles each: ~1.6 MB (trivial).

## Optimization Tips

1. **Keep mesh complexity low** — Use LOD meshes (4–16 triangles) for particles, not hero models
2. **Disable shadow casting** — Unless critical for visual fidelity; shadow depth/complexity grows O(particles)
3. **Use Random rotation for dense swarms** — Avoids velocity computation every frame
4. **Batch similar emitters** — Group by mesh to reuse vertex/index buffers
5. **Tune screen-size culling** — Lower `minScreenPixels` to cull tiny far-away instances sooner
6. **Use deferred transparency** — Transparent mesh particles can be rendered after lighting to save PS invocations

## Common Issues

### Meshes Rendered at Wrong Scale

**Problem**: Particles are huge or tiny compared to intended size.

**Solution**: Check `meshScale` in config. If mesh asset itself is the wrong size, either:
- Adjust scale in `.uzvfx` config
- Re-export mesh from DCC tool at correct size
- Use non-uniform scale: `"mesh_scale": [0.5, 0.5, 1.0]` to squash or stretch

### Particles Not Rotating as Expected

**Problem**: All instances face the same direction regardless of `rotationMode`.

**Solution**:
- Verify `rotationMode` is set correctly (case-sensitive: `"None"`, `"VelocityAligned"`, `"Random"`)
- For VelocityAligned, ensure particles have non-zero velocity at spawn
- For Random, check that particle indices are unique (they should be by default)

### Shadow Artifacts on Mesh Particles

**Problem**: Shadow depth passes show corrupted or repeated geometry.

**Solution**:
- Set `castShadow: false` for non-critical effects
- If shadows are needed, ensure instance count buffer is correct before shadow dispatch
- Consider using sphere occluders instead of mesh shadows for dense systems

### DrawIndirect Call Fails

**Problem**: Graphics command fails with "Invalid argument buffer" or similar error.

**Solution**:
- Verify instance count UAV is correctly transitioned to IndirectArgument state
- Check that indirect args buffer is properly initialized by the instance CS (thread 0)
- Ensure `meshIndexCount` in CS constants matches the actual mesh index count

## Related Documentation

- [VFX Modules](03-vfx-modules.md) — Module architecture
- [Particle System](02-particle-system.md) — Emitter & compute details
- [LOD & Budget System](18-lod-budget-system.md) — Culling mesh emitters
- [.uzvfx Format](07-uzvfx-format.md) — Asset configuration

## Phase Roadmap

| Phase | Milestone | Status |
|-------|-----------|--------|
| 3.65.0 | Mesh render module & CS implementation | Complete |
| 3.66.0 | Graphics pass integration & test coverage | Complete |
| 3.67.0 | Animated mesh particles (skeletal) | Planned |
| 3.68.0 | Mesh LOD & dynamic morphing | Planned |
