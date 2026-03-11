# Renderer Module Reference

Renderer utilities: vertex formats and GPU upload management.

> **Headers**: `core/renderer/` (3 files) | **Guide**: [Rendering](../../guides/rendering/overview.md)

---

## Vertex (`vertex.h`)

Standard vertex layouts for 3D mesh and debug geometry rendering.

All vertex data is uploaded as StructuredBuffers and accessed via SM6.6 bindless
`ResourceDescriptorHeap[index]` in shaders. No Input Assembler vertex layout — vertex
fetch is entirely in the vertex shader.

### Vertex (48 bytes)

Full PBR mesh vertex with position, normal, UV, and tangent.

```cpp
struct Vertex {
    Vec3 position;   // Object-space position
    Vec3 normal;     // Object-space normal (unit length)
    Vec2 texcoord;   // UV coordinates
    Vec4 tangent;    // Tangent (xyz) + handedness sign (w)
};
```

Matches the HLSL struct:
```hlsl
struct Vertex {
    float3 position;
    float3 normal;
    float2 texcoord;
    float4 tangent;  // w = handedness (+1 or -1)
};
```

### SimpleVertex (36 bytes)

Minimal vertex for debug lines, UI, and test geometry.

```cpp
struct SimpleVertex {
    Vec3 position;   // Object-space position
    Vec3 normal;     // Normal vector
    Vec2 texcoord;   // UV coordinates
    uint32_t color;  // Packed RGBA (0xRRGGBBAA)
};
```

---

## UploadContext (`upload_context.h`)

Manages staging (upload-heap) buffers for CPU → GPU data transfer.

### Usage

```cpp
UploadContext upload(device);
upload.UploadBuffer(cmdList, dstBuffer, data, size);
// ... record more uploads ...
cmdList->End();
cmdList->Execute();
device->WaitIdle();          // Wait for GPU copy to finish
upload.ReleaseStaging();     // Free staging buffers
```

### Methods

| Method | Description |
|--------|-------------|
| `UploadBuffer(cmdList, dst, data, size)` | Create staging buffer and record CopyBuffer |
| `ReleaseStaging()` | Free all staging buffers (MUST wait for GPU first) |

### Ownership

The `UploadContext` holds staging buffers alive until `ReleaseStaging()`.
The caller MUST wait for the GPU copy to complete (via fence) before releasing,
or the GPU will read freed memory.

**Thread Safety**: NOT thread-safe — use from the thread that owns the command list.

---

## Related Pages

- [RHI Module](../rhi/index.md) — Device, command lists, buffers
- [Rendering Overview](../../guides/rendering/overview.md) — Graphics subsystem
- [Math Module](../math/index.md) — Vec2, Vec3, Vec4 types used in vertices
