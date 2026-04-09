# Spec — Debug View Modes

> **Status**: Done (v0.3.0+ — 13 modes implemented via --view-mode CLI and debug_view_ps.hlsl)
> **Dependencies**: Deferred G-Buffer rendering, Lighting pass, Post-processing pipeline

---

## Overview

**Debug View Modes** is a developer-facing visualization system that displays individual G-Buffer channels and rendering pipeline state in real-time without shader recompilation. It enables rapid iteration on rendering issues, material configuration, and lighting bugs.

The system provides **13 visualization modes**:
- **Lit** (default PBR rendering)
- **Unlit** (albedo only, no lighting)
- **Albedo**, **Metallic**, **Roughness**, **AO** (individual material parameters)
- **WorldNormal** (surface normals, decoded from octahedral encoding)
- **Depth** (linearized depth)
- **WorldPosition** (absolute world coordinates)
- **Lighting** (direct lighting contribution before albedo multiply)
- **Shadows** (shadow factor visualization)
- **Wireframe** (geometry edge overlay)
- **Overview** (composite grid of all channels with labels)

Modes are toggled via:
1. **Keyboard shortcuts**: `F1`–`F11` for quick iteration
2. **CLI argument**: `--view-mode <name>` for headless/automated testing
3. **Lua API**: `ctx.debug.set_view_mode(modeName)` for scripted testing
4. **Inspector/UI** (future): Runtime UI panel in editor

---

## Architecture

### Design Philosophy

**Runtime mode switching with no shader recompilation** — inspired by Godot's lightweight `DebugDraw` enum pattern combined with UE5's comprehensive Buffer Visualization system.

Instead of:
- Shader variants (slow, many PSOs)
- Full pipeline recompilation (slow)
- Manual fallback shaders (maintenance burden)

We use:
- Single `debug_view_ps.hlsl` shader with branching logic
- Constant buffer flag to select mode (cheapest possible branch)
- Fullscreen post-process pass (minimal overhead, ~0.5ms)
- No external dependencies (uses existing G-Buffer layout)

### Pass Insertion in Render Pipeline

```
Render Flow:
┌─────────────────┐
│ Geometry Pass   │ (outputs G-Buffer: GBuffer0, GBuffer1, GBuffer2, Depth)
└────────┬────────┘
         │
         ▼
┌─────────────────────┐
│ Lighting Pass       │ (samples G-Buffer → outputs LightingBuffer)
└────────┬────────────┘
         │
         ├─ [If debugMode == Lit, skip to tonemapping]
         │
         ├─ [Else, insert debug view pass]
         ▼
┌──────────────────────────────────┐
│ Debug View Post-Process Pass     │ (reads G-Buffer+LightingBuffer, outputs DebugOutputRT)
└────────┬─────────────────────────┘
         │
         ▼
┌──────────────────────────────────┐
│ Tonemapping & Post-Processing    │ (reads DebugOutputRT or LightingBuffer, outputs final HDR)
└────────┬─────────────────────────┘
         │
         ▼
┌──────────────────┐
│ Display Output   │
└──────────────────┘
```

**Key design points**:
- Debug pass is inserted **after** lighting, **before** tonemapping
- Reads G-Buffer (geometry pass output) + LightingBuffer (lighting pass output)
- Writes to temporary RT (`DebugOutputRT`, reused post-process intermediate)
- Tonemapping chain continues with debug output as input
- If `debugMode == Lit`, pass is skipped entirely (zero overhead)

### Data Flow

```cpp
// Simplified pseudo-code

// Geometry pass (existing)
gBuffer0 = float4(albedo, ao)
gBuffer1 = float4(normal_encoded, metallic, roughness)
gBuffer2 = float4(world_position, unused)
depth    = scene_depth

// Lighting pass (existing)
lighting_output = PBR(gBuffer, depth, lights, shadows)

// Debug view pass (NEW)
if (debug_mode != Lit) {
    switch (debug_mode) {
        case Albedo:         output = gBuffer0.rgb
        case Normal:         output = DecodeOctahedral(gBuffer1.rg)
        case Metallic:       output = gBuffer1.b
        case Roughness:      output = gBuffer1.a
        case AO:             output = gBuffer0.a
        case Depth:          output = LinearizeDepth(depth)
        case WorldPosition:  output = gBuffer2.xyz / worldScale
        case Lighting:       output = lighting_output / max(albedo, epsilon)
        case Shadows:        output = shadow_factor
        case Wireframe:      output = edge_detection(screen_pos)
        case Overview:       output = composite_grid(all channels)
    }
    debug_output_rt = output
} else {
    debug_output_rt = lighting_output
}

// Continue with tonemapping
final_hdr = Tonemap(debug_output_rt)
```

