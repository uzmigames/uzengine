# Trail Rendering Pipeline

> **Status**: Planned (Phase 3.64.0)
> **References**: [.uztrail Format](14-uztrail-format.md), [Trail Configuration Guide](16-trail-configuration-guide.md)

## Purpose

This document describes the complete trail/ribbon rendering pipeline: how the engine converts a chain of position samples into camera-facing ribbon geometry, uploads it to the GPU, and renders it with animated textures, color gradients, and soft depth fading.

The pipeline is divided into three stages:
1. **Point Sampling** (CPU): `TrailComponent::Tick()` emits and ages trail points
2. **Geometry Generation** (CPU): `TrailMeshBuilder` converts points to camera-facing quad-strip vertices
3. **GPU Rendering** (GPU): Shaders transform, scroll textures, and blend the ribbon

## Pipeline Overview

```
┌─────────────────────────────────────────────────────────────────────┐
│ Each Frame                                                          │
├─────────────────────────────────────────────────────────────────────┤
│ 1. TrailSystem::Update(dt, positions, cameraPos)                    │
│    ├─ TrailComponent::Tick(dt, entityPos) for each active trail     │
│    │  └─ Emit points if distance ≥ minPointDistance                │
│    │  └─ Age all points: point.age += dt                           │
│    │  └─ Cull tail points where age > pointLifetime                │
│    │  └─ Recalculate distanceFromHead (arc-length)                 │
│    │                                                                 │
│    └─ TrailMeshBuilder::Build() for each trail                     │
│       ├─ Input: 64+ TrailPoint samples (head→tail order)           │
│       ├─ For each pair of adjacent points:                         │
│       │  ├─ Compute tangent direction                              │
│       │  ├─ Compute right vector = cross(tangent, toCamera)        │
│       │  ├─ Emit 4 vertices (quad): left-left, left-right,         │
│       │  │   right-left, right-right                               │
│       │  └─ Emit 2 triangles (6 indices)                           │
│       └─ Output: TrailMeshData (vertices, indices, ready for GPU)  │
│                                                                     │
│ 2. AddTrailRenderPass(rgBuilder, desc) during frame setup           │
│    ├─ Collect TrailSystem::GetRenderData() → per-trail mesh data  │
│    ├─ Sort by (sortOrder, distanceToCamera) for back-to-front     │
│    └─ Register render graph pass                                   │
│                                                                     │
│ 3. Render Pass Execute (GPU)                                       │
│    ├─ For each trail item (back-to-front order):                  │
│    │  ├─ Bind trail pipeline (VS + PS)                            │
│    │  ├─ Upload per-trail CB (viewProj, time, scrollRate)         │
│    │  ├─ Bind vertex/index buffers (SRV t0, t1)                   │
│    │  └─ DrawIndexed(indexCount)                                  │
│    │      → Vertex shader transforms to clip space, scrolls UVs    │
│    │      → Rasterizer expands to screen-space triangles          │
│    │      → Pixel shader samples texture, applies color, alpha     │
│    └─ Output: Composite into HDR color buffer with alpha blend    │
└─────────────────────────────────────────────────────────────────────┘
```

## Stage 1: Point Sampling (TrailComponent)

### Ring Buffer Design

`TrailComponent` manages a fixed-capacity ring buffer (`TrailPointBuffer`) of trail samples.

```cpp
// Fixed-capacity ring buffer, allocated at component construction
class TrailPointBuffer {
    std::vector<TrailPoint> storage_;  // Pre-allocated array
    uint32_t head_;   // Physical index of next write slot
    uint32_t tail_;   // Physical index of oldest point
    uint32_t count_;  // Number of active points
};

// Adding point to full buffer automatically overwrites oldest (tail)
void AddPoint(const TrailPoint& p) {
    storage_[head_] = p;
    head_ = (head_ + 1) % capacity_;
    if (count_ < capacity_) {
        count_++;
    } else {
        tail_ = (tail_ + 1) % capacity_;  // Advance tail
    }
}
```

**Advantages**:
- No dynamic allocation after construction → predictable performance
- O(1) insertion and removal (circular indexing)
- Cache-friendly when points are densely packed
- Tail points are the first to age out → FIFO behavior matches trail lifetime

### Point Emission

Each frame, `TrailComponent::Tick(dt, currentPosition)` evaluates whether to emit a new point:

