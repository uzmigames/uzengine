# Sort-Key Architecture — Draw Call Ordering & Batching

> **Status**: Implemented (Milestone 1.2)
> **Source**: `runtime/engine/renderer/draw_list.h`
> **Depends on**: Material System (§06-pbr-material-system.md)
> **References**: "Order Your Graphics Render Calls Around!!" (GDC 2018)

## Purpose

The sort key is a 64-bit value that encodes the **most expensive GPU state changes** in the most-significant bits. When draw commands are sorted ascending by this key, GPU state transitions (PSO switches, descriptor table changes) happen minimally, improving rendering performance.

The key supports both **front-to-back opaque rendering** (minimizes pixel shader overdraw) and **back-to-front transparent rendering** (maintains correct blending order).

## 64-Bit Key Layout

The sort key encodes **four orthogonal dimensions** of draw state:

```
63 ─────────────────────────────────────── 0 (least significant bit)
│┌───────────┬───────────┬──────────────┬──────────────┐
││  Pipeline │ Material  │    Depth     │     Mesh     │
│└───────────┴───────────┴──────────────┴──────────────┘
│[63:56]    [55:48]    [47:24]       [23:0]
```

### Bit Fields (Most → Least Significant)

| Bits | Field | Width | Purpose | Notes |
|------|-------|-------|---------|-------|
| 63–56 | Pipeline/PSO ID | 8 bits | Graphics pipeline / shader variant | 0–255; PSO changes are most expensive |
| 55–48 | Material ID | 8 bits | Material StructuredBuffer index | 0–255; texture binding is second-most expensive |
| 47–24 | Depth | 24 bits | Linearized/normalized scene depth | **Opaque: front-to-back (ascending)**; **Transparent: back-to-front (descending)** |
| 23–0 | Mesh ID | 24 bits | Mesh resource index | 0–16M; VB/IB rebind is least expensive |

### Why This Order?

**Greedy state change reduction**: Ascending sort by this key minimizes state transitions in order of **cost**:

1. **Pipeline changes first** (bits 63–56) — PSO switches are expensive (CPU validation, hash lookup). Group by PSO.
2. **Material changes second** (bits 55–48) — Descriptor table or texture binding changes. Group by material within each PSO.
3. **Depth third** (bits 47–24) — Opaque: front-to-back minimizes pixel shader work. Transparent: back-to-front ensures correct blending.
4. **Mesh last** (bits 23–0) — Vertex/index buffer rebinds are cheap (just root constant writes on DX12). Group mesh commands together.

**Result**: A typical frame's draw call list is naturally organized into "chunks":

```
Draw calls 0–10:   PSO=0, Materials 5–7, Meshes 100–102
Draw calls 11–45:  PSO=0, Materials 8–12, Meshes 50–80
Draw calls 46–80:  PSO=1, Materials 1–4, Meshes 200–210
...
```

## Construction Helpers

### Opaque Sort Key (Front-to-Back)

```cpp
inline uint64_t Opaque(uint8_t pipelineId,
                       uint8_t materialId,
                       float   depth,
                       uint32_t meshId) noexcept
{
    // Clamp depth to [0, 1]
    const float clamped = (depth < 0.0f) ? 0.0f : (depth > 1.0f) ? 1.0f : depth;

    // Map [0, 1] → [0, 16777215] (2^24 - 1)
    const uint32_t depthBits = (uint32_t)(clamped * 16777215.0f);

    return (uint64_t(pipelineId) << 56) |
           (uint64_t(materialId) << 48) |
           (uint64_t(depthBits) << 24) |
           (uint64_t(meshId) & 0x00FF'FFFFu);
}
```

**Behavior**:

- Smaller depth values → smaller keys → drawn first (near objects)
- Larger depth values → larger keys → drawn last (far objects)
- **Minimizes pixel shader overdraw**: Close geometry is drawn first; far geometry fails depth test early

**Use for**: Opaque geometry (walls, buildings, characters)

### Transparent Sort Key (Back-to-Front)

