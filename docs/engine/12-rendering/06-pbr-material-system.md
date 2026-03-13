# PBR Material System — Data Layout & GPU Upload

> **Status**: Implemented (Milestone 1.2)
> **Source**: `runtime/engine/renderer/material.h`, `runtime/engine/renderer/material_buffer.h`
> **Depends on**: RHI (§03), Render Graph (§04)
> **References**: Cook-Torrance BRDF (§04-cook-torrance-brdf.md), DirectX 12 StructuredBuffer

## Purpose

The PBR Material System provides a lightweight, GPU-uploadable material representation. Each material encodes Cook-Torrance BRDF parameters (albedo, metallic, roughness, ambient occlusion, emissive). Materials are stored in a StructuredBuffer on the GPU and referenced by index from per-object constant buffers during rendering.

This design decouples the logical material definition from the draw call, enabling material-based sort keys and efficient batching.

## PBRMaterial Struct — 48 Bytes

The `PBRMaterial` struct is a **packed POD (Plain Old Data)** type that matches the HLSL layout exactly.

### Layout (C++ Side)

```cpp
struct PBRMaterial {
    Vec4   baseColor;   ///< Albedo color in linear sRGB (RGBA)
    float  metallic;    ///< [0, 1] — 0 = dielectric, 1 = fully metallic
    float  roughness;   ///< [0, 1] — perceptual roughness (remapped to alpha²)
    float  ao;          ///< [0, 1] — ambient occlusion multiplier
    float  _pad0;       ///< Explicit alignment padding (must remain zero)
    Vec4   emissive;    ///< Emissive color + intensity (RGBA)
};

static_assert(sizeof(PBRMaterial) == 48);
```

**Byte layout:**

```
 0 –  3:  baseColor.x (float)
 4 –  7:  baseColor.y (float)
 8 – 11:  baseColor.z (float)
12 – 15:  baseColor.w (float) [opacity]
16 – 19:  metallic (float)
20 – 23:  roughness (float)
24 – 27:  ao (float)
28 – 31:  _pad0 (float) [zeros]
32 – 35:  emissive.x (float)
36 – 39:  emissive.y (float)
40 – 43:  emissive.z (float)
44 – 47:  emissive.w (float) [intensity scale]
```

### HLSL Layout (Must Match Exactly)

```hlsl
struct PBRMaterial {
    float4 baseColor;   // rgb = albedo (linear sRGB), a = opacity
    float  metallic;    // 0 = dielectric, 1 = fully metallic
    float  roughness;   // 0 = mirror surface, 1 = fully diffuse
    float  ao;          // ambient occlusion multiplier [0, 1]
    float  _pad0;       // explicit padding for HLSL alignment
    float4 emissive;    // rgb = emissive color, a = intensity scale
};
```

### Field Descriptions

| Field | Type | Range | Purpose | Default |
|-------|------|-------|---------|---------|
| `baseColor` | Vec4 | [0, 1] | Albedo in linear sRGB + opacity (A channel used if transparency implemented) | (1, 1, 1, 1) — white, fully opaque |
| `metallic` | float | [0, 1] | Blends between dielectric (0) and metallic (1) Fresnel responses. Intermediate values are physically unusual — prefer 0 or 1 in practice. | 0.0 — fully dielectric |
| `roughness` | float | [0, 1] | Perceptual surface roughness. Remapped to alpha = roughness² inside the shader for physically plausible NDF spread. 0 = mirror, 1 = fully diffuse. | 0.5 — matte mid-tone |
| `ao` | float | [0, 1] | Ambient occlusion multiplier. Darkens indirect ambient lighting in crevices. Typically sourced from a baked AO texture or ambient cavity map. | 1.0 — no occlusion |
| `_pad0` | float | N/A | Explicit padding to maintain HLSL alignment. **Must always be zero.** Do not repurpose. | 0.0 |
| `emissive` | Vec4 | [0, ∞] | Self-illumination color (RGB) + intensity scale (W). Final radiance = emissive.rgb * emissive.a. Allows HDR emission without changing baseColor. | (0, 0, 0, 0) — no emission |

### Defaults (Constructor)

