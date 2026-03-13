/// Post-Process Test Demo — Full pipeline: 3D Scene → HDR → Tonemap → FXAA → Display
///
/// Renders a lit cube mesh with editor grid into an HDR render target (RGBA16Float),
/// then applies the post-processing chain through the render graph:
///   1. Scene3D: Clear HDR + depth, draw lit cube + editor grid
///   2. Tone mapping (ACES filmic via tonemap_ps.hlsl at register b2)
///   3. FXAA (edge AA via fxaa_ps.hlsl at register b2)
///   4. Output to backbuffer
///
/// Toggle keys: T=tonemap, A=fxaa

#include "postprocess_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "log.h"
#include "profiler.h"
#include "screenshot_capture.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_shaders.h"
#include "paths.h"
#include "input.h"
#include "root_slots.h"
#include "cb_ring_allocator.h"
#include "vertex.h"
#include "mesh.h"
#include "upload_context.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "editor_grid.h"
#include "texture_loader.h"

#include <cmath>
#include <chrono>
#include <thread>
#include <cstring>
#include <memory>

namespace uz {

static constexpr uint32_t kFramesInFlight = 3;

UZ_DEFINE_LOG_CATEGORY(PostProcessDemo, Info);

// ─────────────────────────────────────────────────────────────────────────────
// CB structs matching shader layouts
// ─────────────────────────────────────────────────────────────────────────────

/// Matches tonemap_ps.hlsl PostProcessParams at register(b2).
struct alignas(256) ToneMapCB {
    float tonemapIntensity = 1.0f;
    float exposureAdjust   = 0.0f;
    float bypassTonemap    = 0.0f;  ///< >0.5 skips ACES curve, just saturates.
    float _pad1            = 0.0f;
};

/// Matches fxaa_ps.hlsl FXAAParams at register(b2).
struct alignas(256) FXAACB {
    float invScreenWidth  = 1.0f / 1280.0f;
    float invScreenHeight = 1.0f / 720.0f;
    float edgeThreshold   = 0.1f;
    float blurStrength    = 1.0f;
};

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Light PerFrame CB — matches mesh_ps.hlsl 768-byte layout at b1
// ─────────────────────────────────────────────────────────────────────────────

/// Single light for the multi-light forward CB (64 bytes per light).
/// Matches LightData in light_types.hlsli.
struct LightDataGPU {
    Vec3  positionOrDir;    ///< Direction toward light (directional) or world position (point/spot).
    float range;            ///< Attenuation range (point/spot). Unused for directional.
    Vec3  color;            ///< Light color * intensity (linear HDR).
    float lightType;        ///< 0 = directional, 1 = point, 2 = spot.
    Vec3  spotDirection;    ///< Spot light cone direction. Unused for directional/point.
    float innerConeCos;     ///< cos(innerConeAngle) for spot lights.
    float outerConeCos;     ///< cos(outerConeAngle) for spot lights.
    float _pad0 = 0.f;
    float _pad1 = 0.f;
    float _pad2 = 0.f;
};

static_assert(sizeof(LightDataGPU) == 64, "LightDataGPU must be 64 bytes for mesh_ps.hlsl");

/// Maximum lights supported by the multi-light forward CB.
static constexpr uint32_t kMeshMaxLights = 8;

/// Per-frame constant buffer matching the 768-byte layout expected by mesh_ps.hlsl.
struct PerFrameMultiLightCB {
    Mat4         viewProj;                     ///< 64B — combined view-projection matrix.
    Vec4         cameraPos;                    ///< 16B — xyz = camera world position, w = 0.
    Vec4         timeParams;                   ///< 16B — x = totalTime, y = dt, z = frame, w = 0.
    Vec4         ambientColor;                 ///< 16B — xyz = ambient light color, w = 0.
    uint32_t     lightCount = 0;               ///< 4B  — number of active lights.
    uint32_t     _pad0a = 0;                   ///< 4B  — padding.
    uint32_t     _pad0b = 0;                   ///< 4B  — padding.
    uint32_t     _pad0c = 0;                   ///< 4B  — padding.
    LightDataGPU lights[kMeshMaxLights];       ///< 512B — light array (8 * 64).
    Vec4         _pad1[8] = {};                ///< 128B — trailing padding.
};

static_assert(sizeof(PerFrameMultiLightCB) == 768,
              "PerFrameMultiLightCB must be 768 bytes for mesh_ps.hlsl multi-light CB");

// ─────────────────────────────────────────────────────────────────────────────
// Helper: create fullscreen PSO (no vertex input, no depth)
// ─────────────────────────────────────────────────────────────────────────────

static RHIPipelineHandle CreateFullscreenPSO(IRHIDevice* device,
                                              RHIShaderHandle vs,
                                              RHIShaderHandle ps,
                                              RHIFormat rtFormat,
                                              const char* debugName)
{
    RHIGraphicsPipelineDesc desc{};
    desc.vertexShader = vs;
    desc.pixelShader  = ps;
    desc.topology     = RHIPrimitiveTopology::TriangleList;
    desc.rasterizer.cullMode              = RHICullMode::None;
    desc.depthStencil.depthEnable         = false;
    desc.depthStencil.depthWrite          = false;
    desc.renderTargetFormats[0]           = rtFormat;
    desc.renderTargetCount                = 1;
    desc.debugName                        = debugName;
    return device->CreateGraphicsPipeline(desc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Main demo function
// ─────────────────────────────────────────────────────────────────────────────

bool RunPostProcessTest(const PostProcessTestConfig& config) {
    UZ_PROFILE_FUNCTION();
    UZ_PROFILE_THREAD("Main");

    // ── Window ───────────────────────────────────────────────────────────
    WindowDesc winDesc{};
    winDesc.title  = "UzEngine — Post-Process Test (T=tonemap A=fxaa)";
    winDesc.width  = 1280;
    winDesc.height = 720;

    PlatformWindow window;
    if (!window.Create(winDesc)) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create window");
        return false;
    }
    window.Show();

    // ── Device ───────────────────────────────────────────────────────────
    RHIDeviceDesc deviceDesc{};
    deviceDesc.backend = config.backend;
    deviceDesc.enableValidation = true;

    IRHIDevice* device = CreateRHIDevice(deviceDesc);
    if (!device) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create RHI device");
        return false;
    }

    // ── Swapchain ────────────────────────────────────────────────────────
    uint32_t w = static_cast<uint32_t>(window.GetWidth());
    uint32_t h = static_cast<uint32_t>(window.GetHeight());

    RHISwapChainDesc scDesc{};
    scDesc.windowHandle   = window.GetNativeHandle();
    scDesc.displayHandle  = window.GetNativeDisplayHandle();
    scDesc.width          = w;
    scDesc.height         = h;
    scDesc.bufferCount    = kFramesInFlight;
    scDesc.format         = RHIFormat::RGBA8Unorm;
    scDesc.vsync          = false;

    IRHISwapChain* swapChain = device->CreateSwapChain(scDesc);
    if (!swapChain) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create swapchain");
        DestroyRHIDevice(device);
        return false;
    }