---

## Implementation Details

### 1. Enum Definition

**File**: `runtime/engine/renderer/core/gbuffer/debug_view_mode.h`

```cpp
/// Debug visualization mode for rendering pipeline inspection.
///
/// Each mode visualizes a different aspect of the rendering state:
/// - Lit: Normal PBR rendering (no visualization, default)
/// - Unlit: Albedo only, no lighting
/// - Albedo through Roughness: Individual material parameter visualization
/// - Depth/WorldPosition: Geometric data
/// - Lighting/Shadows: Lighting pipeline components
/// - Wireframe: Geometry silhouettes
/// - Overview: Composite grid of all major channels
enum class DebugViewMode : uint8_t {
    /// Normal PBR rendering (no debug visualization).
    Lit = 0,

    /// Albedo color (G-Buffer RT0.rgb), no lighting applied.
    Albedo = 1,

    /// World-space surface normals, decoded from octahedral (G-Buffer RT1.rg).
    WorldNormal = 2,

    /// Metallic parameter (G-Buffer RT1.b), visualized as grayscale.
    Metallic = 3,

    /// Roughness parameter (G-Buffer RT1.a), visualized as grayscale.
    Roughness = 4,

    /// Ambient occlusion (G-Buffer RT0.a), visualized as grayscale.
    AO = 5,

    /// Linearized depth from depth buffer, white=far, black=near.
    Depth = 6,

    /// Absolute world position (G-Buffer RT2.xyz), false-color visualization.
    WorldPosition = 7,

    /// Direct lighting contribution (Lo), without albedo multiply.
    Lighting = 8,

    /// Shadow factor (0=shadowed, 1=lit), visualized as grayscale.
    Shadows = 9,

    /// Albedo only, equivalent to Unlit (alternate name).
    Unlit = 10,

    /// Wireframe overlay showing geometry edges.
    Wireframe = 11,

    /// Composite grid overview of 8+ visualization channels with labels.
    Overview = 12,

    // ──────────────────────────────────────────────────────────────────
    // Future Phase 2.5+ modes (not implemented Phase 2):
    // - Overdraw (needs forward pass instrumentation)
    // - LightComplexity (needs light list per pixel)
    // - MotionVectors (needs TAA velocity buffer)
    // - SSAO (needs AO pass completion)
    // - NaniteTriangles (future, after Nanite implementation)
};

/// String representation of debug view mode (for logging, UI, parsing).
/// Returns "Unknown" if mode is not recognized.
const char* DebugViewModeToString(DebugViewMode mode) noexcept;

/// Parse debug view mode from string (e.g., "albedo" → DebugViewMode::Albedo).
/// Case-insensitive. Returns false if mode name is not recognized.
bool TryParseDebugViewMode(std::string_view name, DebugViewMode& outMode) noexcept;

/// List all available debug view mode names (comma-separated string).
/// Useful for help text and error messages.
std::string_view GetDebugViewModeList() noexcept;
```

### 2. Shader Implementation

**File**: `runtime/shaders/runtime/debug/debug_view_ps.hlsl`