```cpp
inline uint64_t Transparent(uint8_t pipelineId,
                            uint8_t materialId,
                            float   depth,
                            uint32_t meshId) noexcept
{
    // Clamp depth to [0, 1]
    const float clamped = (depth < 0.0f) ? 0.0f : (depth > 1.0f) ? 1.0f : depth;

    // Invert depth: larger distances → smaller keys → drawn first
    const uint32_t depthBits = (uint32_t)((1.0f - clamped) * 16777215.0f);

    return (uint64_t(pipelineId) << 56) |
           (uint64_t(materialId) << 48) |
           (uint64_t(depthBits) << 24) |
           (uint64_t(meshId) & 0x00FF'FFFFu);
}
```

**Behavior**:

- Larger depth values → smaller inverted values → drawn first (far objects)
- Smaller depth values → larger inverted values → drawn last (near objects)
- **Preserves correct alpha blending**: Far transparent objects blend first; near objects blend on top

**Use for**: Transparent geometry (glass, foliage, particles, water surfaces)

## Draw List Integration

### DrawCommand Structure

```cpp
struct DrawCommand {
    uint64_t sortKey;      ///< [63:56] PSO | [55:48] Mtl | [47:24] Depth | [23:0] Mesh
    Mat4     worldMatrix;   ///< Object-to-world transform
    Mat4     normalMatrix;  ///< transpose(inverse(worldMatrix))
    uint32_t meshId;       ///< Index into mesh array
    uint32_t materialId;   ///< Index into MaterialBuffer
};
```

### Typical Per-Frame Workflow

```cpp
// 1. Clear the draw list at frame start
drawList.Clear();

// 2. Iterate visible geometry and record commands
for (const auto& renderable : visibleRenderables) {
    DrawCommand cmd;

    // Compute screen-space depth (0 = near, 1 = far)
    float depth = (renderable.worldPos - cameraPos).Dot(cameraForward);
    float normalizedDepth = (depth - zNear) / (zFar - zNear);

    // Build sort key based on material and geometry properties
    if (renderable.material.transparent) {
        cmd.sortKey = SortKey::Transparent(
            0,                              // pipelineId (all PBR for now)
            renderable.materialId,
            normalizedDepth,
            renderable.meshId
        );
    } else {
        cmd.sortKey = SortKey::Opaque(
            0,
            renderable.materialId,
            normalizedDepth,
            renderable.meshId
        );
    }

    cmd.worldMatrix = renderable.transform;
    cmd.normalMatrix = renderable.normalTransform;
    cmd.meshId = renderable.meshId;
    cmd.materialId = renderable.materialId;

    drawList.Add(cmd);
}

// 3. Sort all commands by key
drawList.Sort();

// 4. Iterate sorted list and issue GPU draw calls
for (const auto& cmd : drawList.GetCommands()) {
    // cmd is now guaranteed to have minimal state changes vs. previous cmd
    // Renderer checks if PSO/material changed and rebinds only if necessary
    IssueDrawCall(cmd);
}
```

## Depth Normalization

To map world-space depth into the 24-bit range [0, 16777215], normalize by the camera's near/far planes:

```cpp
// Input: world-space distance from camera along view direction
float worldDepth = (meshWorldPos - cameraPos).Dot(cameraForward);

// Normalize to [0, 1] where 0 = near plane, 1 = far plane
float normalizedDepth = (worldDepth - zNear) / (zFar - zNear);

// Clamp to valid range (sort key functions do this too, but be explicit)
normalizedDepth = (normalizedDepth < 0.0f) ? 0.0f :
                  (normalizedDepth > 1.0f) ? 1.0f : normalizedDepth;
```

### Reversed-Z Considerations

If using reversed-Z depth (1.0 = near, 0.0 = far), adjust the normalization:

```cpp
// Reversed-Z: z = 1 - (depth / farPlane)
float reversedZ = 1.0f - (worldDepth / zFar);
```

The 24-bit encoding then naturally sorts correctly (smaller reversed-Z values = near objects).

## Practical Limits

### Pipeline ID (8 bits, 0–255)

- Forward PBR pass uses ID = 0
- Shadow map pass uses ID = 1
- Post-processing uses ID = 2–5
- Leaves 250+ IDs for future passes or variants

**Typical usage**: 1–3 pipeline IDs per frame (forward opaque, forward transparent, shadow)

### Material ID (8 bits, 0–255)

- Index 0: Default material (white, non-metallic, roughness 0.5)
- Index 1–255: User-created materials
- **Limit**: 256 unique materials per scene

**Why limit to 256?** The 8-bit encoding keeps the sort key compact and prevents deep descriptor table stalls. For scenes exceeding 256 materials, use material atlasing or dynamic batching.

