/// Camera Test Demo Implementation — Cross-Platform (Windows + macOS)
///
/// Renders a colored cube in 3D with FPS camera controls.
/// WASD to move, right-click + mouse to look, Shift to sprint, Space/Ctrl up/down.
/// Uses only abstract RHI interfaces — no DX12, Vulkan, or Metal types.
///
/// On Windows: loads HLSL shaders via ShaderPipeline + ShaderCompiler/FXC.
/// On macOS:   embeds MSL source strings and passes them directly to CreateShader().

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "camera_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "input.h"
#include "log.h"
#include "vertex.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "paths.h"

#include "upload_context.h"
#include "stats_overlay.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#endif

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Embedded MSL Shaders (macOS only)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__APPLE__)

/// MSL vertex shader source — transforms position by viewProj, passes color through.
///
/// The engine stores matrices in row-major order (DirectXMath convention).
/// MSL float4x4 is column-major, so transpose is required before multiply.
/// PerFrameCB is bound at buffer(1), matching HLSL register(b1).
static const char kCameraVSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(0)]];
    float4 color    [[attribute(1)]];
    float2 texcoord [[attribute(2)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

struct PerFrameCB {
    float4x4 viewProj;
    float4   cameraPos;
    float4   timeParams;
};

vertex VertexOut vertex_main(VertexIn in [[stage_in]],
                             constant PerFrameCB& perFrame [[buffer(1)]]) {
    // Row-major bytes (DirectXMath) read as column-major in MSL = transpose(M).
    // HLSL mul(v, M) == transpose(M) * v == MSL_M * v — no transpose needed.
    VertexOut out;
    out.position = perFrame.viewProj * float4(in.position, 1.0);
    out.color    = in.color;
    out.texcoord = in.texcoord;
    return out;
}
)msl";

/// MSL pixel shader source — returns interpolated vertex color.
static const char kCameraPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct FragmentIn {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

fragment float4 fragment_main(FragmentIn in [[stage_in]]) {
    return in.color;
}
)msl";

/// MSL grid vertex shader — fullscreen triangle from vertex_id (no vertex buffer).
///
/// Generates a triangle that covers the entire screen using the vertex ID trick:
/// 3 vertices produce NDC coords that span [-1,1] on both axes.
static const char kGridVSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct GridVSOut {
    float4 position [[position]];
    float2 ndc;
};

vertex GridVSOut grid_vertex_main(uint vid [[vertex_id]]) {
    float2 uv = float2((vid << 1) & 2, vid & 2);
    GridVSOut out;
    out.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    out.ndc = float2(out.position.x, out.position.y);
    return out;
}
)msl";

/// MSL grid pixel shader — ray-plane intersection with procedural anti-aliased grid lines.
///
/// Reconstructs a world-space ray from NDC, intersects with Z=0 ground plane,
/// and draws minor/major grid lines with axis highlights and distance fade.
///
/// DirectXMath stores matrices row-major. MSL float4x4 is column-major,
/// so we transpose before multiplying: transpose(M) * v == HLSL mul(v, M).
static const char kGridPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct GridCB {
    float4x4 invViewProj;
    float4x4 viewProj;
    float4   cameraPos;
    float4   gridParams;
};

struct GridVSOut {
    float4 position [[position]];
    float2 ndc;
};

struct GridPSOut {
    float4 color [[color(0)]];
    float  depth [[depth(any)]];
};

float GridLine(float coord, float spacing) {
    float wrapped = fract(coord / spacing + 0.5) - 0.5;
    float lineDist = abs(wrapped) * spacing;
    float derivative = fwidth(coord);
    return 1.0 - smoothstep(0.0, derivative * 1.5, lineDist);
}