The `PBRMaterial` constructor initializes a physically plausible diffuse white surface:

```cpp
PBRMaterial mat{};  // or explicit:
// mat.baseColor = Vec4(1.0f, 1.0f, 1.0f, 1.0f);
// mat.metallic = 0.0f;
// mat.roughness = 0.5f;
// mat.ao = 1.0f;
// mat._pad0 = 0.0f;
// mat.emissive = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
```

**Why these defaults?**

- White baseColor (1, 1, 1) reflects all incoming light without tinting
- metallic = 0 means dielectric (plastic, ceramic, wood, stone) — most common material type
- roughness = 0.5 is perceptually mid-tone; neither mirror-like nor fully diffuse
- ao = 1 means no ambient occlusion — the full ambient contribution reaches the surface
- emissive = (0, 0, 0, 0) means no self-illumination

## MaterialBuffer — GPU Storage & Uploads

The `MaterialBuffer` class manages a GPU StructuredBuffer of `PBRMaterial` elements. Each draw call references a material by index stored in `PerObjectCB.materialIndex`.

### Class Interface

```cpp
class MaterialBuffer {
public:
    /// Initialize with fixed capacity.
    /// Index 0 is reserved as the default material (white, non-metallic, roughness 0.5).
    bool Init(IRHIDevice* device, uint32_t maxMaterials = 256);

    /// Release GPU resources.
    void Shutdown();

    /// Add a material, return its index (UINT32_MAX if at capacity).
    uint32_t AddMaterial(const PBRMaterial& material);

    /// Update an existing material at the given index.
    void UpdateMaterial(uint32_t index, const PBRMaterial& material);

    /// Retrieve CPU-side copy.
    const PBRMaterial& GetMaterial(uint32_t index) const;

    /// Upload all dirty materials to the GPU buffer.
    /// Call once per frame before rendering.
    void Upload();

    /// Get the SRV index for SM6.6 bindless access.
    uint32_t GetSRVIndex() const;

    /// Get the buffer handle for SM5 descriptor table binding.
    RHIBufferHandle GetBufferHandle() const;

    /// Query counts.
    uint32_t Count() const;      ///< Current material count
    uint32_t Capacity() const;    ///< Maximum capacity
};
```

### Initialization

Materials must be initialized before the first frame and shutdown before device destruction:

```cpp
// In renderer initialization:
g_materialBuffer.Init(device, 256);  // 256-material capacity

// During frame:
uint32_t whiteMatIdx = g_materialBuffer.AddMaterial(PBRMaterial{});
uint32_t stoneMatIdx = g_materialBuffer.AddMaterial(stoneMaterial);

// End-of-frame upload (called once, before render pass execution):
g_materialBuffer.Upload();  // GPU-synced, ready for draw calls

// Before device shutdown:
g_materialBuffer.Shutdown();
```

### Lifetime & Capacity

- **Fixed capacity**: Set at `Init()` time. Cannot grow. Design conservatively (256–2048 materials typical for a scene).
- **Index 0 reserved**: The default material (white, non-metallic, roughness 0.5) is automatically created at index 0.
- **Additions return index**: `AddMaterial()` returns the material index or `UINT32_MAX` if the buffer is full.
- **CPU mirror**: Maintains a `std::vector<PBRMaterial>` on the CPU for dirty tracking and reads.

### GPU Upload Mechanism

The buffer uses an **upload-heap** StructuredBuffer with **dirty tracking**:

1. **CPU changes** — `AddMaterial()` or `UpdateMaterial()` modifies the CPU mirror and sets `m_dirty = true`
2. **Upload call** — `Upload()` (once per frame) maps the upload-heap buffer and memcpy's the entire mirror
3. **GPU access** — Shaders read from the StructuredBuffer using `materialIndex` from `PerObjectCB`

**Performance note**: Copying the entire buffer is efficient even with ~256 materials (48 bytes × 256 = 12 KB). For larger capacity, consider incremental updates.

### SM6.6 Bindless Access (Preferred Path)

In the shader, materials are accessed via the global descriptor heap:

```hlsl
// Shader side (mesh_ps.hlsl):
StructuredBuffer<PBRMaterial> materials = ResourceDescriptorHeap[materialSrvIndex];

// Per-pixel (in pixel shader):
uint matIdx = perObject.materialIndex;
PBRMaterial mat = materials[matIdx];
```

The C++ side provides the SRV index:

```cpp
uint32_t srvIdx = g_materialBuffer.GetSRVIndex();
// Pass srvIdx to the shader via render graph or root constant
```

### SM5 Fallback (Descriptor Table)

For devices without bindless support, the buffer is bound to a descriptor table:

```cpp
RHIBufferHandle bufHandle = g_materialBuffer.GetBufferHandle();
// Bind to register(t0) at shader execution time
```

## Material Indexing Strategy

### Per-Draw Material Reference

Each `DrawCommand` stores a `materialId`:

```cpp
struct DrawCommand {
    uint64_t sortKey;     ///< Encodes material ID for sorting
    Mat4     worldMatrix;
    Mat4     normalMatrix;
    uint32_t meshId;
    uint32_t materialId;  ///< Index into MaterialBuffer
};
```

During sort-key construction, the material ID is encoded in bits [55:48]:

```cpp
uint64_t Opaque(uint8_t pipelineId,
                 uint8_t materialId,       // 0–255
                 float   depth,
                 uint32_t meshId)
{
    // ...materialId encoded in bits 55:48...
}
```

### Upload to GPU

In the forward PBR pass, the material index is passed per-object:

```cpp
// In ForwardPBRPass callback:
for (const auto& cmd : drawList.GetCommands()) {
    // Allocate per-object CB:
    auto cbAlloc = cbAllocator->Allocate(sizeof(PerObjectCB));

    // Fill it:
    PerObjectCB* perObj = (PerObjectCB*)cbAlloc.cpuPtr;
    perObj->model = cmd.worldMatrix;
    perObj->normalMatrix = cmd.normalMatrix;
    perObj->materialIndex = cmd.materialId;  // Reference into MaterialBuffer

    // Bind and draw:
    cmdList->SetGraphicsRootConstantBufferView(2, cbAlloc.gpuAddr);
    cmdList->DrawIndexedInstanced(...);
}
```

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine 1.2** |
|---------|----------|---------|------------------|
| Material representation | `UMaterialInterface` (vtable graph) | Shader + sampler bindings | **POD `PBRMaterial` struct** |
| Storage | Per-material PSO cache | `ShaderMaterial` instance data | **StructuredBuffer (GPU)** |
| Parameter updates | `FMaterialRenderProxy` (thread-safe) | `set_shader_parameter()` per-instance | **`UpdateMaterial()` at sync point** |
| Lookup | Virtual dispatch in draw list | Shader slot binding | **Index via `PerObjectCB.materialIndex`** |
| Capacity | Unlimited (dynamic) | Unlimited (per ShaderMaterial) | **Fixed at init (256–2048)** |
| Complexity | ~500 lines (UMaterial) + PSO caching | ~400 lines (ShaderMaterial) | **~140 lines (MaterialBuffer)** |

## Rules

1. **NEVER exceed the MaterialBuffer capacity** — design capacity ahead of time. Overflow is silent (`AddMaterial()` returns `UINT32_MAX`).
2. **ALWAYS call `Upload()` once per frame** — before the render pass executes. No double-upload.
3. **NEVER modify materials after `Upload()` and before the next frame** — changes won't be visible until the next upload.
4. **ALWAYS initialize the buffer before any draw calls** — uninitialized access causes undefined behavior.
5. **NEVER trust material index 0** — it's reserved as the default. If you need the default material, use index 0 explicitly.
6. **ALWAYS keep C++ and HLSL layouts in sync** — any field reorder breaks rendering silently.

## Future Enhancements

- **Per-material texture maps** — extend `PBRMaterial` to store albedo/normal/roughness texture indices
- **Material variants** — support different roughness/metallic ranges per material (e.g., wet vs. dry versions)
- **Dynamic material updates** — per-frame parameter animation (e.g., fire spreading, rust progression)
- **Incremental uploads** — track dirty ranges and upload only changed materials (for 1000+ material scenes)
