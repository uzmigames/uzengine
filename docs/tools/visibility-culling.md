# Visibility & GPU-Driven Culling System Spec

## Overview & Goals

Modern AAA scenes routinely contain tens of millions of polygons spread across thousands of mesh instances. The GPU can only shade visible pixels efficiently, so the renderer must aggressively discard geometry that cannot contribute to the final image. Without a structured visibility system, the GPU wastes cycles on occluded triangles, off-screen instances, and excessive LOD detail, leading to poor frame budgets and scalability problems.

UzEngine's visibility system provides **three selectable strategies** that can be configured per scene or per mesh type, allowing artists and engineers to balance compatibility, performance, and visual quality:

| Strategy | Description | Requirements | Best For |
|----------|-------------|--------------|----------|
| **Standard LOD + Frustum** | CPU frustum cull + discrete LOD selection | Any DX12 device | Broad compatibility, simple scenes |
| **GPU-Driven Occlusion** | HZB pyramid + compute shader culling + ExecuteIndirect | DX12 FL 12.0 | Dense urban scenes, interiors, forests |
| **Cluster (Meshlet) Rendering** | Amplification/Mesh shader pipeline, Nanite-inspired | DX12 SM 6.5 + Mesh Shaders | Hero assets, ultra-high-poly static meshes |

All three strategies share the same LOD system and can coexist in a single frame. For example, a scene might use cluster rendering for a hero statue, GPU-driven occlusion for the surrounding city, and standard frustum culling for distant foliage impostors.

### Target Users

| User | Interaction |
|------|-------------|
| **Graphics Engineer** | Configure culling strategy, tune HZB parameters, profile cull efficiency |
| **Technical Artist** | Mark meshes as occluders, enable virtual geometry per asset, set LOD thresholds |
| **Level Designer** | Place cull distance volumes, use debug visualization to verify performance |
| **AI Assistant** | Query visibility stats via IPC, auto-optimize scene culling parameters |

### Design Principles

1. **Zero CPU readback** -- GPU culling results stay on the GPU via ExecuteIndirect; no CPU stalls.
2. **Graceful fallback** -- If mesh shaders are unavailable, fall back to GPU-driven occlusion; if that fails, fall back to standard frustum culling.
3. **Per-mesh opt-in** -- Cluster rendering and virtual geometry require explicit asset opt-in; the system never silently changes pipeline behavior.
4. **AI-friendly formats** -- All configuration stored in plain-text `.uzmesh` and `.uzimpostor` files.

---

## Render Pass Integration

The visibility system inserts itself into the existing render graph between frustum culling and the GBuffer pass. The updated pass order:

```
Pass                      Priority    Notes
-------------------------------------------------------
ShadowPass                100         CSM directional
PointLightShadow          110         DPSM atlas
SpotLightShadow           120         Perspective atlas
DepthPrepass              200         Fill depth buffer
HZB Build (Phase 1)       210         NEW: downsample depth -> HZB mip chain
GPU Cull (Phase 1)        220         NEW: cull instances against HZB
GBuffer                   300         Draw surviving instances (Phase 1)
HZB Build (Phase 2)       310         NEW: rebuild HZB from updated depth
GPU Cull (Phase 2)        320         NEW: re-test Phase 1 rejects
GBuffer Late              330         NEW: draw late additions (Phase 2 survivors)
LightCull                 350         Tile-based light list
Lighting                  400         Deferred PBR
Transparency              500         Alpha-blended (sorted, no instancing)
Skybox                    600         Cubemap sky
PostProcess               700         SSAO -> SSR -> Bloom -> Tone -> TAA
UI                        800         Editor UI overlay
Overlay                   900         Debug visualizations
Readback                  1000        JPEG encode -> WebSocket
```

---

## Hierarchical Z-Buffer (HZB) Occlusion Culling

### Concept

The HZB is a mip chain of the depth buffer where each texel at mip level N stores the **maximum** (farthest) depth of the 2x2 texels at mip level N-1. By testing an object's projected screen-space AABB against the appropriate HZB mip level, a compute shader can conservatively determine whether the object is fully occluded in O(1) texture fetches per instance.

### HZB Pyramid Structure

```
Mip 0: Full resolution (1280 x 720)    <- copy from depth prepass
Mip 1: 640 x 360                        <- max of 2x2 from Mip 0
Mip 2: 320 x 180                        <- max of 2x2 from Mip 1
Mip 3: 160 x 90                         <- max of 2x2 from Mip 2
Mip 4: 80 x 45                          <- max of 2x2 from Mip 3
Mip 5: 40 x 23                          <- max of 2x2 from Mip 4
Mip 6: 20 x 12                          <- max of 2x2 from Mip 5
Mip 7: 10 x 6                           <- max of 2x2 from Mip 6
Mip 8: 5 x 3                            <- max of 2x2 from Mip 7
Mip 9: 3 x 2                            <- max of 2x2 from Mip 8
Mip 10: 2 x 1                           <- max of 2x2 from Mip 9
Mip 11: 1 x 1                           <- single farthest depth value

Format: R32_FLOAT (matches D32_FLOAT depth buffer)
UAV:    One per mip level for CS_BuildHZB to write
SRV:    Full mip chain for CS_CullInstances to sample
```

### HZB Build Compute Shader

```hlsl
// shaders/visibility/CS_BuildHZB.hlsl
//
// Downsample depth buffer into HZB mip chain.
// Dispatch once per mip level, from mip 1 to mip N.
// Each thread reads a 2x2 block from the previous mip and writes max depth.

#include "common.hlsli"

cbuffer HZBBuildParams : register(b2) {
    uint2  g_SrcSize;        // source mip dimensions
    uint2  g_DstSize;        // destination mip dimensions
    uint   g_SrcMipLevel;    // source mip to read from
    float3 _pad;
};

Texture2D<float>   g_SrcMip    : register(t0);   // previous mip (SRV)
RWTexture2D<float> g_DstMip    : register(u0);   // current mip (UAV)

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= g_DstSize))
        return;

    // Each output texel covers a 2x2 block in the source mip
    uint2 src_base = dtid.xy * 2;

    // Gather 2x2 depths from source mip, clamping to valid range
    float d00 = g_SrcMip.Load(int3(min(src_base + uint2(0, 0), g_SrcSize - 1), g_SrcMipLevel));
    float d10 = g_SrcMip.Load(int3(min(src_base + uint2(1, 0), g_SrcSize - 1), g_SrcMipLevel));
    float d01 = g_SrcMip.Load(int3(min(src_base + uint2(0, 1), g_SrcSize - 1), g_SrcMipLevel));
    float d11 = g_SrcMip.Load(int3(min(src_base + uint2(1, 1), g_SrcSize - 1), g_SrcMipLevel));

    // Max depth = farthest depth = most conservative occluder
    // With standard (non-reversed) Z: max is farthest
    // With reversed-Z: min is farthest -- see note below
    float max_depth = max(max(d00, d10), max(d01, d11));

    g_DstMip[dtid.xy] = max_depth;
}
```

**Reversed-Z note**: UzEngine uses standard Z (0=near, 1=far) in the depth buffer. If reversed-Z is adopted in the future, the HZB must store `min` instead of `max`, and the occlusion test comparison must be flipped. Both the `CS_BuildHZB` shader and `CS_CullInstances` shader have `#ifdef REVERSED_Z` guards for this case.

### Two-Phase Culling Pipeline