fragment GridPSOut grid_fragment_main(GridVSOut in [[stage_in]],
                                      constant GridCB& gridCb [[buffer(1)]]) {
    float minorSpacing  = gridCb.gridParams.x;
    float majorInterval = gridCb.gridParams.y;
    float fadeDistance   = gridCb.gridParams.z;
    float majorSpacing  = minorSpacing * majorInterval;

    // Row-major bytes (DirectXMath) read as column-major in MSL = transpose(M).
    // HLSL mul(v, M) == transpose(M) * v == MSL_M * v — no transpose needed.
    float4 nearNDC = float4(in.ndc, 0.0, 1.0);
    float4 farNDC  = float4(in.ndc, 1.0, 1.0);

    float4 nearWorld = gridCb.invViewProj * nearNDC;
    float4 farWorld  = gridCb.invViewProj * farNDC;
    nearWorld /= nearWorld.w;
    farWorld  /= farWorld.w;

    // Intersect ray with Z=0 ground plane
    float3 rayOrigin = nearWorld.xyz;
    float3 rayDir    = farWorld.xyz - nearWorld.xyz;
    float t = -rayOrigin.z / rayDir.z;

    if (t < 0.0) {
        discard_fragment();
    }

    float3 worldPos = rayOrigin + rayDir * t;

    // Grid lines on XY plane (Z-up ground)
    float minorX = GridLine(worldPos.x, minorSpacing);
    float minorY = GridLine(worldPos.y, minorSpacing);
    float minor  = max(minorX, minorY);

    float majorX = GridLine(worldPos.x, majorSpacing);
    float majorY = GridLine(worldPos.y, majorSpacing);
    float major  = max(majorX, majorY);

    // Axis highlights — X-axis (forward) = red at Y=0, Y-axis (right) = green at X=0
    float onXAxis = (abs(worldPos.y) < fwidth(worldPos.y) * 2.0) ? 1.0 : 0.0;
    float onYAxis = (abs(worldPos.x) < fwidth(worldPos.x) * 2.0) ? 1.0 : 0.0;

    // Color and distance fade
    float dist = length(worldPos - gridCb.cameraPos.xyz);
    float fade = 1.0 - saturate(dist / fadeDistance);
    fade *= fade;

    float3 color = float3(0.3, 0.3, 0.3);
    float alpha = minor * 0.3;

    color = mix(color, float3(0.5, 0.5, 0.5), major);
    alpha = max(alpha, major * 0.5);

    color = mix(color, float3(0.8, 0.2, 0.2), onXAxis);
    color = mix(color, float3(0.2, 0.8, 0.2), onYAxis);
    alpha = max(alpha, max(onXAxis, onYAxis) * 0.8);

    alpha *= fade;

    if (alpha < 0.01) {
        discard_fragment();
    }

    // Project Z=0 hit point back to clip space for correct depth
    float4 clipPos = gridCb.viewProj * float4(worldPos, 1.0);
    float depth = clipPos.z / clipPos.w;

    GridPSOut out;
    out.color = float4(color, alpha);
    out.depth = depth;
    return out;
}
)msl";

/// MSL overlay vertex shader — screen-space 2D quads via Input Assembler.
///
/// Transforms pixel coordinates to NDC using invScreenSize from a constant buffer.
/// OverlayCB is bound at buffer(1), matching HLSL register(b1).
/// Vertex data comes via [[stage_in]] with OverlayVertex layout.
static const char kOverlayVSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct OverlayVertexIn {
    float2 position [[attribute(0)]];
    float2 texcoord [[attribute(1)]];
    float4 color    [[attribute(2)]];
};

struct OverlayVSOut {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
};

struct OverlayCB {
    float2 invScreenSize;
    float2 padding;
};

vertex OverlayVSOut overlay_vertex_main(OverlayVertexIn in [[stage_in]],
                                         constant OverlayCB& cb [[buffer(1)]]) {
    // Screen pixels -> NDC: x in [0,W] -> [-1,1], y in [0,H] -> [1,-1] (flip Y)
    float2 ndc;
    ndc.x = in.position.x * cb.invScreenSize.x * 2.0 - 1.0;
    ndc.y = 1.0 - in.position.y * cb.invScreenSize.y * 2.0;

    OverlayVSOut out;
    out.position = float4(ndc, 0.0, 1.0);
    out.texcoord = in.texcoord;
    out.color    = in.color;
    return out;
}
)msl";

/// MSL overlay text pixel shader — samples R8 font atlas for alpha.
///
/// Binds font atlas at [[texture(0)]] and sampler at [[sampler(0)]].
/// Output alpha = vertex color alpha * atlas red channel (glyph coverage).
static const char kOverlayTextPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct OverlayPSIn {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
};

fragment float4 overlay_text_fragment(OverlayPSIn in [[stage_in]],
                                      texture2d<float> fontAtlas [[texture(0)]],
                                      sampler texSampler [[sampler(0)]]) {
    float alpha = fontAtlas.sample(texSampler, in.texcoord).r;
    return float4(in.color.rgb, in.color.a * alpha);
}
)msl";

