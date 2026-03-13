# Scene Lighting Helpers — PerFrameCB Builder

> **Status**: Implemented (Milestone 1.2)
> **Source**: `runtime/engine/renderer/scene_lighting.h`
> **Depends on**: Camera (§14), Constant Buffers, Math Library (§02)

## Purpose

The `SceneLighting` struct and `BuildPerFrameCB()` helper consolidate scene-level lighting parameters and constant buffer population. They eliminate the ~20 lines of identical per-frame CB setup duplicated across rendering demos (pbr_test, deferred_test, mesh_test).

Without this helper, every demo repeats:

```cpp
// ❌ Before: Duplicated in every demo
PerFrameCB cb{};
cb.viewProj    = camera.GetViewProjection();
cb.invViewProj = cb.viewProj.Inverse();

const Vec3& camPos = camera.GetPosition();
cb.cameraPos  = {camPos.x, camPos.y, camPos.z, 0.0f};
cb.timeParams = {totalTime, deltaTime, static_cast<float>(frameIndex), 0.0f};

const Vec3& sunDir = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
cb.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.0f};
cb.lightColor   = {3.0f, 2.85f, 2.7f, 0.0f};
cb.ambientColor = {0.03f, 0.03f, 0.05f, 0.0f};
```

With the helper, it becomes:

```cpp
// ✅ After: Single function call
SceneLighting lighting;
lighting.sunDirection = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
lighting.sunColor     = {3.0f, 2.85f, 2.7f};
lighting.ambientColor = {0.03f, 0.03f, 0.05f};

PerFrameCB cb = BuildPerFrameCB(camera, totalTime, deltaTime, frameIndex, lighting);
```

## SceneLighting Struct

Describes the scene's primary light environment in a format independent of the raw `PerFrameCB` GPU layout.

```cpp
struct SceneLighting {
    /// Normalised direction toward the primary directional light source.
    ///
    /// Convention: points FROM the surface TOWARD the light (same as shader
    /// `lightDir`). Must be unit length.
    Vec3 sunDirection = Vec3{0.5f, 0.3f, 0.8f}.Normalized();

    /// Primary directional light color multiplied by intensity (linear HDR).
    ///
    /// Values > 1.0 are valid and expected for sunlight (typically 2.0–5.0).
    Vec3 sunColor = {3.0f, 2.85f, 2.7f};

    /// Ambient light color contribution (constant term in lighting equation).
    ///
    /// Represents indirect illumination. Keep low (0.01–0.1) for outdoor
    /// scenes. Higher values flatten contrast.
    Vec3 ambientColor = {0.03f, 0.03f, 0.05f};
};
```

### Fields

| Field | Type | Default | Notes |
|-------|------|---------|-------|
| `sunDirection` | `Vec3` | (0.5, 0.3, 0.8) normalized | Unit vector pointing toward light |
| `sunColor` | `Vec3` | (3.0, 2.85, 2.7) | Linear HDR color, often > 1.0 |
| `ambientColor` | `Vec3` | (0.03, 0.03, 0.05) | Low ambient keeps good contrast |

### Design Rationale

Separating `SceneLighting` from `PerFrameCB`:
- **Decoupling**: Lighting data flows independently from GPU layout
- **Reusability**: Same lighting parameters can be used by multiple systems
- **Clarity**: Intent is explicit (this is lighting, not a raw GPU buffer)
- **Extensibility**: Can add new lighting parameters (shadowmap data, IBL, etc.) without changing CB layout

---

## BuildPerFrameCB Helper

Populates a `PerFrameCB` from camera state, timing, and lighting in a single call.

```cpp
[[nodiscard]] inline PerFrameCB BuildPerFrameCB(
    const Camera&        camera,
    float                totalTime,
    float                deltaTime,
    uint32_t             frameIndex,
    const SceneLighting& lighting)
```

### Parameters

| Parameter | Type | Description |
|-----------|------|-------------|
| `camera` | `const Camera&` | Camera providing view/projection matrices and world position |
| `totalTime` | `float` | Total elapsed time in seconds since application start |
| `deltaTime` | `float` | Frame delta time in seconds (1/FPS for fixed timestep) |
| `frameIndex` | `uint32_t` | Current frame number (cast to float for shader temporal patterns) |
| `lighting` | `const SceneLighting&` | Scene lighting parameters (sunDirection, sunColor, ambientColor) |

