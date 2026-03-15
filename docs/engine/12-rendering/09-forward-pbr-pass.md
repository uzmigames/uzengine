# Forward PBR Pass — Render Graph Integration & GPU Binding

> **Status**: Implemented (Milestone 1.2)
> **Source**: `runtime/engine/renderer/passes/pbr_pass.h`, `runtime/engine/renderer/constant_buffers.h`
> **Depends on**: Render Graph (§04), Material System (§06), Sort Key Architecture (§07), BRDF (§10)
> **References**: DirectX 12 render passes, per-object constant buffer pattern

## Purpose

The Forward PBR Pass is a single render graph pass that executes a Cook-Torrance BRDF pixel shader on all opaque geometry. The pass:

1. Clears the backbuffer and depth buffer
2. Begins a render pass with one color RT and depth buffer
3. Binds the PBR pipeline, per-frame CB, and material/vertex buffers
4. Iterates the sorted DrawList and issues one indexed draw call per command
5. Binds a fresh per-object CB for each draw (model matrix, normal matrix, material index)
6. Ends the render pass

This architecture minimizes GPU state transitions and memory bandwidth while maintaining full Material System flexibility.

## Render Graph Integration

### ForwardPBRPassDesc Configuration

The pass descriptor bundles all required resources and state:

```cpp
struct ForwardPBRPassDesc {
    /// Render targets
    rg::RgResourceId backbuffer;      ///< Logical RT (write, RenderTarget state)
    rg::RgResourceId depthBuffer;     ///< Logical depth buffer (write, DepthStencilWrite)
    RHITextureHandle  depthTexture;   ///< Physical depth texture (for render pass setup)

    /// Draw state
    const DrawList*   drawList;       ///< Sorted commands (must not be null)
    CBRingAllocator*  cbAllocator;    ///< For per-object CB allocation
    CBAllocation      perFrameAlloc;  ///< Pre-allocated per-frame CB (caller fills)

    /// GPU resources
    RHIPipelineHandle pipeline;       ///< PBR PSO handle
    uint32_t          vertexBufferSrvIndex; ///< SRV for bindless vertex fetch (SM6.6)
    RHIBufferHandle   vertexBuffer;   ///< VB handle (SM5 fallback)
    RHIBufferHandle   indexBuffer;    ///< IB handle
    RHIIndexFormat    indexFormat;    ///< Uint16 or Uint32

    /// Metadata
    uint32_t  indexCount;             ///< Indices per mesh (single-mesh for MVP)
    uint32_t  materialSrvIndex;       ///< SRV index for PBRMaterial StructuredBuffer
    bool      bindless;               ///< SM6.6 or SM5 path
    float     viewportWidth;          ///< Viewport dims (for DX12 viewport setup)
    float     viewportHeight;
};
```

### Pass Registration

```cpp
void AddForwardPBRPass(rg::RgBuilder& builder, const ForwardPBRPassDesc& desc);
```

**Usage (pseudocode)**:

```cpp
// In scene renderer initialization/per-frame:
ForwardPBRPassDesc passDesc{
    .backbuffer = renderGraph.ImportResource(backbufferHandle),
    .depthBuffer = renderGraph.ImportResource(depthHandle),
    .depthTexture = depthHandle,
    .drawList = &sortedDrawList,
    .cbAllocator = &cbRingAllocator,
    .perFrameAlloc = perFrameCBAlloc,
    .pipeline = pbrPipelineHandle,
    .vertexBufferSrvIndex = vertexBufferSRV,
    .vertexBuffer = vertexBufferHandle,
    .indexBuffer = indexBufferHandle,
    .indexFormat = RHIIndexFormat::Uint32,
    .indexCount = meshIndexCount,
    .materialSrvIndex = materialBufferSRV,
    .bindless = true,  // SM6.6
    .viewportWidth = 1920.0f,
    .viewportHeight = 1080.0f,
};

AddForwardPBRPass(renderGraph, passDesc);
```

### Render Graph Declaration

The pass declares resource access:

```cpp
// Inside AddForwardPBRPass:
builder.DeclareWrite(desc.backbuffer, ResourceState::RenderTarget);
builder.DeclareWrite(desc.depthBuffer, ResourceState::DepthStencilWrite);

// All other resources are read-only and implicitly available
```

The render graph compiler automatically:

1. Inserts barriers before the pass (backbuffer, depth to RenderTarget/DepthStencilWrite)
2. Inserts barriers after the pass (RT/depth to next pass's expected states)
3. Validates resource state transitions

## Pass Execution Flow

### 1. Initialization (Once Per Scene)

Before the first frame:

```cpp
// Create empty meshes for the MVP (single mesh + vertex/index buffer)
// Populate vertex/index buffers with a simple test mesh (e.g., cube or Stanford bunny)

// Create PBR PSO
RHIPipelineDesc psoDesc{
    .vertexShader = "mesh_vs.hlsl",  // Vertex shader with bindless or SM5 paths
    .pixelShader = "mesh_ps.hlsl",   // Cook-Torrance BRDF pixel shader
    .topology = PrimitiveTopology::TriangleList,
    .depthFormat = Format::D32_FLOAT,
    .rtFormat = Format::R8G8B8A8_UNORM,
};
pbrPipeline = device->CreatePipeline(psoDesc);

// Initialize material buffer
materialBuffer.Init(device, 256);  // 256-material capacity

// Initialize CB ring allocator (3-frame buffering, 64KB per frame)
cbRingAllocator.Init(device, 3, 64 * 1024);
```

### 2. Per-Frame Setup

At the start of the frame (before render graph execution):

```cpp
// Reset CB allocator for the current frame
cbRingAllocator.BeginFrame(frameIndex % 3);

// Fill per-frame CB (shared across all draw calls)
CBAllocation perFrameCBAlloc = cbRingAllocator.Allocate(sizeof(PerFrameCB));
PerFrameCB* perFrame = (PerFrameCB*)perFrameCBAlloc.cpuPtr;
perFrame->viewProj = viewProj;
perFrame->cameraPos = Vec4(cameraPos, 0.0f);
perFrame->timeParams = Vec4(totalTime, deltaTime, float(frameCount), 0.0f);
perFrame->lightDir = Vec4(normalize(sunDirection), 0.0f);
perFrame->lightColor = Vec4(sunColor * sunIntensity, 0.0f);
perFrame->ambientColor = Vec4(ambientColor, 0.0f);

// Upload all material changes for this frame
materialBuffer.Upload();

// Build sorted draw list
drawList.Clear();
for (const auto& renderable : visibleGeometry) {
    DrawCommand cmd;
    // ... populate cmd ...
    drawList.Add(cmd);
}
drawList.Sort();

// Register pass with render graph
ForwardPBRPassDesc desc{
    // ... (as shown above) ...
    .perFrameAlloc = perFrameCBAlloc,
};
AddForwardPBRPass(renderGraph, desc);

// Execute render graph (includes this pass)
renderGraph.Execute(commandList);
```

### 3. GPU Execution (Per-Draw)

Inside the pass callback (called by render graph during `Execute()`):

```cpp
void ExecuteForwardPBRPass(
    RHICommandList& cmdList,
    const ForwardPBRPassDesc& desc,
    const RGContext& context) noexcept
{
    // 1. Get physical resource handles from render graph
    auto [backbufferHandle, backbufferState] = context.GetPhysicalResource(desc.backbuffer);
    auto [depthHandle, depthState] = context.GetPhysicalResource(desc.depthBuffer);

    // 2. Clear targets
    cmdList.ClearRenderTarget(backbufferHandle, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
    cmdList.ClearDepth(depthHandle, 1.0f);  // Reversed-Z (1 = near, 0 = far)

    // 3. Begin render pass
    RHIRenderPassBegin renderPassBegin{
        .colorTargets = { { backbufferHandle, LoadOp::Load, StoreOp::Store } },
        .depthTarget = { depthHandle, LoadOp::Load, StoreOp::Store },
        .viewport = RHIViewport{0, 0, desc.viewportWidth, desc.viewportHeight},
        .scissorRect = RHIRect{0, 0, (uint32_t)desc.viewportWidth, (uint32_t)desc.viewportHeight},
    };
    cmdList.BeginRenderPass(renderPassBegin);

    // 4. Bind pipeline
    cmdList.SetPipeline(desc.pipeline);

    // 5. Bind per-frame CB (constant across all draws)
    cmdList.SetGraphicsRootConstantBufferView(
        1,  // Root param 1
        desc.perFrameAlloc.gpuAddr
    );

    // 6. Bind material StructuredBuffer (constant, referenced by index)
    if (desc.bindless) {
        // SM6.6: Pass SRV index via root constants
        cmdList.SetGraphicsRoot32BitConstant(0, desc.materialSrvIndex, 0);
    } else {
        // SM5: Bind descriptor table with material SRV
        cmdList.SetGraphicsRootDescriptorTable(3, desc.materialSrvIndex);
    }

    // 7. Bind vertex/index buffers (constant)
    if (desc.bindless) {
        // SM6.6: VB SRV index via root constant
        cmdList.SetGraphicsRoot32BitConstant(0, desc.vertexBufferSrvIndex, 1);
    } else {
        // SM5: Traditional VB binding
        cmdList.SetVertexBuffers(0, 1, &desc.vertexBuffer);
    }
    cmdList.SetIndexBuffer(desc.indexBuffer, desc.indexFormat);

    // 8. Iterate draw list and issue draw calls
    uint32_t previousMaterialId = UINT32_MAX;
    for (const auto& cmd : desc.drawList->GetCommands()) {
        // Allocate per-object CB for this draw call
        CBAllocation perObjAlloc = desc.cbAllocator->Allocate(sizeof(PerObjectCB));
        if (!perObjAlloc.cpuPtr) {
            UZ_LOG(Renderer, Warning, "Per-object CB ring buffer exhausted; skipping draw");
            continue;
        }

        // Fill per-object CB
        PerObjectCB* perObj = (PerObjectCB*)perObjAlloc.cpuPtr;
        perObj->model = cmd.worldMatrix;
        perObj->normalMatrix = cmd.normalMatrix;
        perObj->materialIndex = cmd.materialId;

        // Bind per-object CB at root param 2
        cmdList.SetGraphicsRootConstantBufferView(2, perObjAlloc.gpuAddr);

        // Issue draw call
        // Note: Using single-mesh for MVP. Future: use cmd.meshId to fetch actual mesh geometry.
        cmdList.DrawIndexedInstanced(
            desc.indexCount,  // IndexCountPerInstance
            1,                // InstanceCount
            0,                // StartIndexLocation
            0,                // BaseVertexLocation
            0                 // StartInstanceLocation
        );
    }

    // 9. End render pass
    cmdList.EndRenderPass();
}
```

## Per-Object Constant Buffer Binding

### PerObjectCB Structure

```cpp
struct PerObjectCB {
    Mat4     model;          ///< Object-to-world (column-major)
    Mat4     normalMatrix;   ///< transpose(inverse(model)) for normal transforms
    uint32_t materialIndex;  ///< Index into MaterialBuffer StructuredBuffer
    float    _pad[3];        ///< Align materialIndex to 16-byte boundary
    float    _pad2[28];      ///< Padding to 256-byte alignment
};

static_assert(sizeof(PerObjectCB) == 256);  // D3D12 CB alignment
```

### Ring Allocation Pattern

The CB ring allocator pre-allocates a large upload-heap buffer and suballocates 256-byte-aligned slots:

```
Frame 0 (offset 0–65535):
  Draw 0: [0–255]
  Draw 1: [256–511]
  Draw 2: [512–767]
  ...

Frame 1 (offset 65536–131071):
  Draw 0: [65536–65791]
  Draw 1: [65792–66047]
  ...

Frame 2 (offset 131072–196607):
  ...
```

This **triple-buffering** avoids GPU/CPU synchronization stalls: while the GPU consumes Frame 0, the CPU writes Frame 1 and prepares Frame 2.

### Per-Draw Allocation

```cpp
// In the draw loop:
CBAllocation alloc = cbAllocator->Allocate(sizeof(PerObjectCB));

// alloc.cpuPtr: CPU-writable address
// alloc.gpuAddr: GPU virtual address (for root CBV binding)
// alloc.offsetBytes: Byte offset into the backing buffer

// Write on CPU:
PerObjectCB* perObj = (PerObjectCB*)alloc.cpuPtr;
perObj->model = cmd.worldMatrix;
perObj->materialIndex = cmd.materialId;

// Bind on GPU:
cmdList.SetGraphicsRootConstantBufferView(2, alloc.gpuAddr);
```

## Shader Integration

### Vertex Shader (Bindless Path, SM6.6)

```hlsl
// Input layout:
struct VSInput {
    uint vertexId : SV_VertexID;
};

// Push constants (root param 0):
ConstantBuffer<PushConstants> push : register(b0);

// PerFrameCB (root param 1):
cbuffer PerFrameCB : register(b1) {
    row_major float4x4 viewProj;
    float4 cameraPos;
    // ... (as defined in constant_buffers.h)
};

// PerObjectCB (root param 2):
cbuffer PerObjectCB : register(b2) {
    row_major float4x4 model;
    row_major float4x4 normalMatrix;
    uint materialIndex;
};

struct VSOutput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float3 positionWS : TEXCOORD1;
};

VSOutput main(VSInput input) {
    // Fetch vertex via bindless SRV
    StructuredBuffer<Vertex> vertices = ResourceDescriptorHeap[push.vertexBufferIndex];
    Vertex v = vertices[input.vertexId];

    // Transform to world and clip space
    float4 posWS = mul(float4(v.position, 1.0), model);
    float4 posCS = mul(posWS, viewProj);

    // Transform normal
    float3 normalWS = normalize(mul(v.normal, (float3x3)normalMatrix));

    VSOutput output;
    output.positionCS = posCS;
    output.normalWS = normalWS;
    output.texcoord = v.texcoord;
    output.positionWS = posWS.xyz;
    return output;
}
```

### Pixel Shader (Cook-Torrance BRDF)

```hlsl
// PerFrameCB and PerObjectCB as above

// Material StructuredBuffer:
StructuredBuffer<PBRMaterial> materials = ResourceDescriptorHeap[push.materialSrvIndex];

struct PSInput {
    float4 positionCS : SV_Position;
    float3 normalWS   : NORMAL;
    float2 texcoord   : TEXCOORD0;
    float3 positionWS : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target {
    // Fetch material
    PBRMaterial mat = materials[materialIndex];

    // Surface vectors
    float3 N = normalize(input.normalWS);
    float3 V = normalize(cameraPos.xyz - input.positionWS);
    float3 L = normalize(lightDir.xyz);
    float3 H = normalize(V + L);

    // Cook-Torrance BRDF calculation
    // (See §10-cook-torrance-brdf.md for complete shader)
    float NdotL = max(dot(N, L), 0.001);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = saturate(dot(N, H));

    float alpha = max(mat.roughness * mat.roughness, MIN_ROUGHNESS * MIN_ROUGHNESS);
    float D = DistributionGGX(NdotH, alpha);
    float G = GeometrySmith(NdotV, NdotL, alpha);
    float3 F = FresnelSchlick(dot(V, H), lerp(float3(0.04, 0.04, 0.04), mat.baseColor.rgb, mat.metallic));

    float3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.001);
    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - mat.metallic);
    float3 diffuse = kD * mat.baseColor.rgb / PI;

    float3 Lo = (diffuse + specular) * lightColor.xyz * NdotL;
    float3 ambient = ambientColor.xyz * mat.baseColor.rgb * mat.ao;
    float3 color = ambient + Lo + mat.emissive.rgb * mat.emissive.a;

    // Tonemap + gamma
    color = color / (color + 1.0);
    color = pow(color, 1.0 / 2.2);

    return float4(color, 1.0);
}
```

## Pipeline State Object (PSO)

The PBR pipeline encodes:

```cpp
RHIPipelineDesc pbrDesc{
    .vertexShader = "mesh_vs.hlsl",
    .pixelShader = "mesh_ps.hlsl",
    .blendMode = BlendMode::Opaque,
    .depthMode = DepthMode::Test | DepthMode::Write,
    .depthCompareOp = CompareOp::GreaterOrEqual,  // Reversed-Z
    .cullMode = CullMode::Back,
    .topology = PrimitiveTopology::TriangleList,
    .rtFormat = Format::R8G8B8A8_UNORM,
    .depthFormat = Format::D32_FLOAT,
};

RHIPipelineHandle pipeline = device->CreatePipeline(pbrDesc);
```

## Engine Comparison

| Feature | Unreal 5 | Godot 4 | **UzEngine 1.2** |
|---------|----------|---------|------------------|
| Pass abstraction | `FViewFamilyRenderer` + `FSceneRenderer` | `RenderingServer` (free functions) | **Render graph node with callback** |
| CB allocation | `FRDGPassParameters` with transient CBs | Static `ShaderGlobals` | **CBRingAllocator (triple-buffered)** |
| Per-object data | `FPrimitiveSceneProxy` proxy buffer | Mesh instance draw call | **PerObjectCB per draw** |
| Material lookup | PSO hash + material instance cache | Shader binding by slot | **Index into StructuredBuffer** |
| Bindless support | DirectX 12 only; opt-in | Limited (Vulkan) | **SM6.6 primary; SM5 fallback** |

## Rules

1. **ALWAYS allocate per-object CB from the ring allocator** — never create inline CBs
2. **ALWAYS call `cbAllocator.BeginFrame()` at frame start** — resets the write offset
3. **NEVER exceed the ring allocator capacity** — 64KB/frame supports ~250 draw calls (256 bytes per CB)
4. **ALWAYS validate descriptor indices before binding** — invalid SRV index crashes the driver
5. **ALWAYS use reversed-Z depth (1 = near, 0 = far)** — matches modern GPU conventions
6. **NEVER skip the render pass begin/end** — required for DXVK/Vulkan translation

## Future Enhancements

- **Dynamic mesh indexing** — use `cmd.meshId` to index a mesh array, rendering multiple geometries
- **Multi-light loop** — iterate light array in pixel shader (or deferred lighting pass)
- **Shadow mapping** — depth pass for shadow casters, shadow texture in PS
- **Normal/roughness maps** — extend shader to sample texture maps, encode in sort key
- **Occlusion queries** — per-draw query for GPU culling feedback