/// MSL overlay rect pixel shader — pass-through vertex color.
static const char kOverlayRectPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct OverlayPSIn {
    float4 position [[position]];
    float2 texcoord;
    float4 color;
};

fragment float4 overlay_rect_fragment(OverlayPSIn in [[stage_in]]) {
    return in.color;
}
)msl";

#endif // __APPLE__

// ─────────────────────────────────────────────────────────────────────────────
// Cube Geometry — 36 vertices (6 faces x 2 triangles x 3 verts)
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kCubeVertexCount = 36;
static constexpr float kMaxFPS = 500.f;
static constexpr float kMinFrameTime = 1.f / kMaxFPS;

/// Fills outVerts with a unit cube centered at origin. Each face has a distinct color.
///
/// # Arguments
/// * `outVerts` — Array of exactly `kCubeVertexCount` (36) vertices to fill.
///
/// # Face colors
/// +X=red, -X=cyan, +Y=green, -Y=magenta, +Z=blue, -Z=yellow
/// Cube bottom sits on Z=0, top at Z=1.
static void GetCubeVertices(SimpleVertex outVerts[kCubeVertexCount]) {
    const Vec4 red     = {1, 0, 0, 1};
    const Vec4 cyan    = {0, 1, 1, 1};
    const Vec4 green   = {0, 1, 0, 1};
    const Vec4 magenta = {1, 0, 1, 1};
    const Vec4 blue    = {0, 0, 1, 1};
    const Vec4 yellow  = {1, 1, 0, 1};

    // Half-extent; cube sits on Z=0 ground plane (bottom at Z=0, top at Z=1)
    constexpr float h = 0.5f;
    constexpr float zOff = h;
    const Vec2 uv00 = {0, 0}, uv10 = {1, 0}, uv01 = {0, 1}, uv11 = {1, 1};

    uint32_t i = 0;
    auto quad = [&](Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec4 col) {
        a.z += zOff; b.z += zOff; c.z += zOff; d.z += zOff;
        outVerts[i++] = {a, col, uv00};
        outVerts[i++] = {b, col, uv10};
        outVerts[i++] = {c, col, uv11};
        outVerts[i++] = {a, col, uv00};
        outVerts[i++] = {c, col, uv11};
        outVerts[i++] = {d, col, uv01};
    };

    // +X face (forward)
    quad({h,-h, h}, {h,-h,-h}, {h, h,-h}, {h, h, h}, red);
    // -X face (back)
    quad({-h,-h,-h}, {-h,-h, h}, {-h, h, h}, {-h, h,-h}, cyan);
    // +Y face (right)
    quad({-h, h, h}, { h, h, h}, { h, h,-h}, {-h, h,-h}, green);
    // -Y face (left)
    quad({-h,-h,-h}, { h,-h,-h}, { h,-h, h}, {-h,-h, h}, magenta);
    // +Z face (top)
    quad({-h,-h, h}, { h,-h, h}, { h, h, h}, {-h, h, h}, blue);
    // -Z face (bottom)
    quad({ h,-h,-h}, {-h,-h,-h}, {-h, h,-h}, { h, h,-h}, yellow);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main Demo
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kFramesInFlight = 3;

/// Constant buffer layout for the infinite grid shader.
/// Must match the GridCB struct in both HLSL and MSL grid shaders.
struct GridCB {
    Mat4 invViewProj;
    Mat4 viewProj;
    Vec4 cameraPos;
    Vec4 gridParams; ///< x=minorSpacing, y=majorInterval, z=fadeDistance
};

bool RunCameraTestDemo(const CameraTestConfig& config) {
    const uint32_t frameCount = config.frameCount;

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title = "UzEngine — Camera Test (WASD + RMB Look)";
    winDesc.width = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(Renderer, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Input ────────────────────────────────────────────────────────────
    InputSystem input;
    input.Init();
    window.SetInputSystem(&input);

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = config.backend;
#ifdef NDEBUG
    deviceDesc.enableValidation = false;
#else
    deviceDesc.enableValidation = true;
#endif

    if (const char* env = std::getenv("UZ_FORCE_SM5"); env && std::string(env) == "1") {
        deviceDesc.forceShaderModel = ShaderModel::SM5_0;
    }

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(Renderer, Error, "Failed to create RHI device (backend={})",
               static_cast<int>(config.backend));
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    RHISwapChainDesc scDesc{};
    scDesc.windowHandle = window.GetNativeHandle();
    scDesc.displayHandle = window.GetNativeDisplayHandle();
    scDesc.width = static_cast<uint32_t>(window.GetWidth());
    scDesc.height = static_cast<uint32_t>(window.GetHeight());
    scDesc.bufferCount = kFramesInFlight;
    scDesc.format = RHIFormat::RGBA8Unorm;
    scDesc.vsync = false;

    IRHISwapChain* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(Renderer, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command List ─────────────────────────────────────────────────────
    IRHICommandList* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(Renderer, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    // ── Fences ───────────────────────────────────────────────────────────
    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = device->CreateFence();
        if (!frameFences[i]) {
            UZ_LOG(Renderer, Error, "Failed to create frame fence {}", i);
            for (uint32_t j = 0; j < i; ++j) device->DestroyFence(frameFences[j]);
            device->DestroyCommandList(cmdList);
            device->DestroySwapChain(swapChain);
            DestroyRHIDevice(device);
            return false;
        }
    }

    // ── Shader Loading ──────────────────────────────────────────────────
    // Force non-bindless on macOS Metal — our embedded MSL shaders use
    // [[stage_in]] with [[attribute(N)]], which requires a vertex descriptor.
    // The bindless MSL path would need a StructuredBuffer<SimpleVertex>
    // accessed via argument buffers, which is not implemented yet.
#if defined(__APPLE__)
    const bool bindless = false;
#else
    const bool bindless = device->IsBindless();
#endif

#ifdef _WIN32
    // ── Windows: HLSL via ShaderPipeline ─────────────────────────────────
    ShaderCompiler compiler;
    if (!compiler.Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return false;
    }

    ShaderPipeline shaderPipeline;
    ShaderPipeline::Config pipelineCfg;
    pipelineCfg.cacheDir = (uz::GetExeDir() / "cache/shaders").string();
    pipelineCfg.targetBackend = bindless ? "dx12_sm66" : "dx12_sm50";
    pipelineCfg.bindless = bindless;
    shaderPipeline.Init(pipelineCfg);
    shaderPipeline.SetCompiler(&compiler);

    ShaderCompilerFXC compilerFXC;
    if (!bindless) {
        if (!compilerFXC.Init()) {
            UZ_LOG(Renderer, Error, "Failed to init FXC shader compiler");
            return false;
        }
        shaderPipeline.SetCompilerFXC(&compilerFXC);
    }

    std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
    shaderPipeline.Mount("/demo/camera", shadersDir);

    auto vsBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_vs.hlsl", "");
    auto psBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_ps.hlsl", "");
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load camera shaders");
        return false;
    }

    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = vsBytecode.data();
    vsDesc.bytecodeSize = vsBytecode.size();
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "main";
    vsDesc.debugName = "camera_vs";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = psBytecode.data();
    psDesc.bytecodeSize = psBytecode.size();
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "main";
    psDesc.debugName = "camera_ps";

#elif defined(__APPLE__)
    // ── macOS: embedded MSL source passed directly to CreateShader ───────
    RHIShaderDesc vsDesc{};
    vsDesc.bytecode = kCameraVSSource;
    vsDesc.bytecodeSize = std::strlen(kCameraVSSource);
    vsDesc.stage = RHIShaderStage::Vertex;
    vsDesc.entryPoint = "vertex_main";
    vsDesc.debugName = "camera_vs_msl";

    RHIShaderDesc psDesc{};
    psDesc.bytecode = kCameraPSSource;
    psDesc.bytecodeSize = std::strlen(kCameraPSSource);
    psDesc.stage = RHIShaderStage::Pixel;
    psDesc.entryPoint = "fragment_main";
    psDesc.debugName = "camera_ps_msl";
#endif

    RHIShaderHandle vsHandle = device->CreateShader(vsDesc);
    RHIShaderHandle psHandle = device->CreateShader(psDesc);

    if (!vsHandle.IsValid() || !psHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create camera shaders");
        return false;
    }

    // ── Pipeline ─────────────────────────────────────────────────────────
    // SM5 input layout: POSITION(float3) + COLOR(float4) + TEXCOORD(float2)
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 28 },
    };

    RHIGraphicsPipelineDesc pipeDesc{};
    pipeDesc.vertexShader = vsHandle;
    pipeDesc.pixelShader = psHandle;
    pipeDesc.topology = RHIPrimitiveTopology::TriangleList;
    pipeDesc.rasterizer.cullMode = RHICullMode::None;
    pipeDesc.depthStencil.depthEnable = true;
    pipeDesc.depthStencil.depthWrite = true;
    pipeDesc.renderTargetFormats[0] = swapChain->GetFormat();
    pipeDesc.renderTargetCount = 1;
    pipeDesc.depthStencilFormat = RHIFormat::D32Float;
    pipeDesc.debugName = "CameraCubePipeline";

    if (!bindless) {
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 3;
        pipeDesc.vertexStride = sizeof(SimpleVertex);
    }

    RHIPipelineHandle pipeline = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create pipeline");
        return false;
    }

    // ── Vertex Buffer ────────────────────────────────────────────────────
    SimpleVertex cubeVerts[kCubeVertexCount];
    GetCubeVertices(cubeVerts);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(cubeVerts);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "CubeVertexBuffer";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* mapped = device->MapBuffer(vbHandle);
    std::memcpy(mapped, cubeVerts, sizeof(cubeVerts));
    device->UnmapBuffer(vbHandle);

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    }

    // ── Depth Buffer ─────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width = scDesc.width;
    depthDesc.height = scDesc.height;
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";

    RHITextureHandle depthBuffer = device->CreateTexture(depthDesc);

    // ── CB Ring Allocator ────────────────────────────────────────────────
    CBRingAllocator cbRing;
    cbRing.Init(device, kFramesInFlight, 64 * 1024);