### Mesh ID (24 bits, 0–16,777,215)

- Allows 16M+ unique meshes
- In practice, limited by VRAM and draw call count (typically 10k–100k draw calls/frame)

### Depth (24 bits, 0–16,777,215)

- 16M unique depth values
- Achieves sub-pixel precision for depth-based sorting
- Mitigates flickering when objects have nearly identical depth

## Batching Strategy

The sort key naturally batches draw calls by state:

```
────────────────────────────────────────────────────
PSO #0 (Forward PBR)                          Batch 1
  Material #5 (Brick)          Sub-batch 1a
    Mesh #100                       Mesh change 1
    Mesh #101                       (same PSO, material, different mesh)
    Mesh #100                       Mesh change 2
  Material #6 (Metal)          Sub-batch 1b
    Mesh #50                        Mesh change 3
────────────────────────────────────────────────────
PSO #1 (Shadow Map)                          Batch 2
  Material #0 (Default)        Sub-batch 2a
    Mesh #100                       (PSO changed; material re-bound)
    Mesh #101
────────────────────────────────────────────────────
```

**GPU state changes per frame**:

- PSO changes: ~1–3 (one per major render pass)
- Descriptor table changes: ~3–10 (one per material)
- VB/IB rebinds: ~10–100 (one per mesh)

Compare to **unsorted** draw list: ~10k PSO changes, ~50k descriptor rebinds (depending on draw count).

## Stability & Sorting

The `DrawList::Sort()` uses `std::sort`, which is **not stable**. Draw commands with identical sort keys may appear in any order. This is acceptable because:

1. **Equal sort keys = identical state** → order doesn't affect visual correctness
2. **Equal keys = rare** → only occurs when geometry has identical PSO/material/depth
3. **Stability would cost performance** → std::stable_sort is 10–20% slower

If stable ordering is required (e.g., to avoid flicker in edge cases), use `std::stable_sort`:

```cpp
void DrawList::StableSort() {
    std::stable_sort(m_commands.begin(), m_commands.end(),
        [](const DrawCommand& a, const DrawCommand& b) noexcept {
            return a.sortKey < b.sortKey;
        });
}
```

## Engine Comparison

| Aspect | Unreal 5 | Godot 4 | **UzEngine 1.2** |
|--------|----------|---------|------------------|
| Sort key format | Per-platform (varies) | Custom key per layer | **Fixed 64-bit key** |
| Opaque order | Front-to-back (distance culling) | Ascending z | **Front-to-back (normalized)** |
| Transparent order | Back-to-front (Porter-Duff) | Back-to-front (z-inverted) | **Back-to-front (inverted key)** |
| Material batching | PSO cache → material index | Shader binding → sampler | **8-bit material ID in key** |
| Mesh batching | Draw list iteration → VB switch | Dynamic batching (experimental) | **24-bit mesh ID in key** |
| Key generation | Per-pass (FMeshBatch builder) | Per-material (auto) | **Explicit in visibility system** |

## Rules

1. **ALWAYS use `SortKey::Opaque()` for non-transparent geometry** — front-to-back minimizes overdraw
2. **ALWAYS use `SortKey::Transparent()` for transparent geometry** — back-to-front ensures correct blending
3. **NEVER mix transparent and opaque commands in the same sort key** — they require opposite depth ordering
4. **ALWAYS normalize depth to [0, 1]** — clamping prevents UB in the bit conversion
5. **NEVER use material ID > 255** — sort key encoding limits to 8 bits
6. **ALWAYS sort once per frame** — after all commands are recorded, before iteration
7. **NEVER rely on sort stability** — commands with equal keys may reorder

## GPU Instancing Integration

After sorting and batching, the sorted DrawList can be processed by the **InstanceBatcher** to collapse consecutive commands with identical mesh + material texture indices into instanced draw calls. This reduces thousands of draw calls to dozens while maintaining all per-object state.

### InstanceData — Per-Instance Transforms

Each instance carries its own world transform and normal matrix (required for per-vertex lighting):

```cpp
struct InstanceData {
    Mat4 world;            // Object-to-world transform (64 bytes)
    Mat4 normalMatrix;     // transpose(inverse(world)) for lighting (64 bytes)
};
// Total: 128 bytes per instance
static_assert(sizeof(InstanceData) == 128);
```

