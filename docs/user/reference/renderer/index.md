# Renderer Module Reference

Renderer utilities: vertex formats and GPU upload management.

> **Headers**: `runtime/engine/renderer/` (vertex, upload context, render graph) | **Guide**: [Rendering](../../guides/rendering/overview.md)

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

## Camera (`camera.h`)

Perspective and orthographic camera with Z-up coordinate system (left-handed: X-forward, Y-right, Z-up).

### Perspective Projection

Creates a view frustum with field-of-view, aspect ratio, and near/far planes.

```cpp
class PerspectiveCamera {
    /// Set perspective projection and update ViewProj matrix
    void SetPerspective(float fovDegrees, float aspectRatio, float nearClip, float farClip);
};
```

### Orthographic Projection

Creates a parallel projection (no perspective distortion).

```cpp
class OrthographicCamera {
    /// Set orthographic projection (left, right, bottom, top, near, far)
    void SetOrthographic(float left, float right, float bottom, float top, float nearClip, float farClip);
};
```

### Z-Up Coordinate System

The engine uses a **left-handed Z-up coordinate system**: X points forward, Y points right, Z points up.
This differs from DirectXMath's default (Y-up), so cameras convert internally:

```cpp
/// Convert Z-up quaternion to Y-up for DirectXMath LookAt
Quat ToYUp(const Quat& zUpQuat);

/// Convert Y-up quaternion back to Z-up for storage
Quat FromYUp(const Quat& yUpQuat);
```

Internally, cameras store orientation as Y-up quaternions for consistency with DirectXMath.
The public API and view matrix construction handle the conversion transparently.

### View Matrix

Computes view matrix from position and quaternion orientation.

```cpp
Mat4 GetViewMatrix() const;  // Returns matrix with Z-up → Y-up conversion applied
```

The view matrix uses XMMatrixLookToLH after swapping Z-up to Y-up coordinates.

### Related Pages

- [RHI Module](../rhi/index.md) — Device, command lists, buffers
- [Rendering Overview](../../guides/rendering/overview.md) — Graphics subsystem
- [Math Module](../math/index.md) — Vec2, Vec3, Vec4, Quat, Mat4 types