#if defined(__APPLE__)
    // ── Grid Pipeline (macOS — inline MSL, no ShaderPipeline dependency) ──
    RHIShaderDesc gridVsDesc{};
    gridVsDesc.bytecode = kGridVSSource;
    gridVsDesc.bytecodeSize = std::strlen(kGridVSSource);
    gridVsDesc.stage = RHIShaderStage::Vertex;
    gridVsDesc.entryPoint = "grid_vertex_main";
    gridVsDesc.debugName = "grid_vs_msl";

    RHIShaderDesc gridPsDesc{};
    gridPsDesc.bytecode = kGridPSSource;
    gridPsDesc.bytecodeSize = std::strlen(kGridPSSource);
    gridPsDesc.stage = RHIShaderStage::Pixel;
    gridPsDesc.entryPoint = "grid_fragment_main";
    gridPsDesc.debugName = "grid_ps_msl";

    RHIShaderHandle gridVsHandle = device->CreateShader(gridVsDesc);
    RHIShaderHandle gridPsHandle = device->CreateShader(gridPsDesc);
    if (!gridVsHandle.IsValid() || !gridPsHandle.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create grid shaders");
        return false;
    }

    RHIGraphicsPipelineDesc gridPipeDesc{};
    gridPipeDesc.vertexShader = gridVsHandle;
    gridPipeDesc.pixelShader = gridPsHandle;
    gridPipeDesc.topology = RHIPrimitiveTopology::TriangleList;
    gridPipeDesc.rasterizer.cullMode = RHICullMode::None;
    gridPipeDesc.depthStencil.depthEnable = true;
    gridPipeDesc.depthStencil.depthWrite = false;
    gridPipeDesc.depthStencil.depthFunc = RHICompareFunc::LessEqual;
    gridPipeDesc.renderTargetBlend[0].blendEnable = true;
    gridPipeDesc.renderTargetBlend[0].srcColor = RHIBlendFactor::SrcAlpha;
    gridPipeDesc.renderTargetBlend[0].dstColor = RHIBlendFactor::InvSrcAlpha;
    gridPipeDesc.renderTargetBlend[0].colorOp = RHIBlendOp::Add;
    gridPipeDesc.renderTargetBlend[0].srcAlpha = RHIBlendFactor::SrcAlpha;
    gridPipeDesc.renderTargetBlend[0].dstAlpha = RHIBlendFactor::InvSrcAlpha;
    gridPipeDesc.renderTargetBlend[0].alphaOp = RHIBlendOp::Add;
    gridPipeDesc.renderTargetFormats[0] = swapChain->GetFormat();
    gridPipeDesc.renderTargetCount = 1;
    gridPipeDesc.depthStencilFormat = RHIFormat::D32Float;
    gridPipeDesc.debugName = "GridPipeline";
    // No inputElements — fullscreen triangle generated from vertex_id

    RHIPipelineHandle gridPipeline = device->CreateGraphicsPipeline(gridPipeDesc);
    if (!gridPipeline.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create grid pipeline");
        return false;
    }