These are stored in a GPU `StructuredBuffer<InstanceData>` (Upload heap) and indexed via `SV_InstanceID` in the vertex shader. All instances in a single batch share the same material (textures), but each has unique geometry transforms.

### Instance Buffer Management

The instance buffer grows dynamically in power-of-two increments (1024 → 2048 → 4096):

- **Triple-buffered** — one buffer per frame-in-flight (prevents GPU stalls from CPU overwrites)
- **Capacity**: Initially 1024, grows to 2× on demand (e.g., `1024 → 2048 → 4096`)
- **Upload heap** — CPU-writable from `BuildInstanceBatches()` frame-local allocation
- **Max per frame** — 16,384 instances; excess commands logged and dropped

### Batching Process

```cpp
// After sorting:
drawList.Sort();

// Group consecutive commands with the same mesh and material into batches
std::vector<InstanceBatch> batches;
std::vector<InstanceData> instances;
BuildInstanceBatches(drawList, batches, instances);

// Upload instances to GPU (frame-local allocation, CPU-visible memory)
instanceBuffer.Upload(instances.data(), instances.size());

// Issue one DrawIndexedInstanced per batch instead of N separate DrawIndexed calls
for (const auto& batch : batches) {
    pso->Bind();
    instanceBuffer.Bind();  // Root slot 0: instanceBufferIndex
    meshes[batch.meshId]->Bind();
    materials[batch.materialId]->Bind();

    // One draw call for N instances
    cmd->DrawIndexedInstanced(
        mesh->indexCount,     // Indices per instance
        batch.instanceCount,  // Number of instances
        0,                    // First index
        0,                    // Vertex offset
        batch.firstInstance   // First instance
    );
}
```

### Batching Criteria

Two consecutive sorted commands merge into a single `InstanceBatch` if **ALL** of these match:

- `meshId` — same mesh resource
- `albedoSrvIndex` — same base color texture (SRV descriptor heap index)
- `normalSrvIndex` — same normal map
- `roughnessSrvIndex` — same roughness map
- `metallicSrvIndex` — same metallic map
- `aoSrvIndex` — same ambient occlusion map

**Why these criteria?** Material texture indices are shared across all instances in a batch via a single `PerObjectCB`, so they must be identical. Mesh ID is shared because one batch = one index buffer. Only the per-instance transforms (world matrix, normal matrix) differ.

### Shader Integration (SM6.6 Bindless)

The vertex shader accesses instance data via `SV_InstanceID` (built-in):

```hlsl
#ifdef UZ_INSTANCED
    StructuredBuffer<InstanceData> instances : register(t0);

    uint instanceID = input.instanceID;  // 0 to instanceCount-1
    InstanceData inst = instances[instanceID];

    float4x4 worldMatrix = inst.world;
    float4x4 normalMatrix = inst.normalMatrix;

    float3 worldPos = mul(float4(vert.position, 1.0), worldMatrix).xyz;
    float3 worldNorm = normalize(mul(vert.normal, (float3x3)normalMatrix));
#endif
```

**SM5 fallback** (non-bindless): Instancing is disabled on SM5 devices. The renderer falls back to per-object CB path (non-instanced draw calls).

### Performance Example

**1001 identical cubes with Stonebricks material:**

- **Without instancing**: 1001 separate `DrawIndexed` calls → 1001 root constant updates, 1001 descriptor table rebinds
- **With instancing**: 1–2 `DrawIndexedInstanced` calls → near-zero state changes, 310 FPS @ 3.22ms on RTX 4090

**Key insight**: The frame time is GPU-bound (not CPU-bound), so draw call reduction alone doesn't improve FPS. However, instancing **improves CPU efficiency** (enables larger scenes) and **enables better GPU caching** (fewer state transitions → better hit rates).

See [08-gpu-instancing.md](08-gpu-instancing.md) for full implementation details, buffer layout, and integration checklist.

## Future Enhancements

- **Adaptive key granularity** — use finer depth resolution (32 bits) if overdraw becomes a bottleneck
- **Multi-pass sort keys** — encode shadow pass vs. deferred vs. forward in the pipeline ID
- **Material atlasing** — reduce material ID range by grouping textures into atlases
- **Indirect draw buffers** — GPU-driven instance batching via compute shaders