```cpp
void TrailComponent::Tick(float dt, const Vec3& currentPosition) {
    if (!isActive) return;

    // Step 1: Check if emitter has moved far enough
    float distance = (currentPosition - lastAddedPosition).Length();
    if (points.Count() == 0 || distance >= minPointDistance) {
        // Emit new point
        TrailPoint newPoint{
            .position = currentPosition,
            .direction = (currentPosition - lastAddedPosition).Normalize(),
            .width = widthStart,
            .color = colorStart,
            .age = 0.0f,
            .distanceFromHead = 0.0f
        };
        points.AddPoint(newPoint);
        lastAddedPosition = currentPosition;
    }

    // Step 2: Age all points
    for (uint32_t i = 0; i < points.Count(); ++i) {
        points.GetPoint(i).age += dt;
    }

    // Step 3: Remove tail points past lifetime
    while (!points.IsEmpty() && points.GetPoint(points.Count() - 1).age > pointLifetime) {
        points.RemoveOldest();
    }

    // Step 4: Recalculate arc-length distances
    RecalculateDistances();
}
```

**Key behaviors**:
- First emission always happens (regardless of distance) so trails start immediately
- Emission threshold prevents degenerate zero-length segments
- Stationary emitters emit only one point (repeated calls use same position)
- Distance recalculation each frame adapts V texture coordinates to trail shape

### Distance Recalculation

`distanceFromHead` is the accumulated arc-length from the head point to each point along the trail.

```
Head (i=0):    distanceFromHead = 0.0
Point (i=1):   distanceFromHead = length(p[1] - p[0])
Point (i=2):   distanceFromHead = length(p[1] - p[0]) + length(p[2] - p[1])
...
Tail (i=N-1):  distanceFromHead = sum of all consecutive distances
```

This is recalculated every frame because:
- When points are added, the new head has distance = 0, and all others shift
- When points are removed from the tail, distances shift toward smaller values
- Trail curvature changes frame-to-frame as the emitter moves