```hlsl
/// Debug view post-process shader.
/// Reads G-Buffer textures and outputs selected visualization channel.
/// No recompilation needed — mode selected via constant buffer.

#pragma once
#include "../common.hlsli"
#include "../decode_octahedral.hlsli"
#include "../linearize_depth.hlsli"

/// Debug mode constant buffer (synchronized with C++ DebugViewMode enum).
cbuffer DebugViewCB : register(b15) {
    uint debugMode;  // DebugViewMode enum value (0-12)
    uint3 _pad;
};

/// G-Buffer SRVs (populated by caller before execute).
Texture2D GBuffer0     : register(t0);  // RGBA8: Albedo.rgb | AO.a
Texture2D GBuffer1     : register(t1);  // RGBA8: Encoded Normal.rg | Metallic.b | Roughness.a
Texture2D GBuffer2     : register(t2);  // RGBA16F: World Position.xyz
Texture2D DepthBuffer  : register(t3);  // D32_FLOAT: Scene depth
Texture2D LightingBuffer : register(t4); // RGBA16F: HDR lighting (Lo)

/// Common sampler for texture reads (point or bilinear, caller determines).
SamplerState PointSampler : register(s0);

/// Vertex shader — simple fullscreen quad (position + UV passthrough).
struct VS_Input {
    uint vertexID : SV_VertexID;
};

struct VS_Output {
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

/// Fullscreen quad vertex shader (generates quad from SV_VertexID).
///
/// Vertex layout:
/// 0,1: top-left, top-right
/// 2,4: bottom-left, bottom-right (two triangles: 0,1,2 and 1,3,2)
VS_Output main_vs(VS_Input input) {
    VS_Output output;

    // Generate quad position from vertex ID (0-5 for two triangles)
    uint vertexID = input.vertexID;
    float x = float((vertexID & 1) << 1) - 1.0;      // 0 or 2 → -1 or 1
    float y = float((vertexID & 2)) - 1.0;           // 0 or 2 → -1 or 1
    output.position = float4(x, -y, 0.0, 1.0);       // -Y for top-down UV
    output.uv = float2(x * 0.5 + 0.5, -y * 0.5 + 0.5); // [0,1] UV

    return output;
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode implementations — each outputs float4(rgb, 1.0) in sRGB or linear space
// ─────────────────────────────────────────────────────────────────────────────

/// Mode 1: Albedo visualization (G-Buffer RT0.rgb, no lighting).
float4 VisualizeAlbedo(float2 uv) {
    float3 albedo = GBuffer0.Sample(PointSampler, uv).rgb;
    return float4(albedo, 1.0);
}

/// Mode 2: World normal visualization (octahedral decoded, mapped to RGB).
/// Output: R = right (X), G = forward (Y), B = up (Z), all in [0, 1].
float4 VisualizeNormal(float2 uv) {
    float2 encoded = GBuffer1.Sample(PointSampler, uv).rg;
    float3 normal = DecodeOctahedral(encoded);
    // Remap from [-1, 1] to [0, 1]
    float3 visualized = normal * 0.5 + 0.5;
    return float4(visualized, 1.0);
}

/// Mode 3: Metallic visualization (G-Buffer RT1.b as grayscale).
float4 VisualizeMetallic(float2 uv) {
    float metallic = GBuffer1.Sample(PointSampler, uv).b;
    return float4(metallic.xxx, 1.0);
}

/// Mode 4: Roughness visualization (G-Buffer RT1.a as grayscale).
/// Note: Roughness is stored with limited precision; apply visualization curve if needed.
float4 VisualizeRoughness(float2 uv) {
    float roughness = GBuffer1.Sample(PointSampler, uv).a;
    return float4(roughness.xxx, 1.0);
}

/// Mode 5: Ambient occlusion visualization (G-Buffer RT0.a as grayscale).
float4 VisualizeAO(float2 uv) {
    float ao = GBuffer0.Sample(PointSampler, uv).a;
    return float4(ao.xxx, 1.0);
}

/// Mode 6: Linearized depth visualization (black=near, white=far).
float4 VisualizeDepth(float2 uv) {
    float depth = DepthBuffer.Sample(PointSampler, uv).r;
    float linearDepth = LinearizeDepth(depth);
    return float4(linearDepth.xxx, 1.0);
}

/// Mode 7: World position visualization (false color).
/// Divides XYZ by world scale (100.0 for cm-to-world) and maps to [0, 1].
float4 VisualizeWorldPosition(float2 uv) {
    float3 worldPos = GBuffer2.Sample(PointSampler, uv).rgb;
    // Normalize by world scale (adjust as needed for your coordinate system)
    const float worldScale = 100.0; // 1 unit = 1cm
    float3 scaled = frac(worldPos / (worldScale * 10.0)); // Show 10m × 10m grid
    return float4(scaled, 1.0);
}

/// Mode 8: Lighting contribution (Lo without albedo).
/// Divides lighting buffer by albedo to isolate lighting contribution.
float4 VisualizeLighting(float2 uv) {
    float3 lighting = LightingBuffer.Sample(PointSampler, uv).rgb;
    float3 albedo = GBuffer0.Sample(PointSampler, uv).rgb;
    // Avoid division by zero
    albedo = max(albedo, 0.01);
    float3 Lo = lighting / albedo;
    // Tone down for visualization (prevent overexposure)
    Lo = Lo / (1.0 + Lo); // Reinhard tone map for display
    return float4(Lo, 1.0);
}

/// Mode 9: Shadow factor visualization (0=shadowed red, 1=lit white).
/// Computes approximate shadow factor from lighting vs. unlit albedo.
float4 VisualizeShadows(float2 uv) {
    float3 lighting = LightingBuffer.Sample(PointSampler, uv).rgb;
    float3 albedo = GBuffer0.Sample(PointSampler, uv).rgb;
    float3 expectedUnlit = albedo; // Expected lighting if fully lit
    // Rough shadow factor: lighting intensity / unlit intensity
    float shadowFactor = dot(lighting, float3(0.299, 0.587, 0.114)) /
                        (dot(expectedUnlit, float3(0.299, 0.587, 0.114)) + 0.01);
    shadowFactor = saturate(shadowFactor);
    return float4(shadowFactor.xxx, 1.0);
}

/// Mode 10: Unlit (alias for Albedo).
float4 VisualizeUnlit(float2 uv) {
    return VisualizeAlbedo(uv);
}

/// Mode 11: Wireframe overlay (edge detection via screen-space derivatives).
/// Uses ddx/ddy on screen position to detect edges.
float4 VisualizeWireframe(float2 uv) {
    // Edge detection via depth discontinuity
    float depth = DepthBuffer.Sample(PointSampler, uv).r;
    float depthDx = abs(ddx(depth));
    float depthDy = abs(ddy(depth));
    float edgeStrength = max(depthDx, depthDy);

    // Threshold edge detection
    const float edgeThreshold = 0.01;
    float edge = step(edgeThreshold, edgeStrength);

    // Output: white wireframe on black background
    return float4(edge.xxx, 1.0);
}

/// Mode 12: Overview composite grid (8 or 12 channels in grid layout).
/// Layout: 4×2 grid (Albedo, Normal, Metallic, Roughness, AO, Depth, Lighting, Shadows)
float4 VisualizeOverview(float2 uv) {
    // Grid layout: 4 columns, 2 rows
    const uint gridWidth = 4;
    const uint gridHeight = 2;

    // Determine cell index
    uint cellX = uint(uv.x * gridWidth);
    uint cellY = uint(uv.y * gridHeight);
    uint cellIndex = cellY * gridWidth + cellX;

    // Local UV within cell [0, 1]
    float2 cellUV = frac(uv * float2(gridWidth, gridHeight));

    // Sample appropriate channel based on cell index
    float4 cellOutput;
    switch (cellIndex) {
        case 0:  cellOutput = VisualizeAlbedo(cellUV); break;      // Albedo
        case 1:  cellOutput = VisualizeNormal(cellUV); break;      // Normal
        case 2:  cellOutput = VisualizeMetallic(cellUV); break;    // Metallic
        case 3:  cellOutput = VisualizeRoughness(cellUV); break;   // Roughness
        case 4:  cellOutput = VisualizeAO(cellUV); break;          // AO
        case 5:  cellOutput = VisualizeDepth(cellUV); break;       // Depth
        case 6:  cellOutput = VisualizeLighting(cellUV); break;    // Lighting
        case 7:  cellOutput = VisualizeShadows(cellUV); break;     // Shadows
        default: cellOutput = float4(1, 0, 1, 1); break;           // Magenta (unknown cell)
    }

    // Add cell border for clarity
    const float borderWidth = 2.0; // pixels
    float2 cellPixelUV = cellUV * float2(1280, 720) / float2(gridWidth, gridHeight); // Approximate pixel coords
    bool onBorder = (cellPixelUV.x < borderWidth || cellPixelUV.x > (1280.0/gridWidth - borderWidth) ||
                     cellPixelUV.y < borderWidth || cellPixelUV.y > (720.0/gridHeight - borderWidth));
    if (onBorder) {
        cellOutput = float4(1, 1, 1, 1); // White border
    }

    // TODO: Render text labels over cells (requires font texture atlas or computed text)
    // For now, rely on cell order and visual context

    return cellOutput;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main pixel shader — dispatcher to mode-specific functions
// ─────────────────────────────────────────────────────────────────────────────

/// Main pixel shader — dispatch to visualization mode.
float4 main_ps(VS_Output input) : SV_TARGET {
    float2 uv = input.uv;

    // Dispatch to appropriate visualization function
    switch (debugMode) {
        case 0:  return float4(LightingBuffer.Sample(PointSampler, uv).rgb, 1.0); // Lit (pass-through)
        case 1:  return VisualizeAlbedo(uv);
        case 2:  return VisualizeNormal(uv);
        case 3:  return VisualizeMetallic(uv);
        case 4:  return VisualizeRoughness(uv);
        case 5:  return VisualizeAO(uv);
        case 6:  return VisualizeDepth(uv);
        case 7:  return VisualizeWorldPosition(uv);
        case 8:  return VisualizeLighting(uv);
        case 9:  return VisualizeShadows(uv);
        case 10: return VisualizeUnlit(uv);
        case 11: return VisualizeWireframe(uv);
        case 12: return VisualizeOverview(uv);
        default: return float4(1, 0, 1, 1); // Magenta error
    }
}
```