#endif // __APPLE__

#ifdef _WIN32
    // ── Editor Grid (Windows only — depends on ShaderPipeline) ───────────
    EditorGrid editorGrid;
    if (!editorGrid.Init(device, shaderPipeline, RHIFormat::RGBA8Unorm, RHIFormat::D32Float)) {
        UZ_LOG(Renderer, Error, "Failed to initialize editor grid");
        return false;
    }
#endif

    // ── Stats Overlay (cross-platform) ──────────────────────────────────
    StatsOverlay statsOverlay;
    {
        UploadContext upload(device);
        cmdList->Begin();

#if defined(__APPLE__)
        // Create MSL overlay shaders for the stats overlay
        RHIShaderDesc overlayVsDesc{};
        overlayVsDesc.bytecode = kOverlayVSSource;
        overlayVsDesc.bytecodeSize = std::strlen(kOverlayVSSource);
        overlayVsDesc.stage = RHIShaderStage::Vertex;
        overlayVsDesc.entryPoint = "overlay_vertex_main";
        overlayVsDesc.debugName = "overlay_vs_msl";

        RHIShaderDesc overlayTextPsDesc{};
        overlayTextPsDesc.bytecode = kOverlayTextPSSource;
        overlayTextPsDesc.bytecodeSize = std::strlen(kOverlayTextPSSource);
        overlayTextPsDesc.stage = RHIShaderStage::Pixel;
        overlayTextPsDesc.entryPoint = "overlay_text_fragment";
        overlayTextPsDesc.debugName = "overlay_text_ps_msl";

        RHIShaderDesc overlayRectPsDesc{};
        overlayRectPsDesc.bytecode = kOverlayRectPSSource;
        overlayRectPsDesc.bytecodeSize = std::strlen(kOverlayRectPSSource);
        overlayRectPsDesc.stage = RHIShaderStage::Pixel;
        overlayRectPsDesc.entryPoint = "overlay_rect_fragment";
        overlayRectPsDesc.debugName = "overlay_rect_ps_msl";

        RHIShaderHandle overlayVsHandle = device->CreateShader(overlayVsDesc);
        RHIShaderHandle overlayTextPsHandle = device->CreateShader(overlayTextPsDesc);
        RHIShaderHandle overlayRectPsHandle = device->CreateShader(overlayRectPsDesc);

        if (!overlayVsHandle.IsValid() || !overlayTextPsHandle.IsValid() || !overlayRectPsHandle.IsValid()) {
            UZ_LOG(Renderer, Error, "Failed to create overlay shaders");
            return false;
        }

        // macOS monospace font — Monaco is a plain .ttf available on all macOS versions
        std::string fontPath = "/System/Library/Fonts/Monaco.ttf";
        if (!statsOverlay.Init(device, cmdList, upload, fontPath,
                                overlayVsHandle, overlayTextPsHandle, overlayRectPsHandle,
                                swapChain->GetFormat(), winDesc.width, winDesc.height, bindless)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            return false;
        }
#elif defined(_WIN32)
        if (!statsOverlay.Init(device, cmdList, upload, shaderPipeline,
                               RHIFormat::RGBA8Unorm, winDesc.width, winDesc.height, bindless)) {
            UZ_LOG(Renderer, Error, "Failed to initialize stats overlay");
            return false;
        }
#endif

        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
    }