Total trail length = `points.GetPoint(points.Count()-1).distanceFromHead` (the tail's distance).

## Stage 2: Geometry Generation (TrailMeshBuilder)

### Vertex & Index Layout

`TrailMeshBuilder::Build()` converts N trail points into a camera-facing ribbon:

- **Vertices**: N points × 2 vertices per point = 2N vertices (left + right edge)
- **Indices**: (N−1) segments × 6 indices per quad = 6(N−1) indices
- **Triangles**: 2(N−1) triangles

```
Head Point (p[0]):
    v[0] = left edge     (position + widthStart/2 * right vector)
    v[1] = right edge    (position - widthStart/2 * right vector)

Middle Point (p[1]):
    v[2] = left edge     (position + widthMid/2 * right vector)
    v[3] = right edge    (position - widthMid/2 * right vector)

    Quad indices: [0,1,2], [1,3,2]  ← two triangles, CCW winding

Tail Point (p[N-1]):
    v[2N-2] = left edge
    v[2N-1] = right edge
```

### Camera-Facing Geometry

For each trail point, a "right vector" is computed perpendicular to the trail's tangent direction, pointing toward the camera:

```cpp
// Tangent computation (forward, central, or backward difference)
Vec3 tangent;
if (i == 0) {
    // First point: forward difference
    tangent = (points[1].position - points[0].position).Normalize();
} else if (i == N - 1) {
    // Last point: backward difference
    tangent = (points[N-1].position - points[N-2].position).Normalize();
} else {
    // Interior: central difference (Catmull-Rom style)
    tangent = (points[i+1].position - points[i-1].position).Normalize();
}

// Right vector = cross(tangent, toCamera)
// This ensures the ribbon faces the camera
Vec3 toCamera = (cameraPosition - point.position).Normalize();
Vec3 right = cross(tangent, toCamera);

// Fallback for degenerate cases (tangent parallel to view direction)
if (right.Length() < 0.001f) {
    right = Vec3(1, 0, 0);  // Fallback to world X-axis
}
right = right.Normalize();

// Emit left and right vertices, separated by ribbon width
leftVertex.position = point.position + (point.width * 0.5f) * right;
rightVertex.position = point.position - (point.width * 0.5f) * right;
```

### Vertex Format (TrailVertex)

```cpp
struct TrailVertex {
    Vec3 position;   // 12 bytes: world-space ribbon edge
    float u;         // 4 bytes: normalized arc-length along trail [0, 1]
    float v;         // 4 bytes: normalized width position [-0.5, 0.5] → [0, 1]
    Vec4 color;      // 16 bytes: RGBA tint & lifetime alpha
};  // Total: 36 bytes per vertex
```

**UV Mapping**:
- **U** (trailing coordinate): `u = distanceFromHead / totalTrailLength` (0 at head, 1 at tail)
  - Used for scrolling texture along the trail length
  - Wraps for repeating patterns
- **V** (width coordinate): `v = 0.0` at left edge, `v = 1.0` at right edge
  - Static; not scrolled
  - Used to sample per-width texture details (feathering, soft edges)

**Color Interpolation**:
```cpp
float t = distanceFromHead / totalDistance;  // Normalized [0, 1]
Vec4 color = Lerp(colorStart, colorEnd, t);
```

## Stage 3: GPU Rendering

### Vertex Shader (trail_vs.hlsl)

Transforms world-space ribbon vertices to clip space and applies UV scrolling.

```hlsl
cbuffer cb_TrailVertexConstants : register(b0) {
    row_major float4x4 viewProjection;  // World → Clip matrix
    float time;                         // Elapsed seconds
    float uvScrollRate;                 // UV units per second
};

TrailVSOutput main(TrailVertex input) {
    // World → Clip transform
    output.positionCS = mul(float4(input.position, 1.0f), viewProjection);

    // Animate U coordinate
    output.uv = float2(
        input.uv.x + time * uvScrollRate,  // U: scroll along trail length
        input.uv.y                          // V: width (no scroll)
    );

    // Pass-through
    output.color = input.color;
    output.linearDepth = output.positionCS.w;  // For soft depth fade

    return output;
}
```

**Key features**:
- UV scroll = `time * uvScrollRate` — allows texture to flow along ribbon
- Linear depth = `positionCS.w` (view-space Z) for soft-particle comparison
- Color and position passed through unchanged

### Pixel Shader (trail_ps.hlsl)

Samples the trail texture, applies per-vertex color, and optionally fades against scene depth.

```hlsl
cbuffer cb_TrailPixelConstants : register(b1) {
    float softDepthRange;       // Fade radius in metres (0 = disabled)
    float alphaClipThreshold;   // Discard threshold [0, 1]
    float2 screenDimensions;    // Viewport (width, height)
    float cameraNear, cameraFar;
};

Texture2D<float4> trailTexture : register(t0);  // Trail RGBA
Texture2D<float> sceneDepth : register(t1);     // Reversed-Z depth buffer

float4 main(TrailVSOutput input) : SV_Target0 {
    // Step 1: Sample trail texture
    float4 texSample = trailTexture.Sample(linearSampler, input.uv);

    // Step 2: Combine with vertex color
    float4 finalColor = texSample * input.color;

    // Step 3: Soft-particle depth fade (optional)
    if (softDepthRange > 0.0f) {
        // Compare fragment depth to scene depth
        float depthDiff = sceneDepth.Load(...) - input.linearDepth;
        float softFade = saturate(depthDiff / softDepthRange);
        finalColor.a *= softFade;
    }

    // Step 4: Alpha test
    if (finalColor.a < alphaClipThreshold) {
        discard;
    }

    // Step 5: Premultiplied alpha
    finalColor.rgb *= finalColor.a;

    return finalColor;
}
```

**Key features**:
- Texture multiplication with vertex color tint (gradient)
- Soft depth fade prevents hard intersections with opaque geometry
- Alpha testing discards near-transparent fragments
- Premultiplied alpha for correct blending over layers

### Pipeline State Object (PSO)

Trail rendering requires specific GPU state:

| Setting | Value | Reason |
|---------|-------|--------|
| **Primitive Topology** | Triangle List | Index buffer describes pairs of triangles per segment |
| **Input Layout** | TrailVertex | Position, UV (2×float), Color |
| **Rasterizer: CullMode** | None | Ribbon may fold on itself; disable back-face culling |
| **Rasterizer: WindingOrder** | Counter-Clockwise | Triangle indices are CCW → left-handed front face |
| **Depth: DepthWrite** | Disabled | Trails composite on top; don't modify depth |
| **Depth: DepthFunc** | Greater-Equal | Reversed-Z convention (near=1, far=0) |
| **Blend: SrcBlend** | SrcAlpha or SrcOne | Std alpha or premultiplied (match shader output) |
| **Blend: DstBlend** | InvSrcAlpha | Composites correctly over opaque geometry |
| **Blend: BlendOp** | Add | SrcColor + DstColor × (1 − SrcAlpha) |

## Render Graph Integration

### RgBuilder Declaration

```cpp
// In AddTrailRenderPass():
rg::RgBuilder& builder;
TrailPassDesc desc;

builder.AddPass(
    "TrailRenderPass",
    [desc](rg::RgPassContext& ctx) {
        // Declare color target as writable
        ctx.Write(desc.colorTarget, RHIResourceState::RenderTarget);

        // Declare depth as readable (no write)
        ctx.Read(desc.depthTarget, RHIResourceState::DepthStencilRead);

        // GPU execute callback
        ctx.SetExecuteFn([desc](RHICommandBuffer& cb) {
            for (const auto& item : items) {
                // Bind pipeline, constant buffer, vertex/index buffers
                // Issue DrawIndexed(item.indexCount, 0, 0)
            }
        });
    }
);
```

### Sorting

Trails are sorted **back-to-front** by `(sortOrder, -distanceToCamera)`:

```cpp
// In AddTrailRenderPass():
std::sort(desc.items.begin(), desc.items.end(),
    [](const TrailPassRenderItem& a, const TrailPassRenderItem& b) {
        // Primary key: sort order (lower draws first)
        if (a.sortOrder != b.sortOrder) {
            return a.sortOrder < b.sortOrder;
        }
        // Secondary key: distance to camera (farther draws first)
        return a.distanceToCamera > b.distanceToCamera;
    });
```

This ensures trails blend correctly when overlapping: farther trails render first (backmost), closer trails render last (topmost).

### Render Phase Placement

The trail pass is scheduled in the **transparent phase**:

```
Frame Flow:
├─ Z-Prepass (opaque) → writes depth
├─ Opaque pass (GBuffer) → writes color + normals
├─ Lighting pass → reads GBuffer, writes HDR color
├─ Particle pass → transparent, blended into HDR
├─ Trail pass ← HERE (reads depth for soft-fade, writes HDR)
└─ Post-process (bloom, tonemap, UI)
```

## Soft Particle Depth Fade

When `softDepthRange > 0`, trails fade smoothly where they intersect opaque surfaces.

### Depth Linearization

```hlsl
// Reversed-Z depth buffer (0 = far, 1 = near)
float rawDepth = sceneDepth.Load(screenCoord);

// Linearize to view-space metres
float sceneLinearDepth = LinearizeDepthReversedZ(rawDepth, cameraNear, cameraFar);

// Fragment depth (interpolated from VS)
float fragmentDepth = input.linearDepth;  // Already linear, in metres

// Distance to surface (positive = in front)
float depthDiff = sceneLinearDepth - fragmentDepth;

// Fade factor: 0 at surface, 1 at `softDepthRange` metres ahead
float softFade = saturate(depthDiff / softDepthRange);
```

### Visual Result

Without soft fade: Hard edge where ribbon meets opaque geometry.
With soft fade: Ribbon transparency increases as it approaches the surface, eliminating the hard cutoff.

## Performance Notes

### Draw Calls

- **1 draw call per trail** with ≥2 active points
- Trails with <2 points generate no geometry and are skipped
- Typical scene: 5–20 active trails → 5–20 draw calls

### Vertex/Index Buffers

Trail mesh data is uploaded **per-frame** to dynamic GPU buffers:
- `TrailMeshData::vertices` → dynamic vertex buffer (SRV)
- `TrailMeshData::indices` → dynamic index buffer (SRV)

No persistent GPU buffers are needed; memory is temporary and reused across frames.

### Constant Buffers

- **Per-trail CB**: `TrailPassConstants` (64 bytes)
  - View-projection matrix only
  - Updated once per trail
  - Allocated from `CBRingAllocator` (double-buffered)

### Typical Performance (on RTX 2080)

| Metric | Typical | Max |
|--------|---------|-----|
| **Active trails** | 5 | 50 |
| **Total points** | 200 | 2000 |
| **Draw calls** | 5 | 50 |
| **Vertices per frame** | 400 | 4000 |
| **Time (GPU)** | 0.1 ms | 1.0 ms |

Soft depth fade adds ~0.05 ms per trail (one extra texture read + linearization).

## See Also

- [.uztrail Format](14-uztrail-format.md) — Trail asset specification
- [Trail Configuration Guide](16-trail-configuration-guide.md) — Practical tuning
- [VFX System Overview](01-overview.md) — VFX module architecture
- [Deferred Decal Pipeline](12-deferred-decal-pipeline.md) — Similar camera-facing geometry approach