### 3. Render Pass Class

**File**: `runtime/engine/renderer/passes/debug_view_pass.h`

```cpp
#pragma once

#include "../../rhi/rhi_types.h"
#include "../core/gbuffer/debug_view_mode.h"

/// Render pass for debug view mode visualization.
/// Inputs: G-Buffer textures (GBuffer0, GBuffer1, GBuffer2, Depth, LightingBuffer)
/// Output: DebugOutputRT (temporary, reused post-process intermediate)
class DebugViewPass {
public:
    /// Descriptor for debug view pass inputs/outputs.
    struct Descriptor {
        DebugViewMode debugMode;
        RhiDescriptor gBuffer0Srv;
        RhiDescriptor gBuffer1Srv;
        RhiDescriptor gBuffer2Srv;
        RhiDescriptor depthBufferSrv;
        RhiDescriptor lightingBufferSrv;
        RhiDescriptor outputRtv;
    };

    /// Construct debug view pass.
    /// Loads and compiles `debug_view_ps.hlsl`, creates PSO.
    explicit DebugViewPass(class RhiDevice* device, class PipelineLibrary* pipelineLibrary);

    ~DebugViewPass() noexcept;

    /// Execute the debug view pass (fullscreen quad dispatch).
    /// Binds inputs, sets constant buffer with debugMode, draws full-screen quad.
    void Execute(class RhiCommandList* cmdList, const Descriptor& desc) noexcept;

private:
    RhiDevice* m_device = nullptr;
    PipelineLibrary* m_pipelineLibrary = nullptr;
    RhiPipelineState m_pso;
    RhiRootSignature m_rootSignature;
};
```