#ifdef _WIN32
    // ── Screenshot Capture (Windows only) ────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        if (!screenshotCapture.Init(device, scDesc.width, scDesc.height)) {
            UZ_LOG(Renderer, Error, "Failed to initialize screenshot capture");
            return false;
        }
    }
#endif // _WIN32

    // ── Render Graph Resource Pool (persistent across frames) ────────────
    rg::RgResourcePool rgPool(device);

    // ── Camera (UE5 default viewport values, scaled to meters) ─────────
    Camera camera;
    // UE5: 90deg horizontal FOV -> ~59deg vertical FOV at 16:9
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);
    // UE5 default: Position(0, 1024, 512)cm -> meters: (0, 10.24, 5.12)
    // Scaled down for our scene: (0, 5, 2.5)
    // Yaw=-90deg from +X = looking along -Y, Pitch=-15deg = slightly downward
    camera.SetPosition({0.f, 5.f, 2.5f});

    constexpr float kPitchDeg = -15.f;
    constexpr float kYawDeg = -90.f;
    constexpr float kDegToRad = 3.14159265f / 180.f;
    {
        // Build forward direction from Euler angles in Z-up space
        float yawRad = kYawDeg * kDegToRad;
        float pitchRad = kPitchDeg * kDegToRad;
        float cosPitch = std::cos(pitchRad);
        Vec3 forward = {
            std::cos(yawRad) * cosPitch,
            std::sin(yawRad) * cosPitch,
            std::sin(pitchRad)
        };
        Vec3 pos = camera.GetPosition();
        camera.LookAt(pos + forward, Vec3::Up());
    }

    FPSCameraController cameraCtrl;
    cameraCtrl.InitFromCamera(camera);
    cameraCtrl.SetMoveSpeed(10.f);

    // ── Render Loop ──────────────────────────────────────────────────────
    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto prevTime = std::chrono::high_resolution_clock::now();
    auto startTime = prevTime;

    UZ_LOG(Renderer, Info, "Camera test demo started — WASD move, RMB+mouse look, Shift sprint");

    while (window.PumpMessages() && window.IsOpen()) {
        // Skip rendering while minimized — swapchain surface is 0x0
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        if (frameCount > 0 && frameIndex >= frameCount) break;

        // ── Timing ───────────────────────────────────────────────────────
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        prevTime = now;

        // ── Stats ────────────────────────────────────────────────────────
        statsOverlay.BeginFrame(dt);

        // ── Input ────────────────────────────────────────────────────────
        input.BeginFrame();

        // ── Camera Update ────────────────────────────────────────────────
        cameraCtrl.Update(camera, input, dt);

        // Handle resize
        if (window.WasResized()) {
            device->WaitIdle();
            swapChain->Resize(
                static_cast<uint32_t>(window.GetWidth()),
                static_cast<uint32_t>(window.GetHeight()));
            // Recreate depth buffer at new size
            device->DestroyTexture(depthBuffer);
            depthDesc.width = static_cast<uint32_t>(window.GetWidth());
            depthDesc.height = static_cast<uint32_t>(window.GetHeight());
            depthBuffer = device->CreateTexture(depthDesc);
            statsOverlay.SetScreenSize(depthDesc.width, depthDesc.height);
            window.ClearResizedFlag();
            camera.SetAspectRatio(
                static_cast<float>(window.GetWidth()) /
                static_cast<float>(window.GetHeight()));
        }

        // Wait for frame N-2 before reusing its allocator
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        // ── Upload PerFrameCB ────────────────────────────────────────────
        cbRing.BeginFrame(frameIndex);

        PerFrameCB perFrame{};
        perFrame.viewProj = camera.GetViewProjection();
        Vec3 camPos = camera.GetPosition();
        perFrame.cameraPos = {camPos.x, camPos.y, camPos.z, 0.f};
        perFrame.timeParams = {totalTime, dt, static_cast<float>(frameIndex), 0.f};

        CBAllocation cbAlloc = cbRing.Allocate(sizeof(PerFrameCB));
        std::memcpy(cbAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

        // ── Record via render graph ──────────────────────────────────────
        bool capturedThisFrame = false;
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

        rg::RgBuilder rgBuilder(device, &rgPool);
        auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

        // 3D scene pass: clear + cube + grid.
        rgBuilder.AddPass("CameraScene", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                auto bb = res.GetTexture(bbRes);

                float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
                cmd->ClearRenderTarget(bb, clearColor);
                cmd->ClearDepthStencil(depthBuffer, 1.0f, 0);

                RHIRenderPassDesc rpDesc{};
                rpDesc.colorAttachments[0].texture = bb;
                rpDesc.colorAttachmentCount = 1;
                rpDesc.hasDepth = true;
                rpDesc.depthAttachment.texture = depthBuffer;
                cmd->BeginRenderPass(rpDesc);

                cmd->SetPipeline(pipeline);

                if (bindless) {
                    cmd->SetDescriptorIndex(0, vbSrvIndex);
                } else {
                    cmd->SetVertexBuffer(0, vbHandle, 0);
                }

                cmd->SetRootCBV(1, cbAlloc.bufferHandle, cbAlloc.offsetBytes,
                                cbAlloc.cpuPtr, sizeof(PerFrameCB));

                RHIViewport vp{};
                vp.width = static_cast<float>(swapChain->GetWidth());
                vp.height = static_cast<float>(swapChain->GetHeight());
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right = swapChain->GetWidth();
                sc.bottom = swapChain->GetHeight();
                cmd->SetScissor(sc);

                cmd->Draw(kCubeVertexCount);

#if defined(__APPLE__)
                // Grid (macOS) — fullscreen triangle with procedural grid shader
                {
                    GridCB gridCb{};
                    Mat4 vpMat = camera.GetViewProjection();
                    gridCb.invViewProj = vpMat.inverse();
                    gridCb.viewProj = vpMat;
                    Vec3 gridCamPos = camera.GetPosition();
                    gridCb.cameraPos = {gridCamPos.x, gridCamPos.y, gridCamPos.z, 0.f};
                    gridCb.gridParams = {1.0f, 10.0f, 100.0f, 0.f};

                    CBAllocation gridAlloc = cbRing.Allocate(sizeof(GridCB));
                    std::memcpy(gridAlloc.cpuPtr, &gridCb, sizeof(GridCB));

                    cmd->SetPipeline(gridPipeline);
                    cmd->SetRootCBV(1, gridAlloc.bufferHandle, gridAlloc.offsetBytes,
                                    gridAlloc.cpuPtr, sizeof(GridCB));
                    cmd->Draw(3);
                }
#elif defined(_WIN32)
                editorGrid.Draw(cmd, cbRing, camera);
#endif

                cmd->EndRenderPass();
            });
        });

        // Stats overlay pass: color only, no depth — 2D UI on top of 3D.
        rgBuilder.AddPass("StatsOverlay", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                auto bb = res.GetTexture(bbRes);

                statsOverlay.SetDrawCalls(2);
                statsOverlay.SetVertexCount(kCubeVertexCount + 3);
                statsOverlay.SetTriangleCount(kCubeVertexCount / 3 + 1);

                RHIRenderPassDesc overlayRpDesc{};
                overlayRpDesc.colorAttachments[0].texture = bb;
                overlayRpDesc.colorAttachmentCount = 1;
                overlayRpDesc.hasDepth = false;
                cmd->BeginRenderPass(overlayRpDesc);
                statsOverlay.Render(cmd, cbRing, frameIndex);
                cmd->EndRenderPass();
            });
        });

        // Screenshot pass (optional).