### Returns

A fully populated `PerFrameCB` ready for GPU upload. All fields are valid and properly formatted.

### Fields Populated

| CB Field | Source |
|----------|--------|
| `viewProj` | `camera.GetViewProjection()` |
| `invViewProj` | Inverse of `viewProj` (computed on CPU) |
| `cameraPos` | `{camera.GetPosition().x/y/z, 0.0}` |
| `timeParams` | `{totalTime, deltaTime, float(frameIndex), 0.0}` |
| `lightDir` | `{lighting.sunDirection.x/y/z, 0.0}` |
| `lightColor` | `{lighting.sunColor.x/y/z, 0.0}` |
| `ambientColor` | `{lighting.ambientColor.x/y/z, 0.0}` |

All coordinates are converted to `Vec4` for GPU packing (W = 0 for direction vectors, W = padding for color).

### Usage Examples

#### Example 1: Basic Setup

```cpp
Camera camera;
camera.SetPerspective(XM_PIDIV4, 16.0f / 9.0f, 0.1f, 1000.0f);
camera.SetPosition({0, 2, -5});
camera.LookAt({0, 0, 0});

SceneLighting lighting;
lighting.sunDirection = Vec3{1.0f, 1.0f, 1.0f}.Normalized();
lighting.sunColor     = {3.5f, 3.0f, 2.5f};  // Warm sunlight
lighting.ambientColor = {0.05f, 0.05f, 0.1f};

PerFrameCB cb = BuildPerFrameCB(camera, 12.5f, 0.016f, 750, lighting);
// cb now contains all matrices, timing, and lighting
```

#### Example 2: Time-Varying Lighting

```cpp
SceneLighting lighting;
float timeOfDay = fmod(totalTime, 120.0f) / 120.0f;  // 120 sec cycle
float sunAngle = timeOfDay * XM_2PI;

// Rotate sunlight over the course of a day
lighting.sunDirection = {
    cosf(sunAngle),
    0.5f + 0.3f * sinf(sunAngle),
    sinf(sunAngle)
}.Normalized();

// Dim light at night
float brightness = 2.0f + 2.0f * (0.5f + 0.5f * sinf(sunAngle));
lighting.sunColor = {
    brightness * 0.9f,
    brightness * 0.85f,
    brightness * 0.75f
};

PerFrameCB cb = BuildPerFrameCB(camera, totalTime, deltaTime, frameIndex, lighting);
```

#### Example 3: Multiple Scenes with Different Lighting

```cpp
struct SceneConfig {
    Camera camera;
    SceneLighting lighting;
    const char* name;
};

SceneConfig scenes[] = {
    {
        // Outdoor scene: bright sun, blue sky ambient
        camera: /* configured */,
        lighting: {
            sunDirection: Vec3{0.7f, 0.7f, 0.7f}.Normalized(),
            sunColor: {4.0f, 3.8f, 3.6f},
            ambientColor: {0.05f, 0.08f, 0.15f}  // Blue sky
        },
        name: "Outdoor"
    },
    {
        // Indoor scene: dim overcast light
        camera: /* configured */,
        lighting: {
            sunDirection: Vec3{0.0f, 1.0f, 0.0f},
            sunColor: {1.5f, 1.5f, 1.5f},
            ambientColor: {0.08f, 0.08f, 0.08f}
        },
        name: "Indoor"
    }
};

for (auto& scene : scenes) {
    PerFrameCB cb = BuildPerFrameCB(
        scene.camera,
        totalTime,
        deltaTime,
        frameIndex,
        scene.lighting
    );
    // Use cb for rendering this scene
}
```

---

## Integration with Render Passes

### PBR Pass Example

```cpp
SceneLighting lighting;
lighting.sunDirection = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
lighting.sunColor     = {3.0f, 2.85f, 2.7f};
lighting.ambientColor = {0.03f, 0.03f, 0.05f};

// Build CB before registering pass
PerFrameCB perFrameCB = BuildPerFrameCB(camera, totalTime, deltaTime, frameIndex, lighting);

// Upload to GPU ring allocator
CBAllocation perFrameAlloc = cbRingAllocator.Allocate(sizeof(PerFrameCB));
memcpy(perFrameAlloc.cpuPtr, &perFrameCB, sizeof(perFrameCB));

// Register pass with pre-filled CB
PBRPassDesc desc{};
desc.perFrameAlloc = perFrameAlloc;
// ... other fields
AddPBRPass(builder, desc);
```