**File**: `runtime/engine/renderer/passes/debug_view_pass.cpp`

```cpp
#include "debug_view_pass.h"
#include "../rhi/rhi_device.h"
#include "../rhi/rhi_command_list.h"
#include "../core/pipeline_library.h"
#include "../core/shader_pipeline.h"

DebugViewPass::DebugViewPass(RhiDevice* device, PipelineLibrary* pipelineLibrary)
    : m_device(device), m_pipelineLibrary(pipelineLibrary) {
    UZ_CHECK(device != nullptr);
    UZ_CHECK(pipelineLibrary != nullptr);

    // Load debug view shaders from shader pipeline
    auto& shaderPipeline = ShaderPipeline::Get();

    // Compile vertex and pixel shaders
    RhiShaderBytecode vertexShaderBytecode = shaderPipeline.CompileShader(
        "runtime/shaders/runtime/debug/debug_view_ps.hlsl",
        ShaderType::Vertex,
        "main_vs"
    );
    RhiShaderBytecode pixelShaderBytecode = shaderPipeline.CompileShader(
        "runtime/shaders/runtime/debug/debug_view_ps.hlsl",
        ShaderType::Pixel,
        "main_ps"
    );

    // Create root signature (2 SRV tables + 1 constant buffer)
    RhiRootSignatureDesc rootSigDesc{};
    rootSigDesc.flags = RhiRootSignatureFlag::AllowInputAssemblerInputLayout;
    // Register(b15) for DebugViewCB
    rootSigDesc.parameters.push_back({
        .type = RhiRootParameterType::Descriptor32BitConstant,
        .shaderRegister = 15,
        .registerSpace = 0
    });
    // Register(t0-t4) for G-Buffer SRVs (descriptor table)
    rootSigDesc.parameters.push_back({
        .type = RhiRootParameterType::DescriptorTable,
        .descriptorTable = {{RhiDescriptorRangeType::SRV, 5, 0}} // t0-t4
    });

    m_rootSignature = device->CreateRootSignature(rootSigDesc);

    // Create PSO
    RhiGraphicsPipelineDesc psoDesc{};
    psoDesc.rootSignature = m_rootSignature;
    psoDesc.vertexShader = vertexShaderBytecode;
    psoDesc.pixelShader = pixelShaderBytecode;
    psoDesc.renderTargetFormat = RhiFormat::RGBA16F; // Or match your post-process RT format
    psoDesc.depthTestEnable = false;
    psoDesc.depthWriteEnable = false;
    psoDesc.blendMode = RhiBlendMode::Opaque;

    m_pso = device->CreateGraphicsPipeline(psoDesc);

    UZ_LOG(Renderer, Info, "DebugViewPass initialized");
}

DebugViewPass::~DebugViewPass() noexcept {
    // PSO and root signature cleaned up by RhiDevice
}

void DebugViewPass::Execute(RhiCommandList* cmdList, const Descriptor& desc) noexcept {
    UZ_PROFILE_FUNCTION();

    if (desc.debugMode == DebugViewMode::Lit) {
        // No-op for Lit mode (use lighting buffer directly)
        return;
    }

    cmdList->SetPipelineState(m_pso);
    cmdList->SetRootSignature(m_rootSignature);

    // Set constant buffer with debug mode
    struct DebugViewCB {
        uint32_t debugMode;
        uint32_t pad[3];
    } cb{.debugMode = static_cast<uint32_t>(desc.debugMode)};
    cmdList->SetConstantBuffer(0, &cb, sizeof(cb));

    // Bind G-Buffer SRVs
    cmdList->SetDescriptorTable(1, {
        desc.gBuffer0Srv,
        desc.gBuffer1Srv,
        desc.gBuffer2Srv,
        desc.depthBufferSrv,
        desc.lightingBufferSrv
    });

    // Bind output RTV
    cmdList->SetRenderTarget(desc.outputRtv);
    cmdList->ClearRenderTarget(desc.outputRtv, {0, 0, 0, 1});

    // Draw fullscreen quad (6 indices = 2 triangles)
    cmdList->DrawInstanced(6, 1, 0, 0);

    UZ_LOG(Renderer, Verbose, "DebugViewPass executed: mode={}", static_cast<int>(desc.debugMode));
}
```