Two-phase culling eliminates the one-frame-lag artifact that occurs when objects first enter the view (since the HZB is built from the previous frame's depth). Phase 1 uses reprojected previous-frame HZB; Phase 2 uses the current frame's HZB built from Phase 1 survivors.

```
                      PHASE 1                              PHASE 2
                 (previous HZB)                        (current HZB)

+-------------------+     +-------------------+     +-------------------+
| All Instances     |     | Phase 1 Survivors |     | Phase 2 Survivors |
| (frustum-passed)  |     | (definitely       |     | (newly visible    |
|                   |     |  visible)         |     |  this frame)      |
+--------+----------+     +--------+----------+     +--------+----------+
         |                         |                         |
         v                         v                         v
+--------+----------+     +--------+----------+     +--------+----------+
| CS_CullInstances  |     | GBuffer Pass      |     | GBuffer Late Pass |
| test against      |     | draw survivors    |     | draw late adds    |
| prev-frame HZB    |     | build new depth   |     |                   |
+--------+----------+     +--------+----------+     +-------------------+
         |                         |
         +--> survivors ------>----+
         |                         |
         +--> rejects ------+      v
                            | +--------+----------+
                            | | CS_BuildHZB       |
                            | | rebuild from new  |
                            | | depth buffer      |
                            | +--------+----------+
                            |          |
                            v          v
                       +--------+----------+
                       | CS_CullInstances  |
                       | re-test rejects   |
                       | against new HZB   |
                       +--------+----------+
                                |
                                +--> survivors --> GBuffer Late Pass
                                +--> rejects  --> confirmed occluded (skip)
```

### GPU Instance Culling Compute Shader

```hlsl
// shaders/visibility/CS_CullInstances.hlsl
//
// Per-instance frustum + HZB occlusion culling.
// Input:  StructuredBuffer of all instances (position, AABB, LOD, mesh ID)
// Output: AppendStructuredBuffer of visible instance indices
//         IndirectArgs buffer for ExecuteIndirect

#include "common.hlsli"

// ----- Constants -----

static const uint THREAD_GROUP_SIZE = 256;

// ----- Buffers -----

cbuffer CullParams : register(b2) {
    float4x4 g_ViewProj;          // current frame view-projection
    float4x4 g_PrevViewProj;      // previous frame view-projection (Phase 1 reprojection)
    float4   g_FrustumPlanes[6];   // world-space frustum planes (Ax+By+Cz+D=0)
    float2   g_HZBSize;           // HZB mip 0 dimensions
    uint     g_InstanceCount;     // total number of instances to test
    uint     g_Phase;             // 0 = Phase 1 (prev HZB), 1 = Phase 2 (current HZB)
    float    g_LODBias;           // global LOD bias scalar
    float3   g_CameraPos;        // world-space camera position
    float    g_ScreenHeight;     // viewport height in pixels (for LOD calculation)
};

struct InstanceData {
    float4x4 world;          // world transform
    float3   aabb_min;       // object-space AABB min
    float    lod_bias;       // per-instance LOD bias
    float3   aabb_max;       // object-space AABB max
    uint     mesh_id;        // mesh handle index
    float4   lod_thresholds; // screen-size thresholds for LOD0-3
    uint     flags;          // bit 0: is_occluder, bit 1: is_meshlet, bit 2: is_virtual_geo
    uint3    _pad;
};

struct VisibleInstance {
    uint     instance_index;  // index into g_Instances
    uint     lod_level;       // selected LOD level
    float4x4 world;          // world transform (copied for GPU draw)
};

StructuredBuffer<InstanceData>         g_Instances       : register(t0);
Texture2D<float>                       g_HZB             : register(t1);
SamplerState                           g_PointClamp      : register(s0);

RWStructuredBuffer<VisibleInstance>     g_VisibleOut      : register(u0);
RWByteAddressBuffer                    g_IndirectArgs    : register(u1);
RWStructuredBuffer<uint>               g_RejectedOut     : register(u2); // Phase 1 rejects for Phase 2

// ----- Frustum Test -----

// Returns true if the world-space AABB is fully outside any frustum plane
bool frustum_cull(float3 ws_min, float3 ws_max) {
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        float4 plane = g_FrustumPlanes[i];
        // Find the AABB corner most aligned with plane normal (positive vertex)
        float3 p;
        p.x = (plane.x >= 0.0) ? ws_max.x : ws_min.x;
        p.y = (plane.y >= 0.0) ? ws_max.y : ws_min.y;
        p.z = (plane.z >= 0.0) ? ws_max.z : ws_min.z;
        // If positive vertex is behind plane, AABB is fully outside
        if (dot(plane.xyz, p) + plane.w < 0.0)
            return true; // culled
    }
    return false; // inside or intersecting
}

// ----- HZB Occlusion Test -----

// Project world-space AABB to screen-space rect and test against HZB
bool hzb_occluded(float3 ws_min, float3 ws_max, float4x4 vp) {
    // Project all 8 AABB corners to clip space
    float2 screen_min = float2(1e10, 1e10);
    float2 screen_max = float2(-1e10, -1e10);
    float  closest_z  = 1.0;

    [unroll]
    for (uint i = 0; i < 8; ++i) {
        float3 corner = float3(
            (i & 1) ? ws_max.x : ws_min.x,
            (i & 2) ? ws_max.y : ws_min.y,
            (i & 4) ? ws_max.z : ws_min.z
        );
        float4 clip = mul(vp, float4(corner, 1.0));

        // Skip if behind near plane (partially behind camera)
        if (clip.w <= 0.0)
            return false; // conservatively visible

        float3 ndc = clip.xyz / clip.w;
        float2 screen = ndc.xy * 0.5 + 0.5;
        screen.y = 1.0 - screen.y; // flip Y for texture UV

        screen_min = min(screen_min, screen);
        screen_max = max(screen_max, screen);
        closest_z  = min(closest_z, ndc.z);
    }

    // Clamp to [0,1] screen space
    screen_min = saturate(screen_min);
    screen_max = saturate(screen_max);

    // Compute projected size in pixels to select HZB mip level
    float2 size_pixels = (screen_max - screen_min) * g_HZBSize;
    float  max_dim     = max(size_pixels.x, size_pixels.y);

    // Select mip where the projected AABB covers ~2x2 texels (conservative)
    float mip_level = max(0.0, ceil(log2(max_dim * 0.5)));

    // Sample HZB at the 4 corners of the projected rect
    float hzb_depth = g_HZB.SampleLevel(g_PointClamp, (screen_min + screen_max) * 0.5, mip_level);

    // If object is farther than HZB depth at every sample, it is occluded
    // Standard Z: object_z > hzb_z means behind occluder
    return closest_z > hzb_depth;
}

// ----- LOD Selection -----

// Select LOD level based on screen-space projected size
uint select_lod(float3 ws_center, float sphere_radius, float4 thresholds, float bias) {
    float dist = distance(g_CameraPos, ws_center);
    // Projected screen height in pixels
    float screen_size = (sphere_radius * g_ScreenHeight) / max(dist, EPSILON);
    // Apply global and per-instance LOD bias
    screen_size *= (g_LODBias * bias);

    // Compare against thresholds: LOD0 if large, LOD3 if tiny
    if (screen_size >= thresholds.x) return 0;
    if (screen_size >= thresholds.y) return 1;
    if (screen_size >= thresholds.z) return 2;
    if (screen_size >= thresholds.w) return 3;
    return 4; // impostor
}

// ----- Main -----

[numthreads(THREAD_GROUP_SIZE, 1, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    uint idx = dtid.x;
    if (idx >= g_InstanceCount)
        return;

    InstanceData inst = g_Instances[idx];

    // Transform AABB to world space
    // Simplified: assumes uniform scale; for non-uniform, transform all 8 corners
    float3 ws_center = mul(inst.world, float4((inst.aabb_min + inst.aabb_max) * 0.5, 1.0)).xyz;
    float3 half_ext  = (inst.aabb_max - inst.aabb_min) * 0.5;
    float  radius    = length(half_ext);

    float3 ws_min = ws_center - radius;
    float3 ws_max = ws_center + radius;

    // Step 1: Frustum cull
    if (frustum_cull(ws_min, ws_max))
        return; // outside frustum, skip entirely

    // Step 2: HZB occlusion test
    float4x4 test_vp = (g_Phase == 0) ? g_PrevViewProj : g_ViewProj;
    if (hzb_occluded(ws_min, ws_max, test_vp)) {
        // Occluded in this phase
        if (g_Phase == 0) {
            // Phase 1: save to rejected list for Phase 2 re-test
            uint reject_idx;
            g_RejectedOut.IncrementCounter();
            // Use atomic append (counter is at offset 0 in UAV)
            g_IndirectArgs.InterlockedAdd(16, 1, reject_idx); // rejected count at byte 16
            g_RejectedOut[reject_idx] = idx;
        }
        return; // occluded
    }

    // Step 3: LOD selection
    uint lod = select_lod(ws_center, radius, inst.lod_thresholds,
                          inst.lod_bias * g_LODBias);

    // Step 4: Append to visible output
    uint visible_idx;
    g_IndirectArgs.InterlockedAdd(4, 1, visible_idx); // instance count at byte 4

    VisibleInstance vis;
    vis.instance_index = idx;
    vis.lod_level      = lod;
    vis.world          = inst.world;
    g_VisibleOut[visible_idx] = vis;
}
```

### ExecuteIndirect Pipeline

The GPU cull shader writes directly to an `IndirectArgs` buffer compatible with `D3D12_DRAW_INDEXED_ARGUMENTS`, enabling draw submission with zero CPU readback:

```
IndirectArgs Buffer Layout (per mesh type):
+--------+--------+--------+--------+--------+
| IndexCt| InstCt | IdxOff | VtxOff | InstOff|
| 4 bytes| 4 bytes| 4 bytes| 4 bytes| 4 bytes|
+--------+--------+--------+--------+--------+
  0        4        8        12       16

CPU pre-fills: IndexCount, IndexOffset, VertexOffset from mesh data
GPU writes:    InstanceCount (atomically incremented by CS_CullInstances)
```

```cpp
// C++ side: create indirect argument buffer and command signature
D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
arg_desc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

D3D12_COMMAND_SIGNATURE_DESC sig_desc = {};
sig_desc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
sig_desc.NumArgumentDescs = 1;
sig_desc.pArgumentDescs = &arg_desc;

device->CreateCommandSignature(&sig_desc, nullptr, IID_PPV_ARGS(&cmd_signature));

// At draw time:
cmd_list->ExecuteIndirect(
    cmd_signature,
    max_draw_count,          // upper bound on draw calls
    indirect_args_buffer,    // GPU-filled argument buffer
    0,                       // byte offset
    count_buffer,            // optional: GPU buffer holding actual draw count
    0                        // count buffer offset
);
```

### GPU Reprojection (Phase 1)

In Phase 1, the HZB from the previous frame must be reprojected to account for camera movement. This is achieved by using the previous frame's view-projection matrix (`g_PrevViewProj`) when projecting AABB corners during the occlusion test. The HZB texture itself is not reprojected (which would introduce holes); instead, the test coordinates are transformed into previous-frame screen space.

This approach is conservative: objects that were occluded last frame but are now visible due to camera motion will fail the Phase 1 test and be caught by Phase 2.

---

## Software Occlusion (CPU-Side)

### Purpose

For scenes with a clear hierarchy of large occluders (buildings, walls, terrain chunks), a lightweight CPU software rasterizer can reject obviously occluded objects before they are ever submitted to the GPU. This is useful for:

1. **Early rejection** -- Cull thousands of small objects behind large buildings without GPU overhead.
2. **Reducing GPU cull input** -- Fewer instances in the GPU cull buffer means fewer compute shader invocations.
3. **Complementary** -- Software occlusion runs on the CPU in parallel with GPU work from the previous frame.

### Implementation

```
Software Occlusion Pipeline:

+------------------+      +-----------------------+      +-------------------+
| Occluder Meshes  | ---> | CPU Software          | ---> | Occludee Test     |
| (simplified      |      | Rasterizer            |      | (per-instance     |
|  convex hulls,   |      | (256 x 144 depth      |      |  AABB projection  |
|  marked in       |      |  buffer, float32,     |      |  against SW depth |
|  import settings)|      |  single-threaded)     |      |  buffer)          |
+------------------+      +-----------------------+      +-------------------+
                                                                  |
                                                    +-------------+-------------+
                                                    |                           |
                                              Visible (pass               Occluded (skip
                                              to GPU cull)                GPU submission)
```

### Occluder Mesh Requirements

| Property | Value | Notes |
|----------|-------|-------|
| Max vertices per occluder | 64 | Simplified convex hull |
| Max triangles per occluder | 128 | Low-poly proxy |
| Max occluders per scene | 256 | Only large, solid objects |
| Rasterization resolution | 256 x 144 | 16:9, low-res sufficient for conservative test |
| Depth format | `float32` | CPU-side, no GPU format constraints |

### Occluder Generation

Occluder meshes can be:
1. **Auto-generated** at import time using convex hull decomposition (meshoptimizer `meshopt_simplify` to target vertex count).
2. **Manually authored** by the artist as a simplified proxy mesh and tagged in the `.uzmesh` import settings.

```ini
; In .uzmesh import settings
[occluder]
enabled = true
mode = auto           ; auto | manual
manual_mesh = ""      ; path to manual occluder mesh (if mode=manual)
max_vertices = 64     ; auto-generation target
max_triangles = 128   ; auto-generation target
```

---

## Cluster-Based Rendering (Meshlet System)

### Concept

Traditional indexed rendering submits entire meshes as a single draw call. For high-polygon meshes (500K+ triangles), this means the GPU processes every triangle even if large portions are back-facing or off-screen. Meshlet rendering splits the mesh into small clusters (meshlets) of 128 vertices / 128 primitives, each with its own bounding volume and back-face cone, enabling per-cluster culling on the GPU.

### Meshlet Structure

```
                    Meshlet (128 vertices, 128 primitives max)
                    +-----------------------------------------+
                    |                                         |
                    |  Bounding Sphere: center + radius       |
                    |  Normal Cone: apex, axis, cutoff angle  |
                    |  Vertex Offset + Count                  |
                    |  Primitive Offset + Count               |
                    |                                         |
                    +-----------------------------------------+

Full Mesh -> Array of Meshlets:
+----------+----------+----------+----------+----------+
| Meshlet  | Meshlet  | Meshlet  | Meshlet  | Meshlet  |  ...
|   #0     |   #1     |   #2     |   #3     |   #4     |
| 128v/96t | 128v/128t| 120v/110t| 128v/128t| 90v/80t  |
+----------+----------+----------+----------+----------+
```

### Meshlet Data Layout

```cpp
// src/renderer/meshlet/meshlet_data.h

struct MeshletDesc {
    float    bounding_center[3]; // object-space bounding sphere center
    float    bounding_radius;    // bounding sphere radius
    float    cone_apex[3];       // back-face cone apex (object space)
    float    cone_axis_cutoff;   // packed: axis XY in 16-bit, cutoff in 16-bit
    uint32_t vertex_offset;      // offset into meshlet vertex index buffer
    uint32_t vertex_count;       // number of unique vertices in this meshlet
    uint32_t prim_offset;        // offset into meshlet primitive index buffer
    uint32_t prim_count;         // number of primitives (triangles)
};
// sizeof(MeshletDesc) = 48 bytes

struct MeshletMeshData {
    uint32_t         meshlet_count;
    MeshletDesc*     meshlets;           // array of meshlet descriptors
    uint32_t*        meshlet_vertices;   // vertex index remap (meshlet-local -> mesh-global)
    uint8_t*         meshlet_triangles;  // packed triangle indices (3 bytes per tri, meshlet-local)
    // Vertex buffer is shared with the original mesh
};
```

### Mesh Preprocessing Pipeline

Meshlet generation occurs at import time using meshoptimizer:

```
Import Pipeline (UzBuild.exe or editor import):

glTF/FBX Mesh
    |
    v
[meshopt_buildMeshlets]  --> Array<MeshletDesc>
    |                        + meshlet_vertices[]
    |                        + meshlet_triangles[]
    v
[meshopt_computeMeshletBounds]  --> per-meshlet bounding sphere
    |
    v
[meshopt_computeMeshletNormalCone]  --> per-meshlet back-face cone
    |
    v
Store in .uzmesh binary section (MeshletMeshData)
```

### GPU Pipeline: Amplification + Mesh Shaders

```
Amplification Shader (AS)          Mesh Shader (MS)           Pixel Shader (PS)
+--------------------------+      +-------------------+      +------------------+
| One thread per meshlet   |      | One group per     |      | Standard PBR     |
|                          |      | surviving meshlet |      | (same as GBuffer |
| 1. Frustum test (sphere) |      |                   |      |  pass PSMain)    |
| 2. Backface cone test    | ---> | 1. Fetch vertices | ---> |                  |
| 3. HZB occlusion test    |      | 2. Transform      |      |                  |
|                          |      | 3. Output prims   |      |                  |
| Emit MS invocations for  |      |                   |      |                  |
| surviving meshlets only  |      +-------------------+      +------------------+
+--------------------------+
```

### Amplification Shader

```hlsl
// shaders/visibility/AS_CullMeshlets.hlsl
//
// Amplification shader: per-meshlet culling.
// Each thread group processes one meshlet and decides whether to emit
// a mesh shader invocation for it.

#include "common.hlsli"

#define AS_GROUP_SIZE 32  // process 32 meshlets per thread group

struct MeshletDesc {
    float3 bounding_center;
    float  bounding_radius;
    float3 cone_apex;
    float  cone_axis_cutoff;   // packed
    uint   vertex_offset;
    uint   vertex_count;
    uint   prim_offset;
    uint   prim_count;
};

struct MeshletPayload {
    uint meshlet_indices[AS_GROUP_SIZE];
};

cbuffer MeshletCullParams : register(b2) {
    float4x4 g_World;
    float4x4 g_ViewProj;
    float4   g_FrustumPlanes[6];
    float3   g_CameraPos;
    uint     g_MeshletCount;
    float2   g_HZBSize;
    float2   _pad;
};

StructuredBuffer<MeshletDesc> g_Meshlets : register(t0);
Texture2D<float>              g_HZB      : register(t1);
SamplerState                  g_Sampler  : register(s0);

groupshared MeshletPayload s_Payload;

// Unpack cone axis from packed float
float3 unpack_cone_axis(float packed) {
    uint bits = asuint(packed);
    float2 xy = float2(
        f16tof32(bits >> 16),
        f16tof32(bits & 0xFFFF)
    );
    float z = sqrt(saturate(1.0 - dot(xy, xy)));
    return float3(xy, z);
}

float unpack_cone_cutoff(float packed) {
    // cutoff stored in lower 16 bits as f16
    return f16tof32(asuint(packed) & 0xFFFF);
}

bool frustum_cull_sphere(float3 center, float radius) {
    [unroll]
    for (uint i = 0; i < 6; ++i) {
        if (dot(g_FrustumPlanes[i].xyz, center) + g_FrustumPlanes[i].w < -radius)
            return true; // fully outside
    }
    return false;
}

bool backface_cull_cone(float3 cone_apex, float3 cone_axis, float cone_cutoff,
                        float3 camera_pos) {
    float3 view_dir = normalize(cone_apex - camera_pos);
    // If the view direction is within the cone's half-angle from the axis,
    // all triangles in the meshlet are back-facing
    return dot(view_dir, cone_axis) > cone_cutoff;
}

[numthreads(AS_GROUP_SIZE, 1, 1)]
void ASMain(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID,
            uint gid : SV_GroupID) {
    bool visible = false;

    if (dtid < g_MeshletCount) {
        MeshletDesc m = g_Meshlets[dtid];

        // Transform bounding sphere to world space
        float3 ws_center = mul(g_World, float4(m.bounding_center, 1.0)).xyz;
        // Approximate world-space radius (assumes uniform scale)
        float ws_radius = m.bounding_radius * length(float3(
            g_World[0][0], g_World[1][1], g_World[2][2])) / 1.732;

        // Transform cone to world space
        float3 ws_apex = mul(g_World, float4(m.cone_apex, 1.0)).xyz;
        float3 cone_axis = unpack_cone_axis(m.cone_axis_cutoff);
        float3 ws_axis = normalize(mul((float3x3)g_World, cone_axis));
        float  cutoff = unpack_cone_cutoff(m.cone_axis_cutoff);

        // Test 1: Frustum culling
        if (!frustum_cull_sphere(ws_center, ws_radius)) {
            // Test 2: Back-face cone culling
            if (!backface_cull_cone(ws_apex, ws_axis, cutoff, g_CameraPos)) {
                visible = true;
            }
        }
    }

    // Compact surviving meshlets using wave intrinsics
    uint visible_count = WaveActiveCountBits(visible);
    uint visible_prefix = WavePrefixCountBits(visible);

    if (visible) {
        s_Payload.meshlet_indices[visible_prefix] = dtid;
    }

    // Emit mesh shader invocations for surviving meshlets
    DispatchMesh(visible_count, 1, 1, s_Payload);
}
```

### Mesh Shader

```hlsl
// shaders/visibility/MS_DrawMeshlets.hlsl
//
// Mesh shader: processes one meshlet, outputs transformed vertices and primitives.
// Receives payload from amplification shader with meshlet index.

#include "common.hlsli"
#include "brdf.hlsli"

#define MS_GROUP_SIZE 128   // max vertices per meshlet
#define MAX_PRIMS     128   // max primitives per meshlet

struct MeshletDesc {
    float3 bounding_center;
    float  bounding_radius;
    float3 cone_apex;
    float  cone_axis_cutoff;
    uint   vertex_offset;
    uint   vertex_count;
    uint   prim_offset;
    uint   prim_count;
};

struct MeshletPayload {
    uint meshlet_indices[32];
};

struct VertexOut {
    float4 position : SV_Position;
    float3 world    : WORLD_POS;
    float3 normal   : NORMAL;
    float2 uv       : TEXCOORD0;
};

cbuffer MeshletDrawParams : register(b0) {
    float4x4 g_World;
    float4x4 g_ViewProj;
};

cbuffer PerFrame : register(b1) {
    float3 g_CameraPos;
    float  _pad;
};

StructuredBuffer<MeshletDesc> g_Meshlets        : register(t0);
StructuredBuffer<float3>      g_Positions        : register(t1);
StructuredBuffer<float3>      g_Normals          : register(t2);
StructuredBuffer<float2>      g_UVs              : register(t3);
StructuredBuffer<uint>        g_MeshletVertices  : register(t4);
ByteAddressBuffer             g_MeshletTriangles : register(t5);

[outputtopology("triangle")]
[numthreads(MS_GROUP_SIZE, 1, 1)]
void MSMain(
    uint gtid : SV_GroupThreadID,
    uint gid  : SV_GroupID,
    in payload MeshletPayload payload,
    out vertices VertexOut verts[MS_GROUP_SIZE],
    out indices uint3 tris[MAX_PRIMS]
) {
    uint meshlet_idx = payload.meshlet_indices[gid];
    MeshletDesc m = g_Meshlets[meshlet_idx];

    SetMeshOutputCounts(m.vertex_count, m.prim_count);

    // Each thread processes one vertex
    if (gtid < m.vertex_count) {
        uint global_vertex = g_MeshletVertices[m.vertex_offset + gtid];

        float3 pos    = g_Positions[global_vertex];
        float3 normal = g_Normals[global_vertex];
        float2 uv     = g_UVs[global_vertex];

        float4 world_pos = mul(g_World, float4(pos, 1.0));

        VertexOut v;
        v.position = mul(g_ViewProj, world_pos);
        v.world    = world_pos.xyz;
        v.normal   = normalize(mul((float3x3)g_World, normal));
        v.uv       = uv;
        verts[gtid] = v;
    }

    // Each thread processes one primitive
    if (gtid < m.prim_count) {
        uint byte_offset = (m.prim_offset + gtid) * 3;
        uint packed = g_MeshletTriangles.Load(byte_offset & ~3);
        uint shift  = (byte_offset & 3) * 8;

        uint i0 = (packed >> shift) & 0xFF;
        uint i1 = 0;
        uint i2 = 0;

        // Handle cross-word boundary reads
        if (shift <= 16) {
            i1 = (packed >> (shift + 8)) & 0xFF;
            i2 = (packed >> (shift + 16)) & 0xFF;
        } else {
            uint packed2 = g_MeshletTriangles.Load((byte_offset & ~3) + 4);
            i1 = (shift == 24) ? (packed2 & 0xFF) : ((packed >> (shift + 8)) & 0xFF);
            i2 = (shift == 24) ? ((packed2 >> 8) & 0xFF) : (packed2 & 0xFF);
        }

        tris[gtid] = uint3(i0, i1, i2);
    }
}
```

### DX12 Feature Requirements

| Feature | Required Tier | Fallback |
|---------|--------------|----------|
| Mesh Shaders | `D3D12_MESH_SHADER_TIER_1` | Standard indexed draw |
| Shader Model | SM 6.5 (`-T as_6_5`, `-T ms_6_5`) | No meshlet rendering |
| Wave Intrinsics | `D3D12_SHADER_MODEL_6_0` | Scalar fallback in AS |

```cpp
// Feature detection at startup
D3D12_FEATURE_DATA_D3D12_OPTIONS7 options7 = {};
device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS7,
                            &options7, sizeof(options7));

bool mesh_shaders_supported =
    (options7.MeshShaderTier >= D3D12_MESH_SHADER_TIER_1);
```

### Candidate Meshes

Not all meshes benefit from meshlet rendering. Candidate criteria:

| Criterion | Requirement |
|-----------|-------------|
| Mesh type | Static only (no skinning) |
| Triangle count | > 10,000 (below this, standard draw is cheaper) |
| Transparency | Opaque only (no alpha blend) |
| Import flag | `meshlet_enabled = true` in `.uzmesh` |
| Runtime | Mesh shader tier available on GPU |

---

## Virtual Geometry (Simplified Nanite)

### Concept

Virtual geometry extends the meshlet system by organizing clusters into a hierarchical **Cluster DAG** (Directed Acyclic Graph) where each level represents a different level of detail. At runtime, the system traverses the DAG and selects the set of clusters whose screen-space error is below a threshold, producing a seamless, pixel-perfect LOD with no visible transitions or popping.

### Cluster DAG Structure

```
              Level 0 (Full Detail)
         /      /    |    \       \
        C0    C1    C2    C3     C4      <- individual meshlets (128 tri each)
         \   /       |      \   /
          v v        v       v v
          G0         G1      G2          <- Level 1 groups (simplified from children)
           \         |       /
            \        v      /
             +------ G3 ---+             <- Level 2 group (even more simplified)
                     |
                     v
                    ROOT                 <- Level 3 (single coarse cluster, ~128 tri)

DAG Properties:
- Leaf nodes (Level 0): original high-detail meshlets from meshoptimizer
- Interior nodes: simplified clusters generated by decimating children
- Each node stores: cluster mesh data + bounding sphere + screen-space error
- Parent error > child error (monotonically decreasing toward leaves)
- Runtime selects the CUT through the DAG where error < threshold
```

### Runtime Selection Algorithm

```
traverse_cluster_dag(node, camera, error_threshold):
    projected_error = node.max_error * screen_height / (2 * distance * tan(fov/2))

    if projected_error < error_threshold:
        RENDER this node (sufficient quality at this distance)
    else if node has children:
        for each child in node.children:
            traverse_cluster_dag(child, camera, error_threshold)
    else:
        RENDER this node (leaf, maximum detail)
```

The traversal is performed on the GPU using a compute shader that walks the DAG in parallel, outputting the selected cluster set to a draw buffer.

### Screen-Space Error Metric

```
projected_error_pixels = (cluster.max_geometric_error * viewport_height)
                         / (2.0 * distance_to_camera * tan(fov * 0.5))

Target: projected_error_pixels < 1.0  (sub-pixel error, no visible artifacts)
Configurable threshold: r.VirtualGeo.ErrorThreshold (default 1.0 pixels)
```

### Cluster Streaming

Virtual geometry meshes can be extremely large (millions of clusters). Only the clusters needed for the current camera position are loaded:

1. **Resident set** -- Clusters within the current LOD cut, plus one level of children for smooth transitions.
2. **Streaming priority** -- Based on projected screen-space size and distance to camera.
3. **Budget** -- Configurable cluster memory budget (default 512 MB VRAM).
4. **Fallback** -- If a cluster is not yet loaded, render its parent (coarser LOD, always resident).

### Limitations

| Limitation | Reason |
|-----------|--------|
| Static meshes only | DAG assumes fixed topology; skeletal animation invalidates cluster bounds |
| No transparency | Cluster DAG rendering writes to depth; alpha blend requires ordering |
| No vertex animation | Wind, vertex displacement break cluster bounding volumes |
| High VRAM | Cluster DAG is 2-3x the size of equivalent LOD chain (redundant data at each level) |
| Offline build required | DAG construction is O(minutes) for complex meshes; runs in `UzBuild.exe` |

### Import Settings

```ini
; In .uzmesh import settings
[virtual_geometry]
enabled = true
max_cluster_error = 0.001     ; max geometric error per cluster (object-space units)
target_cluster_tris = 128     ; triangles per leaf cluster
dag_levels = 5                ; maximum DAG depth
```

---

## LOD System

### LOD Generation

LODs are generated automatically at import time using meshoptimizer's `meshopt_simplify`:

```
LOD Generation Pipeline:

Original Mesh (LOD0: 100% triangles)
    |
    v
[meshopt_simplify, target_ratio=0.50] --> LOD1 (50% triangles)
    |
    v
[meshopt_simplify, target_ratio=0.25] --> LOD2 (25% triangles)
    |
    v
[meshopt_simplify, target_ratio=0.125] --> LOD3 (12.5% triangles)
    |
    v
[ImpostorBaker, 8-16 angles] ----------> LOD4 (billboard impostor)
```

### LOD Levels

| LOD | Triangle Ratio | Use Case | Shader |
|-----|---------------|----------|--------|
| LOD0 | 100% | Close-up hero detail | Full PBR |
| LOD1 | 50% | Medium distance | Full PBR, mip bias +1 |
| LOD2 | 25% | Far distance | Simplified PBR (no detail normal) |
| LOD3 | 12.5% | Very far | Vertex color, no textures |
| LOD4 | Impostor | Extreme distance | Billboard shader |

### LOD Selection

LOD is selected based on the projected screen-space size of the mesh's bounding sphere:

```
screen_pixels = (bounding_radius * viewport_height) / (distance * tan(fov * 0.5))

LOD Threshold Table (default, configurable per mesh):
+----------+---------------------------+
| LOD      | Min Screen Size (pixels)  |
+----------+---------------------------+
| LOD0     | >= 200                    |
| LOD1     | >= 100                    |
| LOD2     | >= 50                     |
| LOD3     | >= 20                     |
| LOD4     | < 20 (impostor)           |
+----------+---------------------------+
```

### LOD Bias

LOD bias allows global or per-material adjustment of LOD selection:

| Parameter | Range | Effect |
|-----------|-------|--------|
| `r.LOD.Bias` | 0.1 - 4.0 | Multiplier on screen size; 0.5 = use one LOD higher (lower quality), 2.0 = use one LOD lower (higher quality) |
| Per-material `lod_bias` | 0.1 - 4.0 | Stacks with global bias |
| Per-instance `lod_bias` | 0.1 - 4.0 | Stacks with material and global bias |

Effective bias: `effective_screen_size = raw_screen_size * r.LOD.Bias * material.lod_bias * instance.lod_bias`

### LOD Crossfade (Dither Transition)

To avoid visible popping when LOD levels switch, UzEngine uses a Bayer matrix dither pattern to blend between adjacent LOD levels over a configurable transition range:

```hlsl
// shaders/include/lod_crossfade.hlsli

#ifndef UZ_LOD_CROSSFADE_HLSLI
#define UZ_LOD_CROSSFADE_HLSLI

// 4x4 Bayer dither matrix (normalized to [0,1])
static const float BAYER_4X4[4][4] = {
    { 0.0/16.0,  8.0/16.0,  2.0/16.0, 10.0/16.0},
    {12.0/16.0,  4.0/16.0, 14.0/16.0,  6.0/16.0},
    { 3.0/16.0, 11.0/16.0,  1.0/16.0,  9.0/16.0},
    {15.0/16.0,  7.0/16.0, 13.0/16.0,  5.0/16.0}
};

// fade_factor: 0.0 = fully current LOD, 1.0 = fully next LOD
// screen_pos: SV_Position.xy
// Returns true if this pixel should be discarded (showing the other LOD through)
bool lod_dither_discard(float2 screen_pos, float fade_factor) {
    uint2 pixel = uint2(screen_pos) % 4;
    float threshold = BAYER_4X4[pixel.y][pixel.x];
    return fade_factor > threshold;
}

#endif // UZ_LOD_CROSSFADE_HLSLI
```

During a LOD transition, both the current and next LOD are rendered. The current LOD discards pixels where `fade_factor > threshold`, and the next LOD discards where `fade_factor <= threshold`, producing a seamless dithered blend.

### LOD Transition Range

```
                  LOD0                LOD1               LOD2
                  ====                ====               ====
Screen Size: 220  |  200  |  180  |  100  |  80  |  50
                  | trans |       | trans |      | trans |
                  | range |       | range |      | range |
                  |<-20px->|       |<-20px->|     |<-20px->|

trans_range = r.LOD.TransitionRange (default: 20 pixels)
fade_factor = 1.0 - saturate((screen_size - threshold) / trans_range)
```

---

## Impostor Baker Tool

### Overview

The Impostor Baker generates pre-rendered billboard sprites of 3D meshes from multiple viewing angles. These billboards serve as LOD4 -- the cheapest possible representation for objects at extreme distances.

### Editor Panel

```
+-----------------------------------------------------------------------+
|  IMPOSTOR BAKER                                                       |
+-----------------------------------------------------------------------+
|                                                                       |
|  Source Mesh: [res://meshes/oak_tree.uzmesh            ] [Browse]     |
|                                                                       |
|  Settings:                                                            |
|  +-------------------------------+  +-----------------------------+   |
|  | Resolution: [1024] x [1024]   |  |  Preview:                   |   |
|  | Angle Count: [16]   v         |  |  +-------+-------+-------+  |   |
|  | Capture FOV: [60]   degrees   |  |  | ang 0 | ang 1 | ang 2 |  |   |
|  | Include Top View: [x]         |  |  +-------+-------+-------+  |   |
|  | Alpha Premultiply: [x]        |  |  | ang 3 | ang 4 | ang 5 |  |   |
|  | Normal Map: [x]               |  |  +-------+-------+-------+  |   |
|  | Depth Map: [x]                |  |  | ...   | ...   | ...   |  |   |
|  | Padding: [2] px               |  |  +-------+-------+-------+  |   |
|  +-------------------------------+  +-----------------------------+   |
|                                                                       |
|  Output:                                                              |
|  Atlas Texture: res://textures/impostor/oak_tree_impostor.png         |
|  Config File:   res://meshes/oak_tree.uzimpostor                      |
|                                                                       |
|  [Bake Impostor]  [Preview vs Original]  [Export]                     |
+-----------------------------------------------------------------------+
```

### Impostor Atlas Layout

```
Angle layout (16 angles, 4x4 grid):

+------+------+------+------+
|  0   | 22.5 |  45  | 67.5 |   <- yaw angles (degrees)
| deg  | deg  | deg  | deg  |
+------+------+------+------+
|  90  |112.5 | 135  |157.5 |
| deg  | deg  | deg  | deg  |
+------+------+------+------+
| 180  |202.5 | 225  |247.5 |
| deg  | deg  | deg  | deg  |
+------+------+------+------+
| 270  |292.5 | 315  |337.5 |
| deg  | deg  | deg  | deg  |
+------+------+------+------+

Each cell contains: Albedo (RGB) + Alpha (A)
Separate atlas for: Normal (RG oct) + Depth (B) + Unused (A)

Total atlas size (16 angles, 1024 per angle):
  4096 x 4096 albedo atlas
  4096 x 4096 normal+depth atlas
```

### .uzimpostor Format

```ini
; res://meshes/oak_tree.uzimpostor

[uzimpostor version=1]
source_mesh = "res://meshes/oak_tree.uzmesh"
angle_count = 16
resolution = 1024
capture_fov = 60.0
include_top = true
padding = 2

[textures]
albedo_atlas = "res://textures/impostor/oak_tree_albedo.dds"
normal_depth_atlas = "res://textures/impostor/oak_tree_normal_depth.dds"

[bounds]
; Bounding box of the original mesh (used for billboard sizing)
width = 4.2
height = 8.5
depth = 3.8
pivot_y = 0.0    ; ground plane offset
```

### Impostor Rendering Shader

```hlsl
// shaders/builtin/impostor.hlsl
//
// Billboard impostor shader. Selects angle from atlas based on view direction.

#include "common.hlsli"
#include "global_cbuffer.hlsli"

cbuffer ImpostorParams : register(b0) {
    float4x4 g_World;
    float4x4 g_ViewProj;
    float3   g_CameraPos;
    uint     g_AngleCount;     // 8, 16, or 32
    float2   g_BillboardSize;  // width, height in world units
    float    g_PivotY;         // Y offset for ground alignment
    float    _pad;
};

Texture2D    g_AlbedoAtlas      : register(t0);
Texture2D    g_NormalDepthAtlas : register(t1);
SamplerState g_LinearClamp      : register(s0);

struct VSIn {
    float3 pos : POSITION;     // quad corners: (-0.5,-0.5) to (0.5,0.5)
    float2 uv  : TEXCOORD0;   // 0-1 UV across billboard quad
};

struct VSOut {
    float4 pos    : SV_Position;
    float2 uv     : TEXCOORD0;
    float2 atlas  : TEXCOORD1; // UV offset + scale into atlas
};

VSOut VSMain(VSIn v) {
    VSOut o;

    // Billboard center in world space
    float3 center = mul(g_World, float4(0, g_PivotY, 0, 1)).xyz;

    // View direction on XZ plane (yaw only)
    float3 to_cam = g_CameraPos - center;
    float yaw = atan2(to_cam.x, to_cam.z); // -PI to PI
    if (yaw < 0.0) yaw += TWO_PI;

    // Select nearest atlas angle
    float angle_step = TWO_PI / float(g_AngleCount);
    uint angle_idx = uint(round(yaw / angle_step)) % g_AngleCount;

    // Compute atlas UV offset (grid layout: sqrt(angle_count) x sqrt(angle_count))
    uint grid_size = uint(ceil(sqrt(float(g_AngleCount))));
    uint grid_x = angle_idx % grid_size;
    uint grid_y = angle_idx / grid_size;
    float cell_size = 1.0 / float(grid_size);

    o.atlas = float2(grid_x, grid_y) * cell_size;

    // Billboard: camera-facing quad
    float3 right = normalize(cross(float3(0, 1, 0), normalize(to_cam)));
    float3 up    = float3(0, 1, 0);

    float3 world_pos = center
        + right * v.pos.x * g_BillboardSize.x
        + up    * (v.pos.y + 0.5) * g_BillboardSize.y; // +0.5 to pivot at bottom

    o.pos = mul(g_ViewProj, float4(world_pos, 1.0));
    o.uv  = v.uv * cell_size + o.atlas;

    return o;
}

float4 PSMain(VSOut i) : SV_TARGET {
    float4 albedo = g_AlbedoAtlas.Sample(g_LinearClamp, i.uv);

    // Alpha test: discard transparent pixels
    clip(albedo.a - 0.5);

    return float4(albedo.rgb, 1.0);
}
```

---

## Culling Debug Visualization

### Debug Modes

All debug visualizations are rendered in the Overlay pass (priority 900) and controlled via IPC or editor panel toggles.

| Mode | Description | Keybind |
|------|-------------|---------|
| Show Occluders | Highlight occluder meshes with green wireframe overlay | `F5` |
| Show HZB | Fullscreen quad displaying HZB mip chain (scrollable mip) | `F6` |
| Show Culled | Render AABB wireframes of culled instances in red | `F7` |
| Show LOD Colors | Color instances by active LOD level | `F8` |
| Show Meshlets | Color each meshlet with a unique color | `F9` |
| Show Cluster Error | Heatmap of virtual geometry screen-space error | `F10` |
| Show Overdraw | Accumulated pixel write count heatmap | `F11` |

### LOD Color Scheme

```
LOD0 = Green   (#00FF00)  -- Full detail, close range
LOD1 = Yellow  (#FFFF00)  -- Medium distance
LOD2 = Orange  (#FF8800)  -- Far distance
LOD3 = Red     (#FF0000)  -- Very far, minimal geometry
LOD4 = Magenta (#FF00FF)  -- Impostor billboard

Visualization applies a flat color multiply to the GBuffer albedo output,
preserving lighting for spatial context while clearly showing LOD boundaries.
```

### Debug Visualization Mockup

```
+-----------------------------------------------------------------------+
|  VIEWPORT (LOD Color Mode Active)                                     |
|                                                                       |
|   [green cube]  [green cube]  [yellow tree]    [orange house]         |
|   [green cube]     [green sphere]   [yellow pillar]                   |
|                                                                       |
|       [red tree]  [red tree]  [magenta dot] [magenta dot]             |
|   [orange wall]  [red rock]                     [magenta dot]         |
|                                                                       |
+-----------------------------------------------------------------------+
|  STATS OVERLAY (bottom-left)                                          |
|  +-----------------------------------------------+                   |
|  | Visible: 1,247 / 8,430 (14.8%)               |                   |
|  | Culled:  7,183 (85.2%) [Frustum: 4,200  HZB: 2,983]              |
|  | Draw Calls: 89 (instanced: 34, meshlet: 12, single: 43)          |
|  | Triangles: 2.4M visible / 18.7M total                            |
|  | Meshlets: 1,890 / 5,430 rendered                                  |
|  | LOD: L0=120 L1=340 L2=450 L3=280 L4=57                           |
|  | HZB Build: 0.12ms  GPU Cull: 0.34ms  SW Cull: 0.08ms             |
|  +-----------------------------------------------+                   |
+-----------------------------------------------------------------------+
```

### HZB Visualization Shader

```hlsl
// shaders/debug/CS_VisualizeHZB.hlsl
//
// Renders a selected HZB mip level as a fullscreen quad for debugging.

#include "common.hlsli"

cbuffer HZBVizParams : register(b2) {
    uint   g_MipLevel;       // which mip to display
    uint   g_DisplayMode;    // 0=grayscale, 1=heatmap
    float2 g_OutputSize;     // render target dimensions
};

Texture2D<float>   g_HZB       : register(t0);
RWTexture2D<float4> g_Output   : register(u0);

float3 depth_heatmap(float depth) {
    // Blue (near) -> Green -> Yellow -> Red (far)
    float t = saturate(depth);
    if (t < 0.33) return lerp(float3(0,0,1), float3(0,1,0), t / 0.33);
    if (t < 0.66) return lerp(float3(0,1,0), float3(1,1,0), (t - 0.33) / 0.33);
    return lerp(float3(1,1,0), float3(1,0,0), (t - 0.66) / 0.34);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dtid : SV_DispatchThreadID) {
    if (any(dtid.xy >= uint2(g_OutputSize)))
        return;

    float2 uv = (float2(dtid.xy) + 0.5) / g_OutputSize;
    float depth = g_HZB.SampleLevel(g_PointClamp, uv, g_MipLevel);

    float3 color;
    if (g_DisplayMode == 0) {
        color = depth.xxx; // grayscale
    } else {
        color = depth_heatmap(depth); // heatmap
    }

    g_Output[dtid.xy] = float4(color, 0.8); // semi-transparent overlay
}
```

---

## LOD & Culling Settings Panel (Editor)

### Panel Layout

```
+-----------------------------------------------------------------------+
|  VISIBILITY & CULLING SETTINGS                                        |
+-----------------------------------------------------------------------+
|                                                                       |
|  [Global]                                                             |
|  +-----------------------------------------------------------------+  |
|  | Culling Strategy: [GPU-Driven Occlusion   v]                    |  |
|  | LOD Bias:         [========1.0========]  (0.1 - 4.0)           |  |
|  | LOD Transition:   [========20=========]  pixels                 |  |
|  | HZB Culling:      [x] enabled                                  |  |
|  | SW Occlusion:     [x] enabled                                  |  |
|  | Meshlet Rendering:[x] enabled (requires SM 6.5)                |  |
|  | Virtual Geometry: [ ] enabled (per mesh, requires SM 6.5)      |  |
|  | Error Threshold:  [========1.0========]  pixels (virtual geo)   |  |
|  | Cluster VRAM:     [========512========]  MB budget              |  |
|  +-----------------------------------------------------------------+  |
|                                                                       |
|  [Per-Type Cull Distances]                                            |
|  +-----------------------------------------------------------------+  |
|  | Type              | Max Distance | LOD Bias | Occluder          |  |
|  |-------------------|-------------|----------|-------------------|  |
|  | Trees             | 500m        | 0.8      | [ ]               |  |
|  | Buildings         | 2000m       | 1.0      | [x]               |  |
|  | Props (small)     | 100m        | 0.5      | [ ]               |  |
|  | Characters        | 300m        | 1.2      | [ ]               |  |
|  | Terrain Chunks    | Infinite    | 1.0      | [x]               |  |
|  +-----------------------------------------------------------------+  |
|                                                                       |
|  [Cull Distance Volumes]                                              |
|  +-----------------------------------------------------------------+  |
|  | Volume Name       | Bounds          | Override Distance         |  |
|  |-------------------|-----------------|---------------------------|  |
|  | Interior_Room_A   | (10,0,10)-(20,5,20) | 50m (force close cull)|  |
|  | Background_Hills  | (0,0,500)-(1000,100,1000) | 200m            |  |
|  +-----------------------------------------------------------------+  |
|  [+ Add Volume]                                                       |
|                                                                       |
+-----------------------------------------------------------------------+
```

### Cull Distance Volumes

Cull distance volumes are axis-aligned box regions placed in the scene that override the max draw distance for objects within them. Use cases:

- **Interior rooms**: Force aggressive culling of outdoor objects when inside a building.
- **Background scenery**: Reduce draw distance for distant hills that are only visible from certain viewpoints.
- **Performance zones**: Place around performance-critical areas to cap visible instance count.

```ini
; In .uzscene node definition
[node CullVolume_Interior]
type = CullDistanceVolume
transform = 15.0, 2.5, 15.0        ; center position
bounds_min = -5.0, -2.5, -5.0      ; local AABB min
bounds_max = 5.0, 2.5, 5.0         ; local AABB max
override_distance = 50.0            ; meters
priority = 10                       ; higher priority overrides lower
```

---

## Performance Analysis Tools

### Instance Cost Viewer

Estimates per-instance GPU cost based on triangle count and material complexity:

```
Cost Formula:
  estimated_us = (triangle_count / 1000000.0) * base_cost_per_Mtri
               + texture_samples * cost_per_sample
               + (has_normal_map ? 1.2 : 1.0)   // normal map cost multiplier
               + (is_alpha_test ? 1.5 : 1.0)    // alpha test disables early-Z

base_cost_per_Mtri = 33.0 us  (calibrated for mid-range GPU)
cost_per_sample    = 0.5 us

Color coding in viewport:
  Green  : < 10 us estimated
  Yellow : 10 - 50 us
  Orange : 50 - 200 us
  Red    : > 200 us
```

### Draw Call Budget Panel

```
+-----------------------------------------------------------------------+
|  DRAW CALL BUDGET                                                     |
+-----------------------------------------------------------------------+
|                                                                       |
|  Shadow Pass:     |||||||||||||||||||..............  34 / 200          |
|  Depth Prepass:   ||||||||||..........................  18 / 200       |
|  GBuffer:         ||||||||||||||||||||||||||..........  52 / 200       |
|  GBuffer Late:    ||||...............................   8 / 200        |
|  Transparency:    |||||||||..........................  15 / 100        |
|  Meshlet Draws:   ||||||||||||.......................  12 (AS dispatches)|
|                                                                       |
|  TOTAL: 139 draw calls  |  Budget: 500  |  Headroom: 72%             |
|                                                                       |
+-----------------------------------------------------------------------+
```

### Triangle Budget Panel

```
+-----------------------------------------------------------------------+
|  TRIANGLE BUDGET                                                      |
+-----------------------------------------------------------------------+
|                                                                       |
|  Visible:  2,412,000 triangles                                        |
|  Budget:   4,000,000 triangles  ||||||||||||||||||||...  60.3%        |
|                                                                       |
|  By LOD:                                                              |
|    LOD0:  980,000  ||||||||||||||||||||||||||                          |
|    LOD1:  720,000  |||||||||||||||||||                                 |
|    LOD2:  450,000  ||||||||||||                                       |
|    LOD3:  210,000  ||||||                                             |
|    LOD4:   52,000  ||                                                 |
|                                                                       |
|  Culled: 16,288,000 triangles (87.1% efficiency)                      |
|                                                                       |
+-----------------------------------------------------------------------+
```

### Cull Efficiency Metrics

| Metric | Formula | Target |
|--------|---------|--------|
| Frustum cull rate | frustum_culled / total_instances | > 50% |
| HZB cull rate | hzb_culled / frustum_survived | > 30% |
| Total cull rate | (frustum_culled + hzb_culled) / total_instances | > 70% |
| Meshlet cull rate | meshlets_culled / total_meshlets | > 40% |
| Phase 2 recovery | phase2_survivors / phase1_rejects | < 5% (low = good, few false rejections) |

---

## File Formats

### .uzmesh Import Settings (Visibility Section)

```ini
; Appended to existing .uzmesh import settings

[lod]
auto_generate = true
lod1_ratio = 0.50          ; triangle ratio relative to LOD0
lod2_ratio = 0.25
lod3_ratio = 0.125
lod0_threshold = 200       ; screen pixels for LOD0
lod1_threshold = 100
lod2_threshold = 50
lod3_threshold = 20
crossfade_range = 20       ; transition range in pixels
impostor_enabled = true    ; generate LOD4 impostor

[meshlet]
enabled = false            ; generate meshlet data
max_vertices = 128         ; vertices per meshlet
max_primitives = 128       ; primitives per meshlet

[virtual_geometry]
enabled = false            ; build cluster DAG
max_cluster_error = 0.001  ; max geometric error (object space)
target_cluster_tris = 128
dag_levels = 5

[occluder]
enabled = false
mode = auto                ; auto | manual
manual_mesh = ""
max_vertices = 64
max_triangles = 128
```

### .uzimpostor Format

See the Impostor Baker Tool section above for the full format specification.

### Meshlet Binary Data (in .uzmesh)

Meshlet data is stored in a binary section appended to the `.uzmesh` file:

```
.uzmesh file layout:
+-----------------------+
| INI text header       |  <- import settings, material refs, LOD config
| (variable length)     |
+-----------------------+
| Binary marker:        |  <- "==MESHLET_DATA==" (16 bytes)
| 0x3D3D4D45...         |
+-----------------------+
| MeshletHeader         |  <- 16 bytes: meshlet_count, vertex_count, prim_count, flags
+-----------------------+
| MeshletDesc[]         |  <- meshlet_count * 48 bytes
+-----------------------+
| meshlet_vertices[]    |  <- uint32 array (meshlet-local to mesh-global vertex index)
+-----------------------+
| meshlet_triangles[]   |  <- uint8 array (packed 3 bytes per triangle, meshlet-local)
+-----------------------+
| Cluster DAG (optional)|  <- if virtual_geometry.enabled
|   DAGHeader           |  <- node_count, level_count, root_index
|   DAGNode[]           |  <- per-node: children, error, bounding sphere, cluster range
+-----------------------+
```

---

## C++ Class Hierarchy

```
src/renderer/visibility/
    |
    +-- VisibilitySystem              Main coordinator, owns all subsystems
    |     |
    |     +-- update(FrameContext&)    Called once per frame
    |     +-- get_visible_instances()  Returns GPU buffer of visible instances
    |     +-- get_stats()             Returns VisibilityStats struct
    |
    +-- HZBBuilder                    Builds HZB mip chain from depth buffer
    |     |
    |     +-- build(DepthBuffer&)     Dispatch CS_BuildHZB per mip level
    |     +-- get_hzb_srv()           SRV for the full HZB mip chain
    |     +-- get_hzb_texture()       Raw texture resource
    |
    +-- GPUCuller                     GPU-driven instance culling
    |     |
    |     +-- cull_phase1(HZB, instances)   Cull against prev-frame HZB
    |     +-- cull_phase2(HZB, rejects)     Re-test Phase 1 rejects
    |     +-- get_visible_buffer()          UAV with surviving instances
    |     +-- get_indirect_args()           IndirectArgs for ExecuteIndirect
    |
    +-- SoftwareOccluder              CPU-side software rasterizer
    |     |
    |     +-- rasterize(occluders[])  Rasterize occluder meshes to SW depth
    |     +-- test(aabb)              Test AABB against SW depth buffer
    |     +-- get_depth_image()       256x144 float32 depth (for debug viz)
    |
    +-- MeshletRenderer               Meshlet pipeline (AS + MS + PS)
    |     |
    |     +-- prepare(mesh)           Upload meshlet data to GPU
    |     +-- render(cmd_list, mesh)  Dispatch DispatchMesh
    |     +-- is_supported()          Check D3D12_MESH_SHADER_TIER_1
    |
    +-- VirtualGeometrySystem         Cluster DAG traversal + streaming
    |     |
    |     +-- load_dag(mesh)          Load cluster DAG from .uzmesh
    |     +-- update(camera)          Traverse DAG, select LOD cut
    |     +-- stream_clusters()       Async load needed clusters
    |     +-- get_render_clusters()   Current frame's cluster set
    |
    +-- LODSystem                     LOD selection + crossfade management
    |     |
    |     +-- select_lod(instance)    Compute LOD level from screen size
    |     +-- get_crossfade_factor()  Dither blend factor for transitions
    |     +-- set_global_bias(float)  Set r.LOD.Bias
    |
    +-- ImpostorBaker                 Offline impostor generation tool
          |
          +-- bake(mesh, settings)    Render mesh from N angles -> atlas
          +-- export(path)            Write .uzimpostor + atlas textures

Class Dependency Diagram:

VisibilitySystem
    |
    +-----> HZBBuilder ---------> CS_BuildHZB.hlsl
    |           |
    +-----> GPUCuller ----------> CS_CullInstances.hlsl
    |           |                     |
    |           +---> (reads HZB) ----+
    |
    +-----> SoftwareOccluder     (CPU only, no shaders)
    |
    +-----> MeshletRenderer -----> AS_CullMeshlets.hlsl
    |           |                  MS_DrawMeshlets.hlsl
    |           |
    |           +---> (optional HZB test in AS)
    |
    +-----> VirtualGeometrySystem -> CS_TraverseDAG.hlsl
    |           |
    |           +---> MeshletRenderer (renders selected clusters)
    |
    +-----> LODSystem            (CPU LOD selection + GPU crossfade)
    |
    +-----> ImpostorBaker        (offline tool, not in render loop)
```

---

## IPC Handlers

```json
visibility.setStrategy    { "strategy": "gpu_driven" }
                      --> { "ok": true }
                          ; Values: "standard", "gpu_driven", "meshlet"

visibility.setLODBias     { "bias": 1.0 }
                      --> { "ok": true }

visibility.setHZBEnabled  { "enabled": true }
                      --> { "ok": true }

visibility.stats          {}
                      --> {
                            "total_instances": 8430,
                            "visible_instances": 1247,
                            "frustum_culled": 4200,
                            "hzb_culled": 2983,
                            "cull_efficiency_pct": 85.2,
                            "draw_calls": 89,
                            "visible_triangles": 2412000,
                            "total_triangles": 18700000,
                            "meshlets_rendered": 1890,
                            "meshlets_total": 5430,
                            "hzb_build_ms": 0.12,
                            "gpu_cull_ms": 0.34,
                            "sw_cull_ms": 0.08,
                            "lod_distribution": {
                              "lod0": 120, "lod1": 340, "lod2": 450,
                              "lod3": 280, "lod4": 57
                            }
                          }

visibility.debugMode      { "mode": "lod_colors", "enabled": true }
                      --> { "ok": true }
                          ; Modes: "occluders", "hzb", "culled", "lod_colors",
                          ;        "meshlets", "cluster_error", "overdraw"

visibility.hzbMipLevel    { "level": 3 }
                      --> { "ok": true }
                          ; For HZB debug visualization, select which mip to display

visibility.setCullDistance { "mesh_type": "Trees", "distance": 500.0 }
                      --> { "ok": true }

visibility.addCullVolume  { "name": "Interior_A",
                            "min": [-5, -2.5, -5],
                            "max": [5, 2.5, 5],
                            "position": [15, 2.5, 15],
                            "override_distance": 50.0 }
                      --> { "ok": true, "volume_id": "cull_vol_003" }

impostor.bake             { "mesh": "res://meshes/oak_tree.uzmesh",
                            "resolution": 1024,
                            "angles": 16 }
                      --> { "ok": true,
                            "impostor_path": "res://meshes/oak_tree.uzimpostor",
                            "atlas_path": "res://textures/impostor/oak_tree_albedo.dds",
                            "elapsed_ms": 2400 }
```

---

## Configuration (project.uzproj)

```toml
[renderer.visibility]
strategy = "gpu_driven"          # "standard", "gpu_driven", "meshlet"
hzb_enabled = true               # enable HZB occlusion culling
sw_occlusion_enabled = true      # enable CPU software occlusion
meshlet_enabled = true           # enable meshlet rendering (if GPU supports)
virtual_geometry_enabled = false  # enable virtual geometry (per-mesh opt-in)

[renderer.visibility.hzb]
resolution_divisor = 1           # HZB resolution = depth_res / divisor (1=full, 2=half)
phase2_enabled = true            # enable two-phase culling

[renderer.visibility.lod]
global_bias = 1.0                # LOD bias multiplier (0.1 - 4.0)
transition_range = 20            # crossfade transition range in pixels
impostor_enabled = true          # enable LOD4 impostor generation

[renderer.visibility.meshlet]
max_vertices = 128               # vertices per meshlet
max_primitives = 128             # primitives per meshlet

[renderer.visibility.virtual_geometry]
error_threshold = 1.0            # screen-space error threshold in pixels
cluster_vram_budget_mb = 512     # VRAM budget for resident clusters
streaming_priority_distance = 100.0  # distance beyond which streaming priority drops

[renderer.visibility.software_occlusion]
resolution_x = 256               # SW rasterizer width
resolution_y = 144               # SW rasterizer height
max_occluders = 256              # max occluder meshes per frame

[renderer.visibility.debug]
stats_overlay = false            # show stats overlay
default_mode = "none"            # default debug visualization mode
```

---

## Implementation Notes

### Shader Files

| Shader | Stage | Target | Purpose |
|--------|-------|--------|---------|
| `shaders/visibility/CS_BuildHZB.hlsl` | Compute | `cs_6_0` | Build HZB mip chain |
| `shaders/visibility/CS_CullInstances.hlsl` | Compute | `cs_6_0` | Per-instance frustum + HZB culling |
| `shaders/visibility/CS_CullMeshlets.hlsl` | Compute | `cs_6_0` | Compute-based meshlet culling (fallback) |
| `shaders/visibility/AS_CullMeshlets.hlsl` | Amplification | `as_6_5` | Meshlet culling via amplification shader |
| `shaders/visibility/MS_DrawMeshlets.hlsl` | Mesh | `ms_6_5` | Meshlet rendering via mesh shader |
| `shaders/visibility/CS_TraverseDAG.hlsl` | Compute | `cs_6_0` | Virtual geometry DAG traversal |
| `shaders/include/lod_crossfade.hlsli` | Include | -- | LOD dither crossfade utilities |
| `shaders/builtin/impostor.hlsl` | VS+PS | `vs_6_0`/`ps_6_0` | Billboard impostor rendering |
| `shaders/debug/CS_VisualizeHZB.hlsl` | Compute | `cs_6_0` | HZB debug visualization |

### New Shader Variants

| Define | Added When | Purpose |
|--------|-----------|---------|
| `MESHLET_RENDERING` | Meshlet pipeline active | Switch vertex fetch to meshlet buffers |
| `LOD_CROSSFADE` | During LOD transition | Enable dither discard in pixel shader |
| `IMPOSTOR` | Rendering LOD4 billboard | Use impostor atlas sampling path |
| `REVERSED_Z` | Engine configured for reversed-Z | Flip HZB comparison direction |
| `VIRTUAL_GEOMETRY` | Virtual geometry mesh rendering | Enable cluster DAG selection path |

### DX12 Requirements Summary

| Feature | Minimum | Used By |
|---------|---------|---------|
| Feature Level | 12.0 | All visibility features |
| Shader Model | 6.0 | HZB build, GPU culling, wave intrinsics |
| Shader Model | 6.5 | Mesh shaders, amplification shaders |
| ExecuteIndirect | FL 12.0 | GPU-driven draw submission |
| UAV Counter | FL 12.0 | Atomic append in cull shaders |
| Mesh Shader Tier | TIER_1 | Meshlet and virtual geometry rendering |
| Typed UAV Loads | FL 12.0 | HZB build compute shader |

### Performance Budget

| Operation | Target Time | Notes |
|-----------|-------------|-------|
| HZB Build (both phases) | < 0.3 ms | Dominated by memory bandwidth |
| GPU Cull (Phase 1) | < 0.5 ms | O(N) where N = instance count |
| GPU Cull (Phase 2) | < 0.2 ms | Only tests rejects from Phase 1 |
| SW Occlusion | < 0.1 ms | CPU-side, parallel with GPU |
| Meshlet AS Dispatch | < 0.3 ms | Per mesh shader pipeline draw |
| LOD Selection | < 0.05 ms | Runs inside CS_CullInstances |
| Total Visibility | < 1.5 ms | All culling combined |

---

## Related Specs

- [renderer.md](../specs/renderer.md) -- Render pipeline and pass ordering
- [gpu-instancing.md](../specs/gpu-instancing.md) -- Instance batching (input to GPU culler)
- [shader-variants.md](../specs/shader-variants.md) -- `MESHLET_RENDERING`, `LOD_CROSSFADE` defines
- [shader-library.md](../specs/shader-library.md) -- Shared HLSL includes, `common.hlsli`
- [screen-space-effects.md](../specs/screen-space-effects.md) -- Post-process stack integration
- [material-editor.md](material-editor.md) -- LOD material chains, per-material LOD bias