### Deferred (GBuffer + Lighting) Example

```cpp
PerFrameCB perFrameCB = BuildPerFrameCB(camera, totalTime, deltaTime, frameIndex, lighting);

// Same CB used for both passes (camera only, no light changes between)
CBAllocation frameAlloc = cbRingAllocator.Allocate(sizeof(PerFrameCB));
memcpy(frameAlloc.cpuPtr, &perFrameCB, sizeof(perFrameCB));

GBufferPassDesc gDesc{};
gDesc.perFrameAlloc = frameAlloc;
AddGBufferPass(builder, gDesc);

LightingPassDesc lDesc{};
lDesc.perFrameAlloc = frameAlloc;  // Reuse same CB
AddLightingPass(builder, lDesc);
```

---

## Shader Integration

The `PerFrameCB` is bound to the pixel/compute shader via `PerFrameCB` register definitions.

### HLSL Usage

```hlsl
cbuffer PerFrameCB : register(b1) {
    float4x4 viewProj;
    float4x4 invViewProj;
    float4   cameraPos;      // Position in XYZ, W unused
    float4   timeParams;     // totalTime, deltaTime, frameIndex, padding
    float4   lightDir;       // Direction TO light (normalized), W unused
    float4   lightColor;     // RGB color, W unused
    float4   ambientColor;   // RGB color, W unused
};
```

### Example Pixel Shader

```hlsl
float4 PSMain(VSOutput input) : SV_Target {
    float3 normal = normalize(input.normalWS);
    float3 toLight = lightDir.xyz;  // From BuildPerFrameCB
    float3 albedo = GetAlbedo(input.uv);

    float diffuse = saturate(dot(normal, toLight));
    float3 lit = albedo * (lightColor.rgb * diffuse + ambientColor.rgb);
    return float4(lit, 1.0);
}
```

---

## Rules & Best Practices

### Lighting Parameters

1. **sunDirection MUST be normalized** — the shader assumes unit length for dot products
2. **sunColor values can exceed 1.0** — use HDR values (typical range: 2.0–5.0 for sunlight)
3. **ambientColor should be dim** — typical range 0.01–0.1 to avoid washing out contrast
4. **Blue bias for outdoor ambient** — matches real-world sky contribution (more blue at zenith)

### Timing Parameters

1. **totalTime is cumulative** — increases monotonically from application start
2. **deltaTime should match frame duration** — for temporal effects, use 1/60 for 60 FPS
3. **frameIndex enables temporal sampling** — useful for jitter, dither, and frame-dependent patterns

### Constant Buffer Updates

1. **Call BuildPerFrameCB once per frame** — before rendering any geometry
2. **Reuse same CB for forward rendering** — single lighting state per frame
3. **Reuse CB for deferred geometry + lighting** — camera/timing don't change between passes
4. **Update only when parameters change** — skip if lighting/camera/time static

---

## Performance Notes

- **Inline function**: `BuildPerFrameCB` is inline, zero overhead
- **No allocations**: Stack-only operations, suitable for frame-critical paths
- **Compile-time constant defaults**: SceneLighting defaults are constexpr, optimize to immediate values
- **Matrix inversion cost**: Computed on CPU once per frame (negligible vs render cost)

---

## Related Documentation

- [Camera System](../14-camera/01-camera-system.md) — Camera class and GetViewProjection()
- [Constant Buffers](../12-rendering/constant_buffers.h) — GPU CB layout definition
- [PBR Pass](10-renderer-passes.md#pbr-pass--forward-rendering) — Pass integration example
- [GBuffer Pass](10-renderer-passes.md#gbuffer-pass--deferred-geometry-stage) — Deferred pass example
- [Math Library](../02-math/02-transform.md) — Vec3, Mat4 types

---

## Examples in Codebase

See the following demos for usage examples:

- `runtime/demos/pbr_test/pbr_test_demo.cpp` — Forward rendering with BuildPerFrameCB
- `runtime/demos/deferred_test/deferred_test_demo.cpp` — Deferred rendering with BuildPerFrameCB