---

## Integration Points

### 1. SceneRenderer Integration

```cpp
// In scene_renderer.h
class SceneRenderer {
private:
    std::unique_ptr<DebugViewPass> m_debugViewPass;
    DebugViewMode m_debugViewMode = DebugViewMode::Lit;

public:
    /// Set current debug view mode.
    void SetDebugViewMode(DebugViewMode mode) noexcept {
        m_debugViewMode = mode;
        UZ_LOG(Renderer, Info, "Debug view mode: {}", DebugViewModeToString(mode));
    }

    /// Get current debug view mode.
    DebugViewMode GetDebugViewMode() const noexcept { return m_debugViewMode; }
};

// In scene/scene_render_frame.cpp::Render()
void SceneRenderer::Render(RenderContext& ctx) {
    // ... geometry pass, lighting pass ...

    // Insert debug view pass if not in Lit mode
    if (m_debugViewMode != DebugViewMode::Lit) {
        DebugViewPass::Descriptor debugDesc{};
        debugDesc.debugMode = m_debugViewMode;
        debugDesc.gBuffer0Srv = m_gBuffer0Srv;
        debugDesc.gBuffer1Srv = m_gBuffer1Srv;
        debugDesc.gBuffer2Srv = m_gBuffer2Srv;
        debugDesc.depthBufferSrv = m_depthBufferSrv;
        debugDesc.lightingBufferSrv = m_lightingBufferSrv;
        debugDesc.outputRtv = m_postProcessIntermediateRtv;

        m_debugViewPass->Execute(m_commandList, debugDesc);

        // Update input to tonemapping (use debug output instead of lighting buffer)
        m_lastPassOutputRtv = m_postProcessIntermediateRtv;
    }

    // Continue with tonemapping, bloom, etc. using m_lastPassOutputRtv
    // ...
}
```

### 2. Keyboard Input Integration

```cpp
// In demo_application.cpp or pbr_test_demo.cpp::OnUpdate()
void DemoApplication::OnUpdate() {
    // ... existing update logic ...

    // Debug view mode keyboard shortcuts
    if (InputSystem::IsKeyPressed(Key::F1)) {
        m_sceneRenderer->SetDebugViewMode(DebugViewMode::Lit);
    }
    if (InputSystem::IsKeyPressed(Key::F2)) {
        m_sceneRenderer->SetDebugViewMode(DebugViewMode::Albedo);
    }
    // ... F3-F11 ...

    // Update overlay text
    m_debugViewModeText = std::string("Debug View: ") +
                          DebugViewModeToString(m_sceneRenderer->GetDebugViewMode());
}
```

### 3. CLI Argument Integration

```cpp
// In demo_application.cpp
bool DemoApplication::ParseCommandLine(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--view-mode") == 0 && i + 1 < argc) {
            if (!TryParseDebugViewMode(argv[++i], m_initialDebugViewMode)) {
                UZ_LOG(App, Warning, "Invalid view mode: {}", argv[i]);
                PrintUsage();
                return false;
            }
        }
    }
    return true;
}

// In DemoApplication::Init()
void DemoApplication::Init() {
    // ... create SceneRenderer ...
    m_sceneRenderer->SetDebugViewMode(m_initialDebugViewMode);
}
```

### 4. Lua API Integration (IMPLEMENTED)

**File**: `runtime/engine/scripting/api/renderer/lua_renderer_api.cpp`

The Lua debug API is registered in `LuaRendererAPI::Register()` and exposed as `ctx.debug` namespace:

```lua
-- Set debug view mode (string name, case-insensitive)
ctx.debug:set_view_mode("albedo")   -- modes: lit, albedo, worldnormal, metallic, roughness, ao, depth, worldposition, lighting, shadows, unlit, wireframe, overview

-- Get current debug view mode as string
local mode = ctx.debug:get_view_mode()  -- returns string like "albedo" or "lit"
```

**Implementation** — C++ bindings in `lua_renderer_api.cpp`:

```cpp
// In LuaRendererAPI::Register()
lua_newtable(L);  // ctx.debug table

// ctx.debug.set_view_mode(mode_name: string) → bool
lua_pushcfunction(L, [](lua_State* L) -> int {
    const char* modeName = luaL_checkstring(L, 1);
    DebugViewMode mode;
    if (TryParseDebugViewMode(modeName, mode)) {
        RendererSetDebugView(mode);  // Delegates to SceneRenderer::SetDebugViewMode()
        lua_pushboolean(L, true);
    } else {
        lua_pushboolean(L, false);  // Invalid mode name
    }
    return 1;
});
lua_setfield(L, -2, "set_view_mode");

// ctx.debug.get_view_mode() → string
lua_pushcfunction(L, [](lua_State* L) -> int {
    DebugViewMode mode = RendererGetDebugView();  // Delegates to SceneRenderer::GetDebugViewMode()
    lua_pushstring(L, DebugViewModeToString(mode));
    return 1;
});
lua_setfield(L, -2, "get_view_mode");

lua_setfield(L, -2, "debug");  // Register as ctx.debug
```

**Delegation**: Bindings delegate to existing C++ functions `RendererSetDebugView()` and `RendererGetDebugView()`, which call `SceneRenderer::SetDebugViewMode()` / `GetDebugViewMode()`. No SceneRenderer-specific details leak into Lua API.

**Documentation**: See [Scripting — Lua API Reference](../../engine/15-scripting/06-lua-api.md) § Debug Visualization for full Lua usage examples and available modes.

---

## Debug View Modes Reference

| Mode | ID | Input | Output Format | Description |
|------|----|----|------|---|
| **Lit** | 0 | LightingBuffer | RGB HDR | Normal PBR rendering (no visualization) |
| **Albedo** | 1 | GBuffer0.rgb | RGB [0,1] sRGB | Base color texture (no lighting) |
| **WorldNormal** | 2 | GBuffer1.rg (encoded) | RGB [0,1] | Decoded world-space normals (R=X, G=Y, B=Z) |
| **Metallic** | 3 | GBuffer1.b | Grayscale [0,1] | Metallic parameter (0=nonmetal, 1=metal) |
| **Roughness** | 4 | GBuffer1.a | Grayscale [0,1] | Roughness parameter (0=smooth, 1=rough) |
| **AO** | 5 | GBuffer0.a | Grayscale [0,1] | Ambient occlusion (if available, else white) |
| **Depth** | 6 | DepthBuffer | Grayscale [0,1] | Linearized depth (0=near, 1=far) |
| **WorldPosition** | 7 | GBuffer2.xyz | RGB false-color | Absolute world coordinates (scaled/normalized) |
| **Lighting** | 8 | LightingBuffer / Albedo | RGB HDR | Direct lighting (Lo) without albedo multiply |
| **Shadows** | 9 | LightingBuffer / Depth | Grayscale [0,1] | Shadow factor (0=shadowed, 1=lit) |
| **Unlit** | 10 | GBuffer0.rgb | RGB [0,1] sRGB | Alias for Albedo mode |
| **Wireframe** | 11 | DepthBuffer | Binary (edge detection) | White geometry edges on black background |
| **Overview** | 12 | All G-Buffers | 4×2 grid composite | Composite visualization of 8 major channels |

---

## Keyboard Shortcuts

| Key | Mode | Notes |
|-----|------|-------|
| `F1` | Lit | Default, normal rendering |
| `F2` | Albedo | Material base color |
| `F3` | WorldNormal | Surface normals |
| `F4` | Metallic | Metallic parameter |
| `F5` | Roughness | Roughness parameter |
| `F6` | Depth | Scene depth |
| `F7` | Shadows | Shadow factors |
| `F8` | Lighting | Lighting without albedo |
| `F9` | Overview | Composite grid of all channels |
| `F10` | Wireframe | Geometry silhouettes |
| `F11` | Unlit | Albedo only (alternative to F2) |

---

## Performance Considerations

### Overhead Analysis

- **Debug view pass**: Single fullscreen quad, ~0.2–0.5ms on modern GPU (RTX 3080+)
  - No additional texture uploads or copies
  - No change to geometry culling or draw calls
  - Shader branch cost is negligible (single cbuffer flag)

- **Keyboard input handling**: <0.1ms (simple key polling)

- **Lua API calls**: <0.1ms (function lookup + mode string parsing)

- **Overlay text rendering**: ~0.1–0.2ms (existing HUD text renderer)

**Total overhead**: <1ms on target hardware (DX12 with modern GPU)

**Optimization notes**:
- If debugMode == Lit, pass is skipped entirely (zero cost)
- G-Buffer reads are cached (same textures used by lighting pass)
- No additional memory allocations (constant buffer is stack-allocated)
- No shader recompilation needed for mode switches (use branching instead)