#ifdef _WIN32
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
                pb.Read(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                    screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
                    capturedThisFrame = true;
                });
            });
        }
#endif

        // Present transition pass.
        rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::Present);
            pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
        });

        rgBuilder.Compile();
        rgBuilder.Execute(cmdList);

        cmdList->End();
        cmdList->Execute();

        swapChain->Present(false);

        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);

#ifdef _WIN32
        // ── Save screenshot after GPU completes ──────────────────────────
        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }
#endif

        frameIndex++;

        // ── FPS Cap ──────────────────────────────────────────────────────
        auto frameEnd = std::chrono::high_resolution_clock::now();
        float elapsed = std::chrono::duration<float>(frameEnd - now).count();
        if (elapsed < kMinFrameTime) {
            auto sleepUs = std::chrono::microseconds(
                static_cast<int64_t>((kMinFrameTime - elapsed) * 1e6f));
            std::this_thread::sleep_for(sleepUs);
        }
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.f;

    UZ_LOG(Renderer, Info,
           "Camera test demo: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
           frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.f / avgMs : 0.f);

    statsOverlay.Shutdown();
#ifdef _WIN32
    screenshotCapture.Shutdown();
    editorGrid.Shutdown();
    shaderPipeline.Shutdown();
#endif
#if defined(__APPLE__)
    device->DestroyPipeline(gridPipeline);
    device->DestroyShader(gridVsHandle);
    device->DestroyShader(gridPsHandle);
#endif
    cbRing.Shutdown();
    device->DestroyTexture(depthBuffer);
    device->DestroyBuffer(vbHandle);
    device->DestroyPipeline(pipeline);
    device->DestroyShader(vsHandle);
    device->DestroyShader(psHandle);
    for (uint32_t i = 0; i < kFramesInFlight; ++i) device->DestroyFence(frameFences[i]);
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