    // ── Command list + fences ────────────────────────────────────────────
    IRHICommandList* cmdList = device->CreateCommandList(RHIQueueType::Graphics);
    if (!cmdList) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create command list");
        device->DestroySwapChain(swapChain);
        DestroyRHIDevice(device);
        return false;
    }

    IRHIFence* frameFences[kFramesInFlight] = {};
    uint64_t fenceValues[kFramesInFlight] = {};
    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        frameFences[i] = device->CreateFence();
    }

    // ── CB Ring Allocator ────────────────────────────────────────────────
    CBRingAllocator cbRing;
    cbRing.Init(device, kFramesInFlight, 64 * 1024);

    // ── Compile shaders ──────────────────────────────────────────────────
    const bool bindless = device->IsBindless();

    auto compiler = std::make_unique<ShaderCompiler>();
    if (!compiler->Init()) {
        UZ_LOG(PostProcessDemo, Error, "Failed to init DXC shader compiler");
        return false;
    }

    std::unique_ptr<ShaderCompilerFXC> compilerFXC;
    if (!bindless) {
        compilerFXC = std::make_unique<ShaderCompilerFXC>();
        compilerFXC->Init();
    }

    auto shaderPipeline = std::make_unique<ShaderPipeline>();
    ShaderPipeline::Config pipeCfg;
    pipeCfg.cacheDir      = (uz::GetExeDir() / "cache/shaders").string();
    pipeCfg.targetBackend = bindless ? "dx12_sm66" : "dx12_sm50";
    pipeCfg.bindless      = bindless;
    shaderPipeline->Init(pipeCfg);
    shaderPipeline->SetCompiler(compiler.get());
    if (compilerFXC) shaderPipeline->SetCompilerFXC(compilerFXC.get());

    auto loadShader = [&](EngineShader shader) {
        return shaderPipeline->LoadBytecodeFromSource(
            EngineShaders::GetName(shader), EngineShaders::GetSource(shader));
    };

    // Fullscreen post-process shaders
    auto fsVSBytes    = loadShader(EngineShader::FullscreenVS);
    auto tonemapBytes = loadShader(EngineShader::TonemapPS);
    auto fxaaBytes    = loadShader(EngineShader::FxaaPS);

    // Mesh shaders for 3D scene
    auto meshVSBytes = loadShader(EngineShader::MeshVS);
    auto meshPSBytes = loadShader(EngineShader::MeshPS);

    uint32_t compiled = 0;
    if (!fsVSBytes.empty())    ++compiled;
    if (!tonemapBytes.empty()) ++compiled;
    if (!fxaaBytes.empty())    ++compiled;
    if (!meshVSBytes.empty())  ++compiled;
    if (!meshPSBytes.empty())  ++compiled;
    UZ_LOG(PostProcessDemo, Info, "Shader compilation: {}/5 succeeded", compiled);

    if (compiled < 5) {
        UZ_LOG(PostProcessDemo, Error, "Required shaders failed to compile — aborting");
        return false;
    }

    // ── Create RHI shader handles ────────────────────────────────────────
    auto makeShader = [&](const std::vector<uint8_t>& bc, RHIShaderStage stage,
                          const char* name) -> RHIShaderHandle {
        RHIShaderDesc d{};
        d.bytecode     = bc.data();
        d.bytecodeSize = bc.size();
        d.stage        = stage;
        d.entryPoint   = "main";
        d.debugName    = name;
        return device->CreateShader(d);
    };

    RHIShaderHandle fsVS      = makeShader(fsVSBytes,    RHIShaderStage::Vertex, "fullscreen_vs");
    RHIShaderHandle tonemapPS = makeShader(tonemapBytes, RHIShaderStage::Pixel,  "tonemap_ps");
    RHIShaderHandle fxaaPS    = makeShader(fxaaBytes,    RHIShaderStage::Pixel,  "fxaa_ps");
    RHIShaderHandle meshVS    = makeShader(meshVSBytes,  RHIShaderStage::Vertex, "mesh_vs");
    RHIShaderHandle meshPS    = makeShader(meshPSBytes,  RHIShaderStage::Pixel,  "mesh_ps");

    // ── Create fullscreen PSOs ───────────────────────────────────────────
    auto tonemapPSO = CreateFullscreenPSO(device, fsVS, tonemapPS,
                                           RHIFormat::RGBA8Unorm, "ToneMap_PSO");
    auto fxaaPSO    = CreateFullscreenPSO(device, fsVS, fxaaPS,
                                           RHIFormat::RGBA8Unorm, "FXAA_PSO");

    if (!tonemapPSO.IsValid() || !fxaaPSO.IsValid()) {
        UZ_LOG(PostProcessDemo, Error, "Fullscreen PSO creation failed");
        return false;
    }

    // ── Create mesh pipeline (renders into RGBA16Float HDR target) ───────
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc meshPipeDesc{};
    meshPipeDesc.vertexShader    = meshVS;
    meshPipeDesc.pixelShader     = meshPS;
    meshPipeDesc.topology        = RHIPrimitiveTopology::TriangleList;
    meshPipeDesc.rasterizer.cullMode              = RHICullMode::Back;
    meshPipeDesc.rasterizer.frontCounterClockwise = false;
    meshPipeDesc.depthStencil.depthEnable         = true;
    meshPipeDesc.depthStencil.depthWrite          = true;
    meshPipeDesc.renderTargetFormats[0]           = RHIFormat::RGBA16Float;
    meshPipeDesc.renderTargetCount                = 1;
    meshPipeDesc.depthStencilFormat               = RHIFormat::D32Float;
    meshPipeDesc.debugName                        = "MeshPipeline_HDR";

    if (!bindless) {
        meshPipeDesc.inputElements     = inputElements;
        meshPipeDesc.inputElementCount = 4;
        meshPipeDesc.vertexStride      = sizeof(Vertex);
    }

    RHIPipelineHandle meshPipeline = device->CreateGraphicsPipeline(meshPipeDesc);
    if (!meshPipeline.IsValid()) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create mesh pipeline");
        return false;
    }

    // ── Create sphere mesh (curved edges show FXAA aliasing clearly) ────
    Mesh sphereMesh = MeshBuilder::CreateSphere(1.0f, 32, 64);
    UZ_LOG(PostProcessDemo, Info, "Created UV sphere: {} vertices, {} indices",
           sphereMesh.GetVertexCount(), sphereMesh.GetIndexCount());

    // ── Upload vertex buffer ─────────────────────────────────────────────
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sphereMesh.GetVertexDataSize();
    vbDesc.stride    = sphereMesh.header.vertexStride;
    vbDesc.usage     = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory    = RHIMemoryType::Upload;
    vbDesc.debugName = "PostProcess_VB";

    RHIBufferHandle vbHandle = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle);
    std::memcpy(vbMapped, sphereMesh.vertexData.data(), sphereMesh.GetVertexDataSize());
    device->UnmapBuffer(vbHandle);

    uint32_t vbSrvIndex = 0;
    if (bindless) {
        vbSrvIndex = device->GetBufferSRVIndex(vbHandle);
    }

    // ── Upload index buffer ──────────────────────────────────────────────
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = sphereMesh.GetIndexDataSize();
    ibDesc.stride    = (sphereMesh.header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage     = RHIBufferUsage::IndexBuffer;
    ibDesc.memory    = RHIMemoryType::Upload;
    ibDesc.debugName = "PostProcess_IB";

    RHIBufferHandle ibHandle = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle);
    std::memcpy(ibMapped, sphereMesh.indexData.data(), sphereMesh.GetIndexDataSize());
    device->UnmapBuffer(ibHandle);

    // ── Load checker texture ────────────────────────────────────────────
    std::string texPath = (uz::GetExeDir() / "assets/checker.png").string();
    auto checkerTex = TextureLoader::Load(device, cmdList, texPath);
    if (!checkerTex.handle.IsValid()) {
        UZ_LOG(PostProcessDemo, Warning, "checker.png not found — spheres will be white");
    }

    // ── Depth buffer ─────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width     = w;
    depthDesc.height    = h;
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "PostProcess_Depth";

    RHITextureHandle depthBuffer = device->CreateTexture(depthDesc);

    // ── Create intermediate render targets ───────────────────────────────
    const RHITextureUsage kRTUsage = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

    auto createRT = [&](RHIFormat fmt, const char* name) -> RHITextureHandle {
        RHITextureDesc td{};
        td.width     = w;
        td.height    = h;
        td.format    = fmt;
        td.usage     = kRTUsage;
        td.debugName = name;
        return device->CreateTexture(td);
    };

    RHITextureHandle hdrTarget     = createRT(RHIFormat::RGBA16Float, "HDR_Scene");
    RHITextureHandle tonemapTarget = createRT(RHIFormat::RGBA8Unorm,  "ToneMap_Out");

    UZ_LOG(PostProcessDemo, Info, "Created render targets: HDR (RGBA16F) + ToneMap (RGBA8) @ {}x{}", w, h);

    // ── Editor Grid ──────────────────────────────────────────────────────
    EditorGrid editorGrid;
    if (!editorGrid.Init(device, *shaderPipeline, RHIFormat::RGBA16Float, RHIFormat::D32Float)) {
        UZ_LOG(PostProcessDemo, Error, "Failed to initialize editor grid");
        return false;
    }

    // ── Input ────────────────────────────────────────────────────────────
    InputSystem input;
    input.Init();
    window.SetInputSystem(&input);

    // ── Camera ───────────────────────────────────────────────────────────
    Camera camera;
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera.SetPerspective(vFovRad,
        static_cast<float>(winDesc.width) / static_cast<float>(winDesc.height),
        0.1f, 1000.f);
    camera.SetPosition({0.f, 5.f, 2.5f});
    {
        constexpr float kDegToRad = 3.14159265f / 180.f;
        float yawRad   = -90.f * kDegToRad;
        float pitchRad = -15.f * kDegToRad;
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

    // ── Screenshot ───────────────────────────────────────────────────────
    ScreenshotCapture screenshotCapture;
    bool screenshotPending = !config.screenshotPath.empty();
    if (screenshotPending) {
        screenshotCapture.Init(device, w, h);
    }

    // ── Render graph pool ────────────────────────────────────────────────
    rg::RgResourcePool rgPool(device);

    // ── State ────────────────────────────────────────────────────────────
    bool enableTonemap = config.enableTonemap;
    bool enableFXAA    = config.enableFXAA;

    uint32_t frameIndex = 0;
    uint64_t globalFenceValue = 0;
    auto startTime = std::chrono::high_resolution_clock::now();
    auto prevTime  = startTime;

    UZ_LOG(PostProcessDemo, Info,
        "Starting render loop — T=tonemap A=fxaa toggles | Scene3D → ToneMap → FXAA → display");

    // ── Render loop ──────────────────────────────────────────────────────
    while (window.PumpMessages() && window.IsOpen()) {
        if (window.IsMinimized()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        UZ_PROFILE_SCOPE(Frame);
        if (config.frameCount > 0 && frameIndex >= config.frameCount) break;

        // ── Timing ───────────────────────────────────────────────────────
        auto now = std::chrono::high_resolution_clock::now();
        float dt = std::chrono::duration<float>(now - prevTime).count();
        float totalTime = std::chrono::duration<float>(now - startTime).count();
        prevTime = now;

        // ── Resize ───────────────────────────────────────────────────────
        if (window.WasResized()) {
            device->WaitIdle();
            w = static_cast<uint32_t>(window.GetWidth());
            h = static_cast<uint32_t>(window.GetHeight());
            swapChain->Resize(w, h);

            // Recreate depth buffer at new size
            device->DestroyTexture(depthBuffer);
            depthDesc.width  = w;
            depthDesc.height = h;
            depthBuffer = device->CreateTexture(depthDesc);

            // Recreate render targets at new size
            device->DestroyTexture(hdrTarget);
            device->DestroyTexture(tonemapTarget);
            hdrTarget     = createRT(RHIFormat::RGBA16Float, "HDR_Scene");
            tonemapTarget = createRT(RHIFormat::RGBA8Unorm,  "ToneMap_Out");

            camera.SetAspectRatio(static_cast<float>(w) / static_cast<float>(h));
            window.ClearResizedFlag();
        }

        // ── Input ────────────────────────────────────────────────────────
        input.BeginFrame();
        if (input.IsKeyJustPressed(Key::T)) {
            enableTonemap = !enableTonemap;
            UZ_LOG(PostProcessDemo, Info, "Tone mapping: {}", enableTonemap ? "ON" : "OFF");
        }
        if (input.IsKeyJustPressed(Key::A)) {
            enableFXAA = !enableFXAA;
            UZ_LOG(PostProcessDemo, Info, "FXAA: {}", enableFXAA ? "ON" : "OFF");
        }

        // ── Camera Update ────────────────────────────────────────────────
        cameraCtrl.Update(camera, input, dt);

        // Wait oldest fence
        uint32_t allocFrame = frameIndex % kFramesInFlight;
        if (fenceValues[allocFrame] > 0) {
            frameFences[allocFrame]->WaitCPU(fenceValues[allocFrame]);
        }

        cbRing.BeginFrame(frameIndex);

        // ── Upload PerFrameMultiLightCB ──────────────────────────────────
        PerFrameMultiLightCB perFrame{};
        perFrame.viewProj = camera.GetViewProjection();
        Vec3 camPos = camera.GetPosition();
        perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
        perFrame.timeParams  = {totalTime, dt, static_cast<float>(frameIndex), 0.f};
        perFrame.ambientColor = {0.08f, 0.08f, 0.1f, 0.f};

        // Directional light — extreme HDR intensity to make tonemap difference obvious
        perFrame.lightCount = 2;

        // Sun: warm directional, very high intensity
        LightDataGPU& sun = perFrame.lights[0];
        Vec3 rawDir = {0.5f, 1.0f, 0.3f};
        float len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
        sun.positionOrDir = {rawDir.x / len, rawDir.y / len, rawDir.z / len};
        sun.range     = 0.f;
        sun.color     = {50.0f, 40.0f, 30.0f};  // Warm white, extreme HDR
        sun.lightType = 0.f;

        // Fill light: cool blue from opposite side
        LightDataGPU& fill = perFrame.lights[1];
        Vec3 fillDir = {-0.3f, 0.5f, -0.8f};
        float fillLen = std::sqrt(fillDir.x * fillDir.x + fillDir.y * fillDir.y + fillDir.z * fillDir.z);
        fill.positionOrDir = {fillDir.x / fillLen, fillDir.y / fillLen, fillDir.z / fillLen};
        fill.range     = 0.f;
        fill.color     = {5.0f, 8.0f, 20.0f};  // Cool blue fill
        fill.lightType = 0.f;

        CBAllocation perFrameAlloc = cbRing.Allocate(sizeof(PerFrameMultiLightCB));
        std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameMultiLightCB));

        // ── Upload PerObjectCBs (3 cubes at different positions) ─────────
        static constexpr uint32_t kCubeCount = 3;
        struct CubeInstance { Vec3 pos; float scale; };
        CubeInstance cubes[kCubeCount] = {
            {{-2.5f, 0.f, 0.f}, 1.0f},
            {{ 0.0f, 0.f, 0.f}, 1.0f},
            {{ 2.5f, 0.f, 0.f}, 1.0f},
        };

        CBAllocation perObjectAllocs[kCubeCount];
        for (uint32_t ci = 0; ci < kCubeCount; ++ci) {
            PerObjectCB perObject{};
            perObject.model        = Mat4::TRS(cubes[ci].pos, cubes[ci].scale);
            perObject.normalMatrix = perObject.model.NormalMatrix();
            perObject.albedoIndex  = checkerSrvIdx;
            perObjectAllocs[ci] = cbRing.Allocate(sizeof(PerObjectCB));
            std::memcpy(perObjectAllocs[ci].cpuPtr, &perObject, sizeof(PerObjectCB));
        }

        // ── Upload post-process CB data ──────────────────────────────────
        ToneMapCB tmData{};
        tmData.tonemapIntensity = 1.0f;
        tmData.exposureAdjust   = 2.0f;  // +2 EV = 4x brighter, amplifies HDR range
        tmData.bypassTonemap    = 0.0f;  // Apply ACES curve
        auto tmAlloc = cbRing.Allocate(sizeof(ToneMapCB));
        std::memcpy(tmAlloc.cpuPtr, &tmData, sizeof(ToneMapCB));

        // Passthrough blit CB — no exposure boost, no ACES, just saturate
        ToneMapCB blitData{};
        blitData.tonemapIntensity = 1.0f;
        blitData.exposureAdjust   = 0.0f;  // No exposure change
        blitData.bypassTonemap    = 1.0f;  // Skip ACES curve
        auto blitAlloc = cbRing.Allocate(sizeof(ToneMapCB));
        std::memcpy(blitAlloc.cpuPtr, &blitData, sizeof(ToneMapCB));

        FXAACB fxData{};
        fxData.invScreenWidth  = 1.0f / static_cast<float>(w);
        fxData.invScreenHeight = 1.0f / static_cast<float>(h);
        fxData.edgeThreshold   = 0.1f;
        fxData.blurStrength    = 1.0f;
        auto fxAlloc = cbRing.Allocate(sizeof(FXAACB));
        std::memcpy(fxAlloc.cpuPtr, &fxData, sizeof(FXAACB));

        // ── Build render graph ───────────────────────────────────────────
        bool capturedThisFrame = false;
        {
        UZ_PROFILE_SCOPE(RecordCommands);
        cmdList->Begin();

        RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
        rg::RgBuilder rgBuilder(device, &rgPool);

        auto bbRes  = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
        auto hdrRes = rgBuilder.ImportTexture("HDR_Scene", hdrTarget, RHIResourceState::Common);
        auto tmRes  = rgBuilder.ImportTexture("ToneMap_Out", tonemapTarget, RHIResourceState::Common);

        // ── Pass 1: Scene3D — render lit cube + grid into HDR target ─
        rgBuilder.AddPass("Scene3D", [&](rg::RgPassBuilder& pb) {
            pb.Write(hdrRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                UZ_PROFILE_SCOPE("Scene3D::Execute");

                auto hdr = res.GetTexture(hdrRes);

                // Clear HDR and depth
                float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
                cmd->ClearRenderTarget(hdr, clearColor);
                cmd->ClearDepthStencil(depthBuffer, 1.0f, 0);

                // Begin render pass with HDR color + depth
                RHIRenderPassDesc passDesc{};
                passDesc.colorAttachmentCount = 1;
                passDesc.colorAttachments[0].texture = hdr;
                passDesc.colorAttachments[0].clear   = false;
                passDesc.hasDepth = true;
                passDesc.depthAttachment.texture    = depthBuffer;
                passDesc.depthAttachment.clearDepth = 1.0f;
                passDesc.depthAttachment.clear      = false;
                cmd->BeginRenderPass(passDesc);

                // Set viewport and scissor
                RHIViewport vp{};
                vp.width    = static_cast<float>(w);
                vp.height   = static_cast<float>(h);
                vp.maxDepth = 1.0f;
                cmd->SetViewport(vp);

                RHIScissorRect sc{};
                sc.right  = static_cast<int32_t>(w);
                sc.bottom = static_cast<int32_t>(h);
                cmd->SetScissor(sc);

                // Draw cube meshes
                cmd->SetPipeline(meshPipeline);

                if (bindless) {
                    cmd->SetDescriptorIndex(0, vbSrvIndex);
                } else {
                    cmd->SetVertexBuffer(0, vbHandle, 0);
                }

                cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                                perFrameAlloc.offsetBytes, perFrameAlloc.cpuPtr,
                                sizeof(PerFrameMultiLightCB));

                RHIIndexFormat idxFmt = (sphereMesh.header.indexFormat == MeshIndexFormat::UInt32)
                    ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
                cmd->SetIndexBuffer(ibHandle, idxFmt, 0);

                // Bind checker texture for SM5 (t0 = albedo)
                if (!bindless && checkerTexHandle.IsValid()) {
                    cmd->SetFragmentTexture(0, checkerTexHandle);
                }

                for (uint32_t ci = 0; ci < kCubeCount; ++ci) {
                    cmd->SetRootCBV(kSlotPerObject, perObjectAllocs[ci].bufferHandle,
                                    perObjectAllocs[ci].offsetBytes, perObjectAllocs[ci].cpuPtr,
                                    sizeof(PerObjectCB));
                    cmd->DrawIndexed(sphereMesh.GetIndexCount());
                }

                // Draw editor grid
                editorGrid.Draw(cmd, cbRing, camera);

                cmd->EndRenderPass();
            });
        });

        // ── Pass 2: Tone mapping (HDR → LDR) ────────────────────────
        // If FXAA follows, write to intermediate tmRes; otherwise write straight to backbuffer.
        auto tmOutputRes = enableFXAA ? tmRes : bbRes;

        if (enableTonemap) {
            rgBuilder.AddPass("ToneMap", [&](rg::RgPassBuilder& pb) {
                pb.Read(hdrRes, RHIResourceState::ShaderResource);
                pb.Write(tmOutputRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("ToneMap::Execute");

                    auto outTex = res.GetTexture(tmOutputRes);

                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount = 1;
                    passDesc.colorAttachments[0].texture = outTex;
                    passDesc.colorAttachments[0].clear   = false;
                    passDesc.hasDepth = false;
                    cmd->BeginRenderPass(passDesc);

                    cmd->SetPipeline(tonemapPSO);

                    cmd->SetRootCBV(kSlotPerObject, tmAlloc.bufferHandle,
                                    tmAlloc.offsetBytes, tmAlloc.cpuPtr, sizeof(ToneMapCB));

                    if (bindless) {
                        uint32_t srvIdx = device->GetTextureSRVIndex(res.GetTexture(hdrRes));
                        cmd->SetRootConstants(kSlotRootConstants, &srvIdx, 1, 0);
                    } else {
                        cmd->SetFragmentTexture(0, res.GetTexture(hdrRes));
                    }

                    RHIViewport vp{};
                    vp.width    = static_cast<float>(w);
                    vp.height   = static_cast<float>(h);
                    vp.maxDepth = 1.0f;
                    cmd->SetViewport(vp);

                    RHIScissorRect sc{};
                    sc.right  = static_cast<int32_t>(w);
                    sc.bottom = static_cast<int32_t>(h);
                    cmd->SetScissor(sc);

                    cmd->Draw(3);
                    cmd->EndRenderPass();
                });
            });
        }

        // ── Pass 3: FXAA (edge AA) ──────────────────────────────────
        if (enableFXAA) {
            auto fxaaInputRes = enableTonemap ? tmRes : hdrRes;
            rgBuilder.AddPass("FXAA", [&](rg::RgPassBuilder& pb) {
                pb.Read(fxaaInputRes, RHIResourceState::ShaderResource);
                pb.Write(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    UZ_PROFILE_SCOPE("FXAA::Execute");

                    auto outTex = res.GetTexture(bbRes);

                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount = 1;
                    passDesc.colorAttachments[0].texture = outTex;
                    passDesc.colorAttachments[0].clear   = false;
                    passDesc.hasDepth = false;
                    cmd->BeginRenderPass(passDesc);

                    cmd->SetPipeline(fxaaPSO);

                    cmd->SetRootCBV(kSlotPerObject, fxAlloc.bufferHandle,
                                    fxAlloc.offsetBytes, fxAlloc.cpuPtr, sizeof(FXAACB));

                    if (bindless) {
                        uint32_t srvIdx = device->GetTextureSRVIndex(res.GetTexture(fxaaInputRes));
                        cmd->SetRootConstants(kSlotRootConstants, &srvIdx, 1, 0);
                    } else {
                        cmd->SetFragmentTexture(0, res.GetTexture(fxaaInputRes));
                    }

                    RHIViewport vp{};
                    vp.width    = static_cast<float>(w);
                    vp.height   = static_cast<float>(h);
                    vp.maxDepth = 1.0f;
                    cmd->SetViewport(vp);

                    RHIScissorRect sc{};
                    sc.right  = static_cast<int32_t>(w);
                    sc.bottom = static_cast<int32_t>(h);
                    cmd->SetScissor(sc);

                    cmd->Draw(3);
                    cmd->EndRenderPass();
                });
            });
        } else if (!enableTonemap) {
            // Neither tonemap nor FXAA — blit HDR to backbuffer via tonemap passthrough
            rgBuilder.AddPass("BlitToBackbuffer", [&](rg::RgPassBuilder& pb) {
                pb.Read(hdrRes, RHIResourceState::ShaderResource);
                pb.Write(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
                    auto outTex = res.GetTexture(bbRes);

                    RHIRenderPassDesc passDesc{};
                    passDesc.colorAttachmentCount = 1;
                    passDesc.colorAttachments[0].texture = outTex;
                    passDesc.colorAttachments[0].clear   = false;
                    passDesc.hasDepth = false;
                    cmd->BeginRenderPass(passDesc);

                    // Passthrough blit — bypass ACES, no exposure, just saturate
                    cmd->SetPipeline(tonemapPSO);
                    cmd->SetRootCBV(kSlotPerObject, blitAlloc.bufferHandle,
                                    blitAlloc.offsetBytes, blitAlloc.cpuPtr, sizeof(ToneMapCB));

                    if (bindless) {
                        uint32_t srvIdx = device->GetTextureSRVIndex(res.GetTexture(hdrRes));
                        cmd->SetRootConstants(kSlotRootConstants, &srvIdx, 1, 0);
                    } else {
                        cmd->SetFragmentTexture(0, res.GetTexture(hdrRes));
                    }

                    RHIViewport vp{};
                    vp.width    = static_cast<float>(w);
                    vp.height   = static_cast<float>(h);
                    vp.maxDepth = 1.0f;
                    cmd->SetViewport(vp);

                    RHIScissorRect sc{};
                    sc.right  = static_cast<int32_t>(w);
                    sc.bottom = static_cast<int32_t>(h);
                    cmd->SetScissor(sc);

                    cmd->Draw(3);
                    cmd->EndRenderPass();
                });
            });
        }

        // ── Screenshot ───────────────────────────────────────────────
        if (screenshotPending && frameIndex == config.screenshotFrame) {
            rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
                pb.Read(bbRes, RHIResourceState::RenderTarget);
                pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                    screenshotCapture.CaptureBackBuffer(cmdList, swapChain);
                    capturedThisFrame = true;
                });
            });
        }

        // ── Present barrier ──────────────────────────────────────────
        rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
            pb.Write(bbRes, RHIResourceState::Present);
            pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
        });

        rgBuilder.Compile();
        rgBuilder.Execute(cmdList);
        cmdList->End();
        }

        // ── Execute + Present ────────────────────────────────────────
        {
        UZ_PROFILE_SCOPE(ExecuteAndPresent);
        cmdList->Execute();
        swapChain->Present(false);
        globalFenceValue++;
        fenceValues[allocFrame] = globalFenceValue;
        frameFences[allocFrame]->Signal(globalFenceValue);
        }

        if (capturedThisFrame) {
            frameFences[allocFrame]->WaitCPU(globalFenceValue);
            screenshotCapture.SaveToPNG(config.screenshotPath);
            screenshotPending = false;
        }

        frameIndex++;
        UZ_PROFILE_FRAME_MARK();
    }

    // ── Cleanup ──────────────────────────────────────────────────────────
    device->WaitIdle();

    auto endTime = std::chrono::high_resolution_clock::now();
    float totalMs = std::chrono::duration<float, std::milli>(endTime - startTime).count();
    float avgMs = frameIndex > 0 ? totalMs / static_cast<float>(frameIndex) : 0.0f;

    UZ_LOG(PostProcessDemo, Info,
        "Demo finished: {} frames, {:.1f}ms total, {:.2f}ms/frame ({:.0f} FPS)",
        frameIndex, totalMs, avgMs, avgMs > 0 ? 1000.0f / avgMs : 0.0f);

    screenshotCapture.Shutdown();
    editorGrid.Shutdown();
    cbRing.Shutdown();

    device->DestroyTexture(depthBuffer);
    device->DestroyTexture(hdrTarget);
    device->DestroyTexture(tonemapTarget);
    if (checkerTexHandle.IsValid()) device->DestroyTexture(checkerTexHandle);
    device->DestroyBuffer(vbHandle);
    device->DestroyBuffer(ibHandle);
    device->DestroyPipeline(meshPipeline);
    device->DestroyShader(meshVS);
    device->DestroyShader(meshPS);

    for (uint32_t i = 0; i < kFramesInFlight; ++i) {
        device->DestroyFence(frameFences[i]);
    }
    device->DestroyCommandList(cmdList);
    device->DestroySwapChain(swapChain);
    DestroyRHIDevice(device);

    return true;
}

} // namespace uz