---

## Future Phases (Phase 2.5+)

### Phase 2.5 — Advanced Visualizations
- **Overdraw**: Pixel overdraw counter (requires forward pass + UAV accumulation)
- **LightComplexity**: Number of lights affecting each pixel (requires light list generation)
- **MotionVectors**: Screen-space velocity visualization (requires TAA velocity buffer)
- **SSAO**: Ambient occlusion before blending (requires SSAO pass completion)
- **NaniteTriangles**: Nanite cluster visualization (requires Nanite implementation)

### Phase 3 — UI Integration
- **Inspector panel**: Real-time mode selection via dropdown (VSCode/editor UI)
- **Grid label rendering**: Proper text labels in Overview grid (font atlas or compute text)
- **Custom color grading**: Apply false-color palettes to distance/complexity visualizations
- **Comparison mode**: Side-by-side before/after (e.g., with/without shadows)

### Phase 4 — Performance Profiling
- **GPU profiling integration**: Wire debug pass timing into Tracy GPU zones
- **Stat counters**: Track G-Buffer miss rates, cache efficiency per channel
- **Memory usage per mode**: Measure VRAM read bandwidth per visualization mode

---

## File Structure

```
runtime/
├── shaders/
│   └── runtime/
│       ├── debug/
│       │   └── debug_view_ps.hlsl          (new)
│       ├── common.hlsli
│       ├── decode_octahedral.hlsli         (existing)
│       └── linearize_depth.hlsli           (existing or new)
└── engine/
    ├── renderer/
    │   ├── core/
    │   │   ├── debug_view_mode.h           (new)
    │   │   ├── scene_renderer.h            (modified)
    │   │   ├── scene/scene_render_frame.cpp          (modified)
    │   │   └── pipeline_library.h/cpp      (modified)
    │   └── passes/
    │       ├── debug_view_pass.h           (new)
    │       └── debug_view_pass.cpp         (new)
    ├── app/
    │   ├── demo_application.h              (modified)
    │   └── demo_application.cpp            (modified)
    └── scripting/
        └── lua_vm.cpp                      (modified)

docs/
├── specs/
│   └── rendering/
│       └── debug-view-modes.md             (new, this file)
└── engine/
    └── 1.3-renderer.md                     (modified, add 1.3.4 section)
```

---

## Testing Strategy

### Unit Tests (if applicable)
- Test `DebugViewModeToString()` for all 13 modes
- Test `TryParseDebugViewMode()` with valid/invalid inputs (case-insensitive)
- Test enum value ranges (0–12, error on 13+)

### Visual Tests (mandatory)
- Screenshot each mode with standardized test scene (see Rulebook task section 9.1–9.7)
- Compare against UE5/Godot visualizations for correctness
- Verify no visual artifacts (texture coordinate wrapping, color banding, etc.)

### Integration Tests
- Keyboard shortcuts (F1–F11) trigger correct mode changes
- CLI argument `--view-mode <name>` sets initial mode
- Lua API calls `ctx.debug.set_view_mode()` work in scripts
- Mode persists across frame updates
- Mode switches don't cause crashes or rendering artifacts

### Performance Tests
- Measure frame time per mode
- Verify overhead < 1ms on target GPU
- Verify no frame stuttering or hitches

---

## Related Docs

- [Spec 1.3 — Renderer](../1.3-renderer.md) — Deferred rendering pipeline, G-Buffer layout
- [Spec 1.4 — Scene System](../1.4-scene-system.md) — Scene and component hierarchy
- [Lua Scripting API](../../engine/lua-api-reference.md) — Lua bindings and ctx namespace
- [Shader Library](../../engine/shader-library.md) — HLSL utilities and includes
- [Editor Workflow Guide](../../guides/editor-workflow.md) — Using debug views during development

---

## Changelog

### Added
- Debug View Modes system with 13 visualization modes
- Single `debug_view_ps.hlsl` shader with runtime mode branching
- `DebugViewPass` render pass for fullscreen visualization
- Keyboard shortcuts (F1–F11) for rapid mode switching
- CLI argument `--view-mode <name>` for headless testing
- Lua API `ctx.debug.set_view_mode(name)` and `ctx.debug.get_view_mode()`
- Text overlay showing current debug mode name
- Overview grid mode (composite visualization of 8+ channels)

### Performance
- Debug pass overhead: < 0.5ms on modern GPU
- No performance cost when mode is set to Lit (pass skipped)
- No shader recompilation on mode switch

### Compatibility
- DX12 and DX11 backends supported (via ShaderPipeline model negotiation)
- Works with all existing render passes (non-invasive insertion)
- No breaking changes to public APIs
