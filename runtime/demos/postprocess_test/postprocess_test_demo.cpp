/// Post-Process Test Demo Implementation — DemoApplication-based
///
/// Renders 3 HDR spheres into RGBA16Float, then applies tonemap + FXAA chain.
/// All GPU interaction flows through abstract RHI interfaces.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "postprocess_test_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "platform_window.h"
#include "input.h"
#include "log.h"
#include "profiler.h"
#include "vertex.h"
#include "mesh.h"
#include "upload_context.h"
#include "constant_buffers.h"
#include "camera.h"
#include "fps_camera_controller.h"
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "engine_shaders.h"
#include "paths.h"
#include "editor_grid.h"
#include "screenshot_capture.h"
#include "cb_ring_allocator.h"
#include "texture_loader.h"
#include "root_slots.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"

#include <cmath>
#include <cstring>
#include <memory>

namespace uz {

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
struct LightDataGPU {
    Vec3  positionOrDir;
    float range;
    Vec3  color;
    float lightType;
    Vec3  spotDirection;
    float innerConeCos;
    float outerConeCos;
    float _pad0 = 0.f;
    float _pad1 = 0.f;
    float _pad2 = 0.f;
};

static_assert(sizeof(LightDataGPU) == 64, "LightDataGPU must be 64 bytes");

static constexpr uint32_t kMeshMaxLights = 8;

/// Per-frame constant buffer matching the 768-byte layout expected by mesh_ps.hlsl.
struct PerFrameMultiLightCB {
    Mat4         viewProj;
    Vec4         cameraPos;
    Vec4         timeParams;
    Vec4         ambientColor;
    uint32_t     lightCount = 0;
    uint32_t     _pad0a = 0;
    uint32_t     _pad0b = 0;
    uint32_t     _pad0c = 0;
    LightDataGPU lights[kMeshMaxLights];
    Vec4         _pad1[8] = {};
};

static_assert(sizeof(PerFrameMultiLightCB) == 768,
              "PerFrameMultiLightCB must be 768 bytes");

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
// Constructor / Destructor
// ─────────────────────────────────────────────────────────────────────────────

PostProcessTestDemo::PostProcessTestDemo(const PostProcessTestConfig& config)
    : DemoApplication(DemoConfig{
          .title = "UzEngine — Post-Process Test (T=tonemap A=fxaa)",
          .width = 1280,
          .height = 720,
          .maxFrames = config.frameCount,
          .vsync = false,
          .enableValidation = true,
      })
    , config_(config)
    , enableTonemap_(config.enableTonemap)
    , enableFXAA_(config.enableFXAA)
{
}

PostProcessTestDemo::~PostProcessTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::OnInit() {
    UZ_PROFILE_FUNCTION();

    auto* device = GetDevice();
    bindless_ = device->IsBindless();

    startTime_ = std::chrono::high_resolution_clock::now();

    CompileShaders();
    CreatePipelines();
    LoadMeshData();
    LoadTextures();
    CreateRenderTargets(GetWidth(), GetHeight());

    // CB ring
    cbRing_ = std::make_unique<CBRingAllocator>();
    cbRing_->Init(device, 3, 64 * 1024);

    // Editor grid
    editorGrid_ = std::make_unique<EditorGrid>();
    if (!editorGrid_->Init(device, *shaderPipeline_, RHIFormat::RGBA16Float, RHIFormat::D32Float)) {
        UZ_LOG(PostProcessDemo, Error, "Failed to initialize editor grid");
    }

    // Input
    input_ = std::make_unique<InputSystem>();
    input_->Init();
    GetWindow()->SetInputSystem(input_.get());

    // Camera
    constexpr float kHFovDeg = 90.f;
    constexpr float kAspect  = 16.f / 9.f;
    float vFovRad = 2.f * std::atan(std::tan(kHFovDeg * 0.5f * 3.14159265f / 180.f) / kAspect);
    camera_.SetPerspective(vFovRad,
        static_cast<float>(GetWidth()) / static_cast<float>(GetHeight()),
        0.1f, 1000.f);
    camera_.SetPosition({0.f, 5.f, 2.5f});
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
        Vec3 pos = camera_.GetPosition();
        camera_.LookAt(pos + forward, Vec3::Up());
    }
    cameraCtrl_.InitFromCamera(camera_);
    cameraCtrl_.SetMoveSpeed(10.f);

