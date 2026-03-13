# Render Graph Pass Authoring Guide

> **Status**: Done (Phase 3a-3.1.0)
> **Companion to**: Render Graph Overview (`04-render-graph.md`)
> **Audience**: Rendering engineers and graphics programmers authoring new passes

This guide walks you through the step-by-step process of creating, declaring, and executing a render pass using the render graph API. It covers everything from conceptual design to GPU debugging.

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Anatomy of a Render Pass](#anatomy-of-a-render-pass)
3. [Step-by-Step Tutorial](#step-by-step-tutorial)
4. [Pass Patterns by Type](#pass-patterns-by-type)
5. [Resource Management](#resource-management)
6. [Debugging Passes](#debugging-passes)
7. [Performance Optimization](#performance-optimization)
8. [Troubleshooting](#troubleshooting)

## Prerequisites

Before authoring render passes, you should understand:
- **RHI fundamentals** (`03-rhi.md`): Resource states, barriers, command lists
- **Render graph overview** (`04-render-graph.md`): DAG model, compilation phases, pooling
- **Shader pipeline** (`05-shader-pipeline.md`): Shader compilation and hot-reload
- **C++17 lambdas**: Captures, move semantics, std::function

## Anatomy of a Render Pass

Every render pass has three parts:

### 1. Resource Declaration (What it reads/writes)

```cpp
rg.AddPass("MyPass", [&](RgPassBuilder& pb) {
    // Declare inputs (things this pass reads)
    pb.Read(inputTexture, RHIResourceState::ShaderResource);
    pb.Read(constantBuffer, RHIResourceState::ConstantBuffer);

    // Declare outputs (things this pass writes)
    pb.Write(outputTexture, RHIResourceState::RenderTarget);
    // ... more declarations ...
});
```

### 2. Execution Callback (What GPU commands to record)

```cpp
pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
    // Resolve virtual handles to physical RHI handles
    auto input = res.GetTexture(inputTexture);
    auto output = res.GetTexture(outputTexture);

    // Record GPU commands using resolved handles
    cmd->SetRenderTargets({output}, nullptr);
    cmd->SetShaderResourceView(0, input);
    cmd->SetGraphicsPipeline(myPSO);
    cmd->DrawInstanced(4, 1);  // Full-screen quad
});
```

### 3. Proper Ordering

**Always declare dependencies first, then set the callback:**

```cpp
rg.AddPass("MyPass", [&](RgPassBuilder& pb) {
    // ✅ CORRECT: Declare all reads/writes first
    pb.Read(inputA, ShaderResource);
    pb.Read(inputB, ShaderResource);
    pb.Write(output, RenderTarget);

    // ✅ Then set the callback
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        // ... GPU commands ...
    });

    // ❌ WRONG: Callback before declarations
    // pb.SetExecutionCallback([=]{...});
    // pb.Read(inputA, ShaderResource);  // Too late!
});
```

## Step-by-Step Tutorial

### Tutorial 1: Full-Screen Post-Processing Pass

This example implements a simple color grading pass that reads scene color and outputs tone-mapped color.

**Step 1: Plan the pass**

```
Input: Scene color (HDR, RGBA16Float, in RenderTarget state)
Output: Tone-mapped color (LDR, RGBA8Unorm, in RenderTarget state)
Operations: Read from input, apply tone-mapping, write output
```

**Step 2: Create output texture**

```cpp
auto tonemappedColor = rg.CreateTexture("TonemappedColor", {
    .width = 1920,
    .height = 1080,
    .format = RHIFormat::RGBA8Unorm,  // LDR for display
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});
```

**Step 3: Register the pass**

```cpp
rg.AddPass("Tonemap", [&](RgPassBuilder& pb) {
    // Step 3a: Declare what you read
    pb.Read(sceneColor, RHIResourceState::ShaderResource);

    // Step 3b: Declare what you write
    pb.Write(tonemappedColor, RHIResourceState::RenderTarget);

    // Step 3c: Set execution callback
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        // Resolve virtual handles to physical RHI handles
        auto inputSRV = res.GetTexture(sceneColor);
        auto outputRTV = res.GetTexture(tonemappedColor);

        // Set up render pass
        cmd->SetRenderTargets({outputRTV}, nullptr);
        cmd->SetViewport(0, 0, 1920, 1080);
        cmd->ClearRenderTargets({0, 0, 0, 1});  // Clear to black

        // Bind resources
        cmd->SetGraphicsPipeline(tonemapPSO);
        cmd->SetShaderResourceView(0, inputSRV);
        cmd->SetConstantBuffer(0, tonemapConstantsCB);

        // Draw full-screen quad (vertex positions encoded in shader)
        cmd->DrawInstanced(4, 1);
    });
});
```

### Tutorial 2: Deferred Shading Pass (Multiple Inputs/Outputs)

This example shows a lighting accumulation pass that consumes a GBuffer and outputs scene color.

**Step 1: Plan the pass**

```
Inputs:
  - GBuffer.Albedo (RGBA8Unorm, ShaderResource)
  - GBuffer.Normal (RGBA8Unorm, ShaderResource)
  - GBuffer.Emissive (RGBA8Unorm, ShaderResource)
  - GBuffer.Depth (R32Float, ShaderResource)
  - ShadowMap (R32Float, ShaderResource)

Outputs:
  - SceneColor (RGBA16Float, RenderTarget)

Operations: Read all inputs, accumulate lighting per pixel, output to scene color
```

**Step 2: Create/import output texture**

```cpp
auto sceneColor = rg.CreateTexture("SceneColor", {
    .width = 1920,
    .height = 1080,
    .format = RHIFormat::RGBA16Float,
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});
```

**Step 3: Register the pass**

```cpp
rg.AddPass("Lighting", [&](RgPassBuilder& pb) {
    // Chain reads together for convenience (returns same ID for next call)
    pb.Read(gbufferAlbedo, RHIResourceState::ShaderResource)
      .Read(gbufferNormal, RHIResourceState::ShaderResource)
      .Read(gbufferEmissive, RHIResourceState::ShaderResource)
      .Read(gbufferDepth, RHIResourceState::ShaderResource)
      .Read(shadowMap, RHIResourceState::ShaderResource);

    pb.Write(sceneColor, RHIResourceState::RenderTarget);

    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        // Resolve all inputs
        auto albedoSRV = res.GetTexture(gbufferAlbedo);
        auto normalSRV = res.GetTexture(gbufferNormal);
        auto emissiveSRV = res.GetTexture(gbufferEmissive);
        auto depthSRV = res.GetTexture(gbufferDepth);
        auto shadowSRV = res.GetTexture(shadowMap);
        auto sceneColorRTV = res.GetTexture(sceneColor);

        // Set up render pass
        cmd->SetRenderTargets({sceneColorRTV}, nullptr);
        cmd->SetViewport(0, 0, 1920, 1080);
        cmd->ClearRenderTargets({0, 0, 0, 0});

        // Bind all inputs
        cmd->SetGraphicsPipeline(lightingPSO);
        cmd->SetShaderResourceView(0, albedoSRV);
        cmd->SetShaderResourceView(1, normalSRV);
        cmd->SetShaderResourceView(2, emissiveSRV);
        cmd->SetShaderResourceView(3, depthSRV);
        cmd->SetShaderResourceView(4, shadowSRV);

        // Bind light constants (updated per-frame, passed into closure)
        cmd->SetConstantBuffer(0, lightDataCB);

        // Draw full-screen quad
        cmd->DrawInstanced(4, 1);
    });
});
```

**Key observations:**
- Multiple inputs are declared with chained `.Read()` calls (returns the same ID)
- All inputs are resolved inside the callback
- The callback captures input IDs by value (`[=]`), then resolves them to physical handles
- Compiler automatically inserts barriers between GBuffer generation and Lighting pass

### Tutorial 3: Compute Shader with Buffer Output

This example builds a hierarchical Z-buffer (HZB) using a compute shader.

**Step 1: Plan the pass**

```
Input: Scene depth (R32Float, ShaderResource)
Output: HZB mip chain (Structured buffer, UnorderedAccess)
Operations: Read depth, compute pyramid, output to UAV
```

**Step 2: Create buffer**

```cpp
auto hzbBuffer = rg.CreateBuffer("HZBMipChain", {
    .sizeBytes = 512 * 512 * 4,  // Pyramid: 256x256 + 128x128 + 64x64 + ... mips
    .stride = 4,  // uint (one float per texel)
    .usage = RHIBufferUsage::StorageBuffer | RHIBufferUsage::UnorderedAccess
});
```

**Step 3: Register the pass**

```cpp
rg.AddPass("BuildHZB", [&](RgPassBuilder& pb) {
    pb.Read(sceneDepth, RHIResourceState::ShaderResource);
    pb.Write(hzbBuffer, RHIResourceState::UnorderedAccess);

    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto depthSRV = res.GetTexture(sceneDepth);
        auto hzbUAV = res.GetBuffer(hzbBuffer);

        // Set up compute pass
        cmd->SetComputePipeline(hzbBuildCSO);
        cmd->SetShaderResourceView(0, depthSRV);
        cmd->SetUnorderedAccessView(0, hzbUAV);

        // Dispatch 32x32 thread groups (covers 512x512 texture)
        cmd->Dispatch(32, 32, 1);
    });
});
```

**Key observations:**
- Compute shader uses different pipeline type (CSO not PSO)
- Output is a buffer, not texture (use `GetBuffer()` for UAV resolution)
- `Dispatch()` instead of draw calls

### Tutorial 4: Depth Prepass (Depth-Only Rendering)

This example renders geometry to a depth texture only (no color output).

**Step 1: Plan the pass**

```
Input: Scene constants, mesh data
Output: Depth texture (D32, DepthWrite)
Operations: Render all geometry, output depth only
```

**Step 2: Create depth texture**

```cpp
auto sceneDepth = rg.CreateTexture("SceneDepth", {
    .width = 1920,
    .height = 1080,
    .format = RHIFormat::D32Float,
    .usage = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource
});
```

**Step 3: Register the pass**

```cpp
rg.AddPass("DepthPrepass", [=](RgPassBuilder& pb) {
    pb.Write(sceneDepth, RHIResourceState::DepthStencilWrite);

    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto depthDSV = res.GetTexture(sceneDepth);

        // Set up depth-only render pass
        cmd->SetRenderTargets({}, depthDSV);  // No color targets, only depth
        cmd->SetViewport(0, 0, 1920, 1080);
        cmd->ClearDepthStencil(depthDSV, 1.0f);  // Clear to far plane

        // Bind depth-only PSO
        cmd->SetGraphicsPipeline(depthOnlyPSO);
        cmd->SetConstantBuffer(0, viewProjectionCB);

        // Render all opaque geometry
        for (const auto& mesh : opaqueGeometry) {
            cmd->SetVertexBuffer(0, mesh.vertexBuffer);
            cmd->SetIndexBuffer(mesh.indexBuffer);
            cmd->DrawIndexed(mesh.indexCount);
        }
    });
});
```

**Key observations:**
- Color targets can be empty (`{}`) when only rendering depth
- Use `DepthStencilWrite` state for depth output
- Depth texture format must be depth-appropriate (D16, D24S8, D32Float, etc.)

## Pass Patterns by Type

### Pattern A: Render-to-Texture (RTT)

Render geometry to a texture, then read that texture in a later pass.

```cpp
auto diffuseLighting = rg.CreateTexture("DiffuseLighting", {...});

rg.AddPass("RenderDiffuse", [=](RgPassBuilder& pb) {
    pb.Write(diffuseLighting, RHIResourceState::RenderTarget);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto rtv = res.GetTexture(diffuseLighting);
        cmd->SetRenderTargets({rtv}, nullptr);
        // Render geometry...
    });
});

// Later pass reads the texture
rg.AddPass("CompositePass", [=](RgPassBuilder& pb) {
    pb.Read(diffuseLighting, RHIResourceState::ShaderResource);
    // Compiler automatically inserts barrier: RenderTarget → ShaderResource
    // ...
});
```

**Compiler handles the barrier transition automatically.**

### Pattern B: Compute Shader -> Graphics Shader

Compute pass produces data, graphics pass consumes it.

```cpp
auto computeOutput = rg.CreateBuffer("ComputeResult", {...});

rg.AddPass("ComputePhase", [=](RgPassBuilder& pb) {
    pb.Write(computeOutput, RHIResourceState::UnorderedAccess);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto uav = res.GetBuffer(computeOutput);
        cmd->SetComputePipeline(cso);
        cmd->SetUnorderedAccessView(0, uav);
        cmd->Dispatch(32, 32, 1);
    });
});

rg.AddPass("GraphicsPhase", [=](RgPassBuilder& pb) {
    pb.Read(computeOutput, RHIResourceState::ShaderResource);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto srv = res.GetBuffer(computeOutput);
        cmd->SetGraphicsPipeline(pso);
        cmd->SetShaderResourceView(0, srv);
        // Compiler inserts UAV → SRV barrier
        // ...
    });
});
```

### Pattern C: Copy Operations

Move data between resources.

```cpp
rg.AddPass("CopyPass", [=](RgPassBuilder& pb) {
    pb.Read(source, RHIResourceState::CopySrc);
    pb.Write(dest, RHIResourceState::CopyDst);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto src = res.GetTexture(source);
        auto dst = res.GetTexture(dest);
        cmd->CopyTexture(src, dst);
    });
});
```

### Pattern D: Indirect Rendering

Pass uses output of another pass as indirect arguments.

```cpp
rg.AddPass("CullPass", [=](RgPassBuilder& pb) {
    pb.Write(indirectArgs, RHIResourceState::UnorderedAccess);
    // Compute pass populates indirect arguments...
});

rg.AddPass("DrawIndirect", [=](RgPassBuilder& pb) {
    pb.Read(indirectArgs, RHIResourceState::IndirectArgument);
    pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
        auto argsBuffer = res.GetBuffer(indirectArgs);
        cmd->SetGraphicsPipeline(pso);
        cmd->DrawIndirect(argsBuffer, 0);  // Argument offset in buffer
    });
});
```

## Resource Management

### Creating Transient Textures

**Use for temporary data that doesn't persist across frames.**

```cpp
auto tempSurface = rg.CreateTexture("TempSurface", {
    .width = 1920,
    .height = 1080,
    .format = RHIFormat::RGBA8Unorm,
    .usage = RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource
});
```

Properties:
- Lifetime is one frame
- Automatically pooled and reused by `RgResourcePool`
- Allocated on first access, returned after frame completes
- No explicit cleanup needed

### Creating Transient Buffers

**Use for temporary data (HZB, indirect args, upload buffers).**

```cpp
auto tempBuffer = rg.CreateBuffer("TempBuffer", {
    .sizeBytes = 1024 * 1024,
    .stride = 16,
    .usage = RHIBufferUsage::StorageBuffer | RHIBufferUsage::UnorderedAccess
});
```

Properties:
- Same lifetime as transient textures
- Pooled by descriptor (size, stride, usage)
- Cleared on allocation but not zeroed

### Importing External Resources

**Use for resources managed outside the graph (swapchain, persistent scene textures).**

```cpp
auto backBuffer = rg.ImportTexture("BackBuffer", swapChainTextureHandle,
                                   RHIResourceState::Present);

auto shadowMapAtlas = rg.ImportTexture("ShadowMapAtlas", persistentShadowAtlasHandle,
                                       RHIResourceState::ShaderResource);
```

Properties:
- Caller retains ownership (graph doesn't allocate or free)
- Graph tracks state transitions (inserts barriers as needed)
- Must specify initial state at import time

### Resource Naming

Always name resources and passes. Names appear in:
- Log output (`RenderGraph: created texture 'GBuffer.Albedo'`)
- GPU debugger (PIX, Xcode Instruments)
- Profiling captures
- Error messages

```cpp
auto result = rg.CreateTexture("PostProcess.Bloom", {...});  // ✅ Good
auto result = rg.CreateTexture("temp", {...});               // ❌ Bad
auto result = rg.CreateTexture(nullptr, {...});              // ❌ Very bad
```

### Resource Pooling Strategy

The `RgResourcePool` caches physical resources by descriptor:

```cpp
// Frame 1:
auto tex1 = rg.CreateTexture("Tex1", {1920, 1080, RGBA8, RenderTarget | SRV});
rg.Execute(cmd);
// → Pool acquires new texture from device, releases it after frame

// Frame 2:
auto tex2 = rg.CreateTexture("Tex2", {1920, 1080, RGBA8, RenderTarget | SRV});
rg.Execute(cmd);
// → Pool reuses same physical texture (same descriptor)
// Memory allocation avoided!

// Frame 3 (memory pressure):
pool.Trim();  // Destroy unused cached resources
```

**Pool keeps resources indefinitely.** Call `Trim()` to reclaim memory:

```cpp
// In renderer destructor or memory pressure handler
resourcePool->Trim();
```

## Debugging Passes

### Enable Trace Logging

```cpp
uz::SetLogLevel("Renderer", "Trace");

// Output shows:
// RenderGraph: created transient texture 'GBuffer.Albedo' (1920x1080, id=0)
// RenderGraph: registered pass 'GBuffer Pass' (3 accesses)
// RenderGraph: compiled — 10 passes (1 culled), 23 barriers
```

### Inspect Compiled Graph

```cpp
rg.Compile();

uint32_t total = rg.GetPassCount();
uint32_t culled = rg.GetCulledPassCount();
uint32_t barriers = rg.GetBarrierCount();

UZ_LOG(Renderer, Info, "Total passes: {}, culled: {}, barriers: {}",
       total, culled, barriers);

if (culled > expected_culled) {
    UZ_LOG(Renderer, Warning, "More passes culled than expected!");
}
```

### Check Resource Validity

```cpp
for (uint32_t i = 0; i < rg.GetResourceCount(); ++i) {
    const auto& res = rg.GetResource(RgResourceId{i});
    if (!res.imported && res.culled) {
        UZ_LOG(Renderer, Info, "Culled resource: {}", res.name ? res.name : "unnamed");
    }
}
```

### GPU Debugging

**Use your graphics debugger (PIX, Xcode, etc.) to:**
- Verify pass execution order (matches topological sort)
- Inspect barriers (state transitions should match declarations)
- Profile pass execution time
- Validate resource bindings

Each pass is wrapped in a **marker** (via `IRHICommandList::BeginMarker()`):

```
Frame capture shows:
  ▼ Marker: "GBuffer Pass"
    │ Clear render targets
    │ Set pipeline
    │ Draw calls...
  ▼ Marker: "Lighting Pass"
    │ Barrier: GBuffer.Albedo (RenderTarget → ShaderResource)
    │ Set pipeline
    │ Draw calls...
```

## Performance Optimization

### Optimization 1: Minimize Barriers

**Declare accurate resource states.** A barrier costs GPU time and stalls the pipeline.

```cpp
// ❌ WRONG: Over-declares access (unnecessary barrier)
pb.Read(depthTexture, RHIResourceState::CopySrc);  // Only reading, but declares copy source
// Should be ShaderResource, not CopySrc

// ✅ CORRECT: Minimal, accurate state
pb.Read(depthTexture, RHIResourceState::ShaderResource);
```

### Optimization 2: Batch Resources

Group reads/writes to reduce pipeline flushes.

```cpp
// ❌ Bad: Interleaved reads/writes
pb.Read(albedo, ShaderResource);
pb.Write(output1, RenderTarget);
pb.Read(normal, ShaderResource);
pb.Write(output2, RenderTarget);

// ✅ Better: Group by type
pb.Read(albedo, ShaderResource);
pb.Read(normal, ShaderResource);
pb.Write(output1, RenderTarget);
pb.Write(output2, RenderTarget);
```

### Optimization 3: Enable Pass Culling

Disabled effects should not be added to the graph.

```cpp
// ✅ Conditional pass registration
if (settings.bloomEnabled) {
    rg.AddPass("Bloom", [=](RgPassBuilder& pb) {
        // ...
    });
}

// ⚠️ Alternative: Always register, let compiler cull
// Works but less efficient (graph still analyzes culled pass)
```

### Optimization 4: Reuse Resources Across Frames

Imported resources allow cross-frame reuse.

```cpp
// Persistent shadow map atlas (created once, reused every frame)
auto shadowAtlas = rg.ImportTexture("ShadowMap", persistent_shadow_texture,
                                    RHIResourceState::ShaderResource);

// Each frame:
rg.AddPass("RenderShadowMaps", [=](RgPassBuilder& pb) {
    pb.Write(shadowAtlas, RHIResourceState::RenderTarget);
    // ...
});

rg.AddPass("LightingPass", [=](RgPassBuilder& pb) {
    pb.Read(shadowAtlas, RHIResourceState::ShaderResource);
    // ...
});
```

### Optimization 5: Profile Resource Pool

Monitor pool usage to identify memory waste.

```cpp
uint32_t cachedTextures = pool.GetCachedTextureCount();
uint32_t cachedBuffers = pool.GetCachedBufferCount();

if (cachedTextures > threshold) {
    UZ_LOG(Renderer, Warning, "Pool has {} unused textures, trimming", cachedTextures);
    pool.Trim();
}
```

## Troubleshooting

### Issue: Pass Never Executes

**Symptoms**: Execution callback never called, pass not in GPU profiler.

**Causes**:
1. Pass output is never read by another pass
2. Pass is culled by compiler
3. Callback was never set

**Debug**:
```cpp
rg.Compile();
uint32_t culled = rg.GetCulledPassCount();
if (culled > 0) {
    UZ_LOG(Renderer, Warning, "{} passes were culled!", culled);
}
```

**Solution**:
```cpp
// Option 1: Ensure output is read by another pass
rg.AddPass("Bloom", [=](RgPassBuilder& pb) {
    auto bloomOut = rg.CreateTexture("BloomResult", {...});
    pb.Write(bloomOut, RHIResourceState::RenderTarget);
    pb.SetExecutionCallback([=]{...});
});

rg.AddPass("Composite", [=](RgPassBuilder& pb) {
    pb.Read(bloomOut, RHIResourceState::ShaderResource);  // Now bloom won't be culled
    pb.SetExecutionCallback([=]{...});
});

// Option 2: Import the output (makes it "live")
auto bloomOut = rg.CreateTexture("BloomResult", {...});
// ... register bloom pass ...
// If nobody reads it:
auto exported = rg.ImportTexture("BloomExport", bloomOut, ...);  // Keeps bloom alive
```

### Issue: Crash on Resource Lookup

**Symptoms**: `UZ_CHECK(entry.kind == Texture)` fails, or `UZ_CHECK(entry.physicalTexture.IsValid())`

**Causes**:
1. Accessing resource not declared by pass
2. Type mismatch (calling `GetBuffer()` on texture, or vice versa)
3. Resource not created or imported

**Solution**:
```cpp
// ❌ WRONG: Never declared inputTexture in Read()
pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
    auto tex = res.GetTexture(inputTexture);  // Crash: inputTexture not declared
});

// ✅ CORRECT: Declare first
pb.Read(inputTexture, RHIResourceState::ShaderResource);
pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
    auto tex = res.GetTexture(inputTexture);  // OK
});

// ❌ WRONG: Type mismatch
auto buffer = rg.CreateBuffer("MyBuffer", {...});
pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {
    auto tex = res.GetTexture(buffer);  // Crash: buffer is not a texture
});

// ✅ CORRECT: Use correct getter
auto buf = res.GetBuffer(buffer);  // OK
```

### Issue: Barrier State Mismatch

**Symptoms**: GPU warnings about incorrect resource state in GPU debugger.

**Causes**:
1. Declared state doesn't match actual GPU state
2. Forgot to declare a read or write
3. Declared wrong state

**Debug**:
```cpp
uz::SetLogLevel("Renderer", "Trace");
rg.Compile();
// Logs will show scheduled barriers and state transitions

// In GPU debugger, verify:
// - Barriers appear before each pass
// - State transitions match declarations
// - No state conflicts (two passes writing same resource concurrently)
```

**Solution**:
```cpp
// ❌ WRONG: Missing read declaration
pb.Read(texture, RHIResourceState::ShaderResource);
pb.Write(texture, RHIResourceState::RenderTarget);
// Compiler sees: texture ends in RenderTarget state
// Next pass reads it? Compiler doesn't know about that read!

// ✅ CORRECT: Declare all accesses
pb.Read(textureInput, RHIResourceState::ShaderResource);
pb.Write(textureOutput, RHIResourceState::RenderTarget);
```

### Issue: Memory Leak (Pool Exhaustion)

**Symptoms**: Pool grows indefinitely, VRAM pressure increases.

**Causes**:
1. Not releasing resources after frame
2. Pool not trimmed
3. Creating resources with different descriptors each frame

**Solution**:
```cpp
// After each frame, compiler calls ReleasePhysicalResources()
// But unused cached resources accumulate

// Periodically trim:
if (frame_count % 100 == 0) {
    resourcePool->Trim();
}

// Or on memory pressure:
void Renderer::OnMemoryPressure() {
    resourcePool->Trim();
}

// Check pool status:
UZ_LOG(Renderer, Info, "Pool cached textures: {}, buffers: {}",
       resourcePool->GetCachedTextureCount(),
       resourcePool->GetCachedBufferCount());
```

## Related Documentation

- **Render Graph Overview** (`04-render-graph.md`) — API reference, compilation phases
- **RHI** (`03-rhi.md`) — GPU resource states, command list interface
- **Render World** (`12-render-world.md`) — Scene data for rendering
- **Shader Pipeline** (`05-shader-pipeline.md`) — Shader compilation and hot-reload

## Checklist: Implementing a New Pass

Use this checklist when authoring a new render pass:

- [ ] **Plan**: Understand inputs, outputs, GPU operations
- [ ] **Create resources**: `CreateTexture()`, `CreateBuffer()`, or `ImportTexture()`
- [ ] **Register pass**: `RgBuilder::AddPass(name, [](RgPassBuilder& pb) {...})`
- [ ] **Declare reads**: `pb.Read()` for all input resources + correct states
- [ ] **Declare writes**: `pb.Write()` for all output resources + correct states
- [ ] **Set callback**: `pb.SetExecutionCallback([=](IRHICommandList* cmd, const RgPassResources& res) {...})`
- [ ] **Resolve handles**: `res.GetTexture()` / `res.GetBuffer()` inside callback
- [ ] **Record commands**: Bind resources, set pipeline, draw/dispatch
- [ ] **Test**: Enable trace logging, verify pass executes in GPU profiler
- [ ] **Optimize**: Minimize barriers, batch accesses, enable culling
- [ ] **Debug**: Check GPU captures, verify barrier state transitions
- [ ] **Document**: Update pass comments with inputs/outputs/purpose