    // Screenshot
    screenshotPending_ = !config_.screenshotPath.empty();
    if (screenshotPending_) {
        screenshotCapture_ = std::make_unique<ScreenshotCapture>();
        screenshotCapture_->Init(device, GetWidth(), GetHeight());
    }

    // Render graph pool
    rgPool_ = std::make_unique<rg::RgResourcePool>(device);

    UZ_LOG(PostProcessDemo, Info,
        "PostProcessTestDemo initialized — T=tonemap A=fxaa toggles | Scene3D -> ToneMap -> FXAA -> display");
}

// ─────────────────────────────────────────────────────────────────────────────
// CompileShaders
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::CompileShaders() {
    auto* device = GetDevice();

    compiler_ = std::make_unique<ShaderCompiler>();
    if (!compiler_->Init()) {
        UZ_LOG(PostProcessDemo, Error, "Failed to init DXC shader compiler");
        return;
    }

    if (!bindless_) {
        compilerFXC_ = std::make_unique<ShaderCompilerFXC>();
        compilerFXC_->Init();
    }

    shaderPipeline_ = std::make_unique<ShaderPipeline>();
    ShaderPipeline::Config pipeCfg;
    pipeCfg.cacheDir      = (uz::GetExeDir() / "cache/shaders").string();
    pipeCfg.targetBackend = bindless_ ? "dx12_sm66" : "dx12_sm50";
    pipeCfg.bindless      = bindless_;
    shaderPipeline_->Init(pipeCfg);
    shaderPipeline_->SetCompiler(compiler_.get());
    if (compilerFXC_) shaderPipeline_->SetCompilerFXC(compilerFXC_.get());

    auto loadShader = [&](EngineShader shader) {
        return shaderPipeline_->LoadBytecodeFromSource(
            EngineShaders::GetName(shader), EngineShaders::GetSource(shader));
    };

    auto fsVSBytes    = loadShader(EngineShader::FullscreenVS);
    auto tonemapBytes = loadShader(EngineShader::TonemapPS);
    auto fxaaBytes    = loadShader(EngineShader::FxaaPS);
    auto meshVSBytes  = loadShader(EngineShader::MeshVS);
    auto meshPSBytes  = loadShader(EngineShader::MeshPS);

    uint32_t compiled = 0;
    if (!fsVSBytes.empty())    ++compiled;
    if (!tonemapBytes.empty()) ++compiled;
    if (!fxaaBytes.empty())    ++compiled;
    if (!meshVSBytes.empty())  ++compiled;
    if (!meshPSBytes.empty())  ++compiled;
    UZ_LOG(PostProcessDemo, Info, "Shader compilation: {}/5 succeeded", compiled);

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

    fsVS_      = makeShader(fsVSBytes,    RHIShaderStage::Vertex, "fullscreen_vs");
    tonemapPS_ = makeShader(tonemapBytes, RHIShaderStage::Pixel,  "tonemap_ps");
    fxaaPS_    = makeShader(fxaaBytes,    RHIShaderStage::Pixel,  "fxaa_ps");
    meshVS_    = makeShader(meshVSBytes,  RHIShaderStage::Vertex, "mesh_vs");
    meshPS_    = makeShader(meshPSBytes,  RHIShaderStage::Pixel,  "mesh_ps");
}

// ─────────────────────────────────────────────────────────────────────────────
// CreatePipelines
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::CreatePipelines() {
    auto* device = GetDevice();

    tonemapPSO_ = CreateFullscreenPSO(device, fsVS_, tonemapPS_,
                                       RHIFormat::RGBA8Unorm, "ToneMap_PSO");
    fxaaPSO_    = CreateFullscreenPSO(device, fsVS_, fxaaPS_,
                                       RHIFormat::RGBA8Unorm, "FXAA_PSO");

    // Mesh pipeline — renders into RGBA16Float HDR target
    RHIInputElementDesc inputElements[] = {
        { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
        { RHIInputSemantic::Normal,   0, RHIFormat::RGB32Float,  0, 12 },
        { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 24 },
        { RHIInputSemantic::Tangent,  0, RHIFormat::RGBA32Float, 0, 32 },
    };

    RHIGraphicsPipelineDesc meshPipeDesc{};
    meshPipeDesc.vertexShader    = meshVS_;
    meshPipeDesc.pixelShader     = meshPS_;
    meshPipeDesc.topology        = RHIPrimitiveTopology::TriangleList;
    meshPipeDesc.rasterizer.cullMode              = RHICullMode::Back;
    meshPipeDesc.rasterizer.frontCounterClockwise = false;
    meshPipeDesc.depthStencil.depthEnable         = true;
    meshPipeDesc.depthStencil.depthWrite          = true;
    meshPipeDesc.renderTargetFormats[0]           = RHIFormat::RGBA16Float;
    meshPipeDesc.renderTargetCount                = 1;
    meshPipeDesc.depthStencilFormat               = RHIFormat::D32Float;
    meshPipeDesc.debugName                        = "MeshPipeline_HDR";

    if (!bindless_) {
        meshPipeDesc.inputElements     = inputElements;
        meshPipeDesc.inputElementCount = 4;
        meshPipeDesc.vertexStride      = sizeof(Vertex);
    }

    meshPipeline_ = device->CreateGraphicsPipeline(meshPipeDesc);
    if (!meshPipeline_.IsValid()) {
        UZ_LOG(PostProcessDemo, Error, "Failed to create mesh pipeline");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshData
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::LoadMeshData() {
    auto* device = GetDevice();

    sphereMesh_ = std::make_unique<Mesh>(MeshBuilder::CreateSphere(1.0f, 32, 64));
    UZ_LOG(PostProcessDemo, Info, "Created UV sphere: {} vertices, {} indices",
           sphereMesh_->GetVertexCount(), sphereMesh_->GetIndexCount());

    // Upload vertex buffer
    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sphereMesh_->GetVertexDataSize();
    vbDesc.stride    = sphereMesh_->header.vertexStride;
    vbDesc.usage     = bindless_ ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory    = RHIMemoryType::Upload;
    vbDesc.debugName = "PostProcess_VB";

    vbHandle_ = device->CreateBuffer(vbDesc);
    void* vbMapped = device->MapBuffer(vbHandle_);
    std::memcpy(vbMapped, sphereMesh_->vertexData.data(), sphereMesh_->GetVertexDataSize());
    device->UnmapBuffer(vbHandle_);

    if (bindless_) {
        vbSrvIndex_ = device->GetBufferSRVIndex(vbHandle_);
    }

    // Upload index buffer
    RHIBufferDesc ibDesc{};
    ibDesc.sizeBytes = sphereMesh_->GetIndexDataSize();
    ibDesc.stride    = (sphereMesh_->header.indexFormat == MeshIndexFormat::UInt32) ? 4u : 2u;
    ibDesc.usage     = RHIBufferUsage::IndexBuffer;
    ibDesc.memory    = RHIMemoryType::Upload;
    ibDesc.debugName = "PostProcess_IB";

    ibHandle_ = device->CreateBuffer(ibDesc);
    void* ibMapped = device->MapBuffer(ibHandle_);
    std::memcpy(ibMapped, sphereMesh_->indexData.data(), sphereMesh_->GetIndexDataSize());
    device->UnmapBuffer(ibHandle_);

    indexFormat_ = (sphereMesh_->header.indexFormat == MeshIndexFormat::UInt32)
        ? RHIIndexFormat::Uint32 : RHIIndexFormat::Uint16;
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadTextures
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::LoadTextures() {
    auto* device = GetDevice();
    auto* cmdList = GetCommandList();

    std::string texPath = (uz::GetExeDir() / "assets/checker.png").string();
    auto result = TextureLoader::Load(device, cmdList, texPath);
    if (result.handle.IsValid()) {
        checkerHandle_ = result.handle;
        checkerSrvIndex_ = result.srvIndex;
    } else {
        UZ_LOG(PostProcessDemo, Warning, "checker.png not found — spheres will be white");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateRenderTargets / RecreateRenderTargets
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::CreateRenderTargets(uint32_t width, uint32_t height) {
    auto* device = GetDevice();
    const RHITextureUsage kRTUsage = RHITextureUsage::ShaderResource | RHITextureUsage::RenderTarget;

    // Depth buffer
    RHITextureDesc depthDesc{};
    depthDesc.width     = width;
    depthDesc.height    = height;
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "PostProcess_Depth";
    depthBuffer_ = device->CreateTexture(depthDesc);

    // HDR scene target
    RHITextureDesc hdrDesc{};
    hdrDesc.width     = width;
    hdrDesc.height    = height;
    hdrDesc.format    = RHIFormat::RGBA16Float;
    hdrDesc.usage     = kRTUsage;
    hdrDesc.debugName = "HDR_Scene";
    hdrTarget_ = device->CreateTexture(hdrDesc);

    // Tonemap output
    RHITextureDesc tmDesc{};
    tmDesc.width     = width;
    tmDesc.height    = height;
    tmDesc.format    = RHIFormat::RGBA8Unorm;
    tmDesc.usage     = kRTUsage;
    tmDesc.debugName = "ToneMap_Out";
    tonemapTarget_ = device->CreateTexture(tmDesc);

    UZ_LOG(PostProcessDemo, Info, "Created render targets: HDR (RGBA16F) + ToneMap (RGBA8) @ {}x{}", width, height);
}

void PostProcessTestDemo::RecreateRenderTargets(uint32_t width, uint32_t height) {
    auto* device = GetDevice();
    device->DestroyTexture(depthBuffer_);
    device->DestroyTexture(hdrTarget_);
    device->DestroyTexture(tonemapTarget_);
    CreateRenderTargets(width, height);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::OnFrame(float dt) {
    UZ_PROFILE_FUNCTION();

    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();
    auto* swapChain = GetSwapChain();
    const uint32_t w = GetWidth();
    const uint32_t h = GetHeight();

    auto now = std::chrono::high_resolution_clock::now();
    totalTime_ = std::chrono::duration<float>(now - startTime_).count();

    // ── Input ────────────────────────────────────────────────────────────
    input_->BeginFrame();
    if (input_->IsKeyJustPressed(Key::T)) {
        enableTonemap_ = !enableTonemap_;
        UZ_LOG(PostProcessDemo, Info, "Tone mapping: {}", enableTonemap_ ? "ON" : "OFF");
    }
    if (input_->IsKeyJustPressed(Key::A)) {
        enableFXAA_ = !enableFXAA_;
        UZ_LOG(PostProcessDemo, Info, "FXAA: {}", enableFXAA_ ? "ON" : "OFF");
    }

    // ── Camera ───────────────────────────────────────────────────────────
    cameraCtrl_.Update(camera_, *input_, dt);

    // ── CB Ring ──────────────────────────────────────────────────────────
    cbRing_->BeginFrame(GetFrameIndex());

    // ── Upload PerFrameMultiLightCB ──────────────────────────────────────
    PerFrameMultiLightCB perFrame{};
    perFrame.viewProj = camera_.GetViewProjection();
    Vec3 camPos = camera_.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};
    perFrame.ambientColor = {0.08f, 0.08f, 0.1f, 0.f};

    // Sun: warm directional, very high intensity for HDR demo
    perFrame.lightCount = 2;

    LightDataGPU& sun = perFrame.lights[0];
    Vec3 rawDir = {0.5f, 1.0f, 0.3f};
    float len = std::sqrt(rawDir.x * rawDir.x + rawDir.y * rawDir.y + rawDir.z * rawDir.z);
    sun.positionOrDir = {rawDir.x / len, rawDir.y / len, rawDir.z / len};
    sun.range     = 0.f;
    sun.color     = {50.0f, 40.0f, 30.0f};
    sun.lightType = 0.f;

    // Fill light: cool blue from opposite side
    LightDataGPU& fill = perFrame.lights[1];
    Vec3 fillDir = {-0.3f, 0.5f, -0.8f};
    float fillLen = std::sqrt(fillDir.x * fillDir.x + fillDir.y * fillDir.y + fillDir.z * fillDir.z);
    fill.positionOrDir = {fillDir.x / fillLen, fillDir.y / fillLen, fillDir.z / fillLen};
    fill.range     = 0.f;
    fill.color     = {5.0f, 8.0f, 20.0f};
    fill.lightType = 0.f;

    CBAllocation perFrameAlloc = cbRing_->Allocate(sizeof(PerFrameMultiLightCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameMultiLightCB));

    // ── Upload PerObjectCBs (3 spheres) ──────────────────────────────────
    static constexpr uint32_t kSphereCount = 3;
    struct SphereInstance { Vec3 pos; float scale; };
    SphereInstance spheres[kSphereCount] = {
        {{-2.5f, 0.f, 0.f}, 1.0f},
        {{ 0.0f, 0.f, 0.f}, 1.0f},
        {{ 2.5f, 0.f, 0.f}, 1.0f},
    };

    CBAllocation perObjectAllocs[kSphereCount];
    for (uint32_t ci = 0; ci < kSphereCount; ++ci) {
        PerObjectCB perObject{};
        perObject.model        = Mat4::TRS(spheres[ci].pos, spheres[ci].scale);
        perObject.normalMatrix = perObject.model.NormalMatrix();
        perObject.albedoIndex  = checkerSrvIndex_;
        perObjectAllocs[ci] = cbRing_->Allocate(sizeof(PerObjectCB));
        std::memcpy(perObjectAllocs[ci].cpuPtr, &perObject, sizeof(PerObjectCB));
    }

    // ── Upload post-process CB data ──────────────────────────────────────
    ToneMapCB tmData{};
    tmData.tonemapIntensity = 1.0f;
    tmData.exposureAdjust   = 2.0f;
    tmData.bypassTonemap    = 0.0f;
    auto tmAlloc = cbRing_->Allocate(sizeof(ToneMapCB));
    std::memcpy(tmAlloc.cpuPtr, &tmData, sizeof(ToneMapCB));

    // Passthrough blit CB — no exposure boost, no ACES, just saturate
    ToneMapCB blitData{};
    blitData.tonemapIntensity = 1.0f;
    blitData.exposureAdjust   = 0.0f;
    blitData.bypassTonemap    = 1.0f;
    auto blitAlloc = cbRing_->Allocate(sizeof(ToneMapCB));
    std::memcpy(blitAlloc.cpuPtr, &blitData, sizeof(ToneMapCB));

    FXAACB fxData{};
    fxData.invScreenWidth  = 1.0f / static_cast<float>(w);
    fxData.invScreenHeight = 1.0f / static_cast<float>(h);
    fxData.edgeThreshold   = 0.1f;
    fxData.blurStrength    = 1.0f;
    auto fxAlloc = cbRing_->Allocate(sizeof(FXAACB));
    std::memcpy(fxAlloc.cpuPtr, &fxData, sizeof(FXAACB));

    // ── Build render graph ───────────────────────────────────────────────
    bool capturedThisFrame = false;

    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(device, rgPool_.get());

    auto bbRes  = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    auto hdrRes = rgBuilder.ImportTexture("HDR_Scene", hdrTarget_, RHIResourceState::Common);
    auto tmRes  = rgBuilder.ImportTexture("ToneMap_Out", tonemapTarget_, RHIResourceState::Common);

    // ── Pass 1: Scene3D — render lit spheres + grid into HDR target ──────
    rgBuilder.AddPass("Scene3D", [&](rg::RgPassBuilder& pb) {
        pb.Write(hdrRes, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            UZ_PROFILE_SCOPE("Scene3D::Execute");

            auto hdr = res.GetTexture(hdrRes);

            float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
            cmd->ClearRenderTarget(hdr, clearColor);
            cmd->ClearDepthStencil(depthBuffer_, 1.0f, 0);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount = 1;
            passDesc.colorAttachments[0].texture = hdr;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth = true;
            passDesc.depthAttachment.texture    = depthBuffer_;
            passDesc.depthAttachment.clearDepth = 1.0f;
            passDesc.depthAttachment.clear      = false;
            cmd->BeginRenderPass(passDesc);

            RHIViewport vp{};
            vp.width    = static_cast<float>(w);
            vp.height   = static_cast<float>(h);
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(w);
            sc.bottom = static_cast<int32_t>(h);
            cmd->SetScissor(sc);

            cmd->SetPipeline(meshPipeline_);

            if (bindless_) {
                cmd->SetDescriptorIndex(0, vbSrvIndex_);
            } else {
                cmd->SetVertexBuffer(0, vbHandle_, 0);
            }

            cmd->SetRootCBV(kSlotPerFrame, perFrameAlloc.bufferHandle,
                            perFrameAlloc.offsetBytes, perFrameAlloc.cpuPtr,
                            sizeof(PerFrameMultiLightCB));

            cmd->SetIndexBuffer(ibHandle_, indexFormat_, 0);

            // Bind checker texture for SM5 (t0 = albedo)
            if (!bindless_ && checkerHandle_.IsValid()) {
                cmd->SetFragmentTexture(0, checkerHandle_);
            }

            for (uint32_t ci = 0; ci < kSphereCount; ++ci) {
                cmd->SetRootCBV(kSlotPerObject, perObjectAllocs[ci].bufferHandle,
                                perObjectAllocs[ci].offsetBytes, perObjectAllocs[ci].cpuPtr,
                                sizeof(PerObjectCB));
                cmd->DrawIndexed(sphereMesh_->GetIndexCount());
            }

            editorGrid_->Draw(cmd, *cbRing_, camera_);

            cmd->EndRenderPass();
        });
    });

    // ── Pass 2: Tone mapping (HDR -> LDR) ────────────────────────────────
    auto tmOutputRes = enableFXAA_ ? tmRes : bbRes;

    if (enableTonemap_) {
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

                cmd->SetPipeline(tonemapPSO_);
                cmd->SetRootCBV(kSlotPerObject, tmAlloc.bufferHandle,
                                tmAlloc.offsetBytes, tmAlloc.cpuPtr, sizeof(ToneMapCB));

                if (bindless_) {
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

    // ── Pass 3: FXAA (edge AA) ───────────────────────────────────────────
    if (enableFXAA_) {
        auto fxaaInputRes = enableTonemap_ ? tmRes : hdrRes;
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

                cmd->SetPipeline(fxaaPSO_);
                cmd->SetRootCBV(kSlotPerObject, fxAlloc.bufferHandle,
                                fxAlloc.offsetBytes, fxAlloc.cpuPtr, sizeof(FXAACB));

                if (bindless_) {
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
    } else if (!enableTonemap_) {
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

                cmd->SetPipeline(tonemapPSO_);
                cmd->SetRootCBV(kSlotPerObject, blitAlloc.bufferHandle,
                                blitAlloc.offsetBytes, blitAlloc.cpuPtr, sizeof(ToneMapCB));

                if (bindless_) {
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

    // ── Screenshot ───────────────────────────────────────────────────────
    if (screenshotPending_ && GetFrameIndex() == config_.screenshotFrame) {
        rgBuilder.AddPass("Screenshot", [&](rg::RgPassBuilder& pb) {
            pb.Read(bbRes, RHIResourceState::RenderTarget);
            pb.SetExecutionCallback([&](IRHICommandList*, const rg::RgPassResources&) {
                screenshotCapture_->CaptureBackBuffer(cmdList, swapChain);
                capturedThisFrame = true;
            });
        });
    }

    // ── Present barrier ──────────────────────────────────────────────────
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // ── Deferred screenshot save ─────────────────────────────────────────
    if (screenshotSaveDeferred_) {
        screenshotCapture_->SaveToPNG(config_.screenshotPath);
        screenshotPending_ = false;
        screenshotSaveDeferred_ = false;
    }
    if (capturedThisFrame) {
        screenshotSaveDeferred_ = true;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::OnResize(uint32_t width, uint32_t height) {
    RecreateRenderTargets(width, height);
    camera_.SetAspectRatio(static_cast<float>(width) / static_cast<float>(height));
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown
// ─────────────────────────────────────────────────────────────────────────────

void PostProcessTestDemo::OnShutdown() {
    auto* device = GetDevice();

    rgPool_.reset();

    if (screenshotCapture_) screenshotCapture_->Shutdown();
    if (editorGrid_) editorGrid_->Shutdown();
    if (cbRing_) cbRing_->Shutdown();

    device->DestroyTexture(depthBuffer_);
    device->DestroyTexture(hdrTarget_);
    device->DestroyTexture(tonemapTarget_);
    if (checkerHandle_.IsValid()) device->DestroyTexture(checkerHandle_);

    device->DestroyBuffer(vbHandle_);
    device->DestroyBuffer(ibHandle_);

    device->DestroyPipeline(tonemapPSO_);
    device->DestroyPipeline(fxaaPSO_);
    device->DestroyPipeline(meshPipeline_);

    device->DestroyShader(fsVS_);
    device->DestroyShader(tonemapPS_);
    device->DestroyShader(fxaaPS_);
    device->DestroyShader(meshVS_);
    device->DestroyShader(meshPS_);
}

} // namespace uz
