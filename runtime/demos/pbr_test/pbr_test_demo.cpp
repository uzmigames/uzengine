/// PBR Test Demo Implementation — Backend-Agnostic
///
/// Supports two render paths, toggled at runtime with the F key:
///
/// **Deferred** (default):
///   Pass 1 — G-Buffer: 5 cubes + ground plane -> albedo (RGBA8), normal (RGBA16F),
///             metallic/roughness (RGBA8), depth (D32F).
///   Pass 2 — Lighting (or Debug): fullscreen triangle reads G-Buffer,
///             accumulates 4 coloured point lights + directional sun, or
///             visualises a single G-Buffer channel when debug mode is active.
///
/// **Forward**:
///   Pass 1 — Forward PBR: 5 cubes + ground plane rendered directly to backbuffer with
///             per-pixel Cook-Torrance lighting in a single pass.
///
/// Both paths share:
///   Grid + Stats overlay composited on the backbuffer, then present barrier.
///
/// All GPU interaction flows through abstract RHI interfaces — no DX12,
/// Vulkan, or Metal types appear in this file.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "pbr_test_demo.h"

#include <chrono>
#include <cstdlib>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr int      kCubeCount       = 5;
static constexpr uint32_t kPointLightCount = 4;
static constexpr uint32_t kShadowResolution = 2048;
static constexpr uint32_t kShadowAtlasSize  = kShadowResolution * 2;

// ─────────────────────────────────────────────────────────────────────────────
// Scene data
// ─────────────────────────────────────────────────────────────────────────────

static constexpr Vec3 kCubePositions[kCubeCount] = {
    {-4.f, 0.f, 0.f},
    {-2.f, 0.f, 0.f},
    { 0.f, 0.f, 0.f},
    { 2.f, 0.f, 0.f},
    { 4.f, 0.f, 0.f},
};
static constexpr float kCubeScale = 0.8f;

// ─────────────────────────────────────────────────────────────────────────────
// PBRTestDemo — Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

PBRTestDemo::PBRTestDemo(const PBRTestConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 PBR Test (Cook-Torrance)",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.frameCount,
        .vsync  = false,
        .screenshotPath = config.screenshotPath,
        .screenshotFrame = config.screenshotFrame,
        .backend = config.backend,
        .shaderModel = config.shaderModel,
    })
    , config_(config)
{}

PBRTestDemo::~PBRTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit — Create demo-specific rendering resources
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnInit() {
    startTime_ = std::chrono::high_resolution_clock::now();

    auto* device = GetDevice();

    // Command list is already in recording state (base class called Begin())
    LoadMeshData();
    LoadTextures();
    CreateGBuffer();

    // ── Shadow atlas texture ─────────────────────────────────────────────
    RHITextureDesc shadowAtlasDesc{};
    shadowAtlasDesc.width     = kShadowAtlasSize;
    shadowAtlasDesc.height    = kShadowAtlasSize;
    shadowAtlasDesc.format    = RHIFormat::D32Float;
    shadowAtlasDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    shadowAtlasDesc.debugName = "ShadowAtlas";
    shadowAtlas_    = device->CreateTexture(shadowAtlasDesc);
    shadowAtlasSrv_ = device->GetTextureSRVIndex(shadowAtlas_);

    // ── Render state ─────────────────────────────────────────────────────
    debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    renderPath_ = GetDefaultRenderPath();

    UZ_LOG(Renderer, Info, "PBR test demo started — 5 cubes, {} point lights + directional sun "
           "(F=toggle render path, current={})", kPointLightCount, RenderPathToString(renderPath_));

    // Signal that all resources are loaded — screenshots can now be captured.
    SetReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshData
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::LoadMeshData() {
    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cpuMesh = LoadMesh(meshPath);
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to load cube.uzmesh from '{}'", meshPath);
        return;
    }
    // Command list is already in recording state (base class called Begin() before OnInit)
    auto* cmdList = GetCommandList();
    cubeMesh_ = UploadMesh(GetDevice(), cmdList, cpuMesh);
    if (!cubeMesh_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to upload cube mesh to GPU");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadTextures
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::LoadTextures() {
    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    auto loadTex = [&](const char* file) {
        std::string path = (uz::GetExeDir() / "assets" / file).string();
        return TextureLoader::Load(device, cmdList, path);
    };

    albedoTex_    = loadTex("Stonebricks_BaseColor.jpg");
    normalTex_    = loadTex("Stonebricks_Normal.jpg");
    roughnessTex_ = loadTex("Stonebricks_Roughness.jpg");
    metallicTex_  = loadTex("Stonebricks_Metallic.jpg");
    aoTex_        = loadTex("Stonebricks_AO.jpg");
    checkerTex_   = loadTex("checker.png");

    // ── Fallback textures for SM5/Vulkan non-bindless path ────────────────
    // The SM5 shader samples all PBR textures unconditionally, so every slot
    // must always have a valid texture. These 1×1 fallbacks produce neutral
    // defaults when an object has no material texture assigned.
    {
        RHITextureDesc fbDesc{};
        fbDesc.width     = 1;
        fbDesc.height    = 1;
        fbDesc.format    = RHIFormat::RGBA8Unorm;
        fbDesc.usage     = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;

        fbDesc.debugName = "FallbackWhite";
        fallbackWhite_   = device->CreateTexture(fbDesc);

        fbDesc.debugName = "FallbackBlack";
        fallbackBlack_   = device->CreateTexture(fbDesc);

        fbDesc.debugName = "FallbackNormal";
        fallbackNormal_  = device->CreateTexture(fbDesc);

        static constexpr uint8_t kWhitePixel[4]  = {255, 255, 255, 255};
        static constexpr uint8_t kBlackPixel[4]  = {  0,   0,   0, 255}; // metallic=0 fallback
        static constexpr uint8_t kNormalPixel[4] = {128, 128, 255, 255}; // (0,0,1) tangent-space

        UploadContext upload(device);
        upload.UploadTexture(cmdList, fallbackWhite_,  kWhitePixel,  sizeof(kWhitePixel));
        upload.UploadTexture(cmdList, fallbackBlack_,  kBlackPixel,  sizeof(kBlackPixel));
        upload.UploadTexture(cmdList, fallbackNormal_, kNormalPixel, sizeof(kNormalPixel));
        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();
        cmdList->Begin();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateGBuffer / RecreateGBuffer
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::CreateGBuffer() {
    if (!gbuffer_.Create(GetDevice(), GetWidth(), GetHeight())) {
        UZ_LOG(Renderer, Error, "Failed to create G-Buffer");
    }
}

void PBRTestDemo::RecreateGBuffer(uint32_t width, uint32_t height) {
    if (!gbuffer_.Resize(GetDevice(), width, height)) {
        UZ_LOG(Renderer, Error, "Failed to recreate G-Buffer on resize");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnResize(uint32_t width, uint32_t height) {
    RecreateGBuffer(width, height);
    // Camera aspect ratio and stats overlay are updated by the base class
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame — Record one frame's rendering commands
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnFrame(float dt) {
    auto* device    = GetDevice();
    auto* cmdList   = GetCommandList();
    auto* swapChain = GetSwapChain();
    auto* cbRing    = GetCBRing();
    auto& camera    = GetCamera();
    auto* input     = GetInput();

    const bool bindless = device->IsBindless();

    auto now = std::chrono::high_resolution_clock::now();
    totalTime_ = std::chrono::duration<float>(now - startTime_).count();

    // ── Stats ────────────────────────────────────────────────────────────
    GetStatsOverlay()->BeginFrame(dt);

    // ── Input ────────────────────────────────────────────────────────────
    input->BeginFrame();

    // Debug mode toggle: '0' = off, '1'-'7' = G-Buffer channels 0-6
    if (input->IsKeyJustPressed(Key::Num0)) {
        debugModeValue_ = static_cast<uint32_t>(GBufferDebugMode::Count);
    }
    for (uint32_t k = 0; k < 7; ++k) {
        if (input->IsKeyJustPressed(static_cast<Key>(static_cast<uint32_t>(Key::Num1) + k))) {
            debugModeValue_ = k;
        }
    }

    // Render path toggle: F switches between Deferred and Forward
    if (input->IsKeyJustPressed(Key::F)) {
        renderPath_ = (renderPath_ == RenderPath::Deferred)
            ? RenderPath::Forward : RenderPath::Deferred;
        UZ_LOG(Renderer, Info, "Render path switched to: {}",
               RenderPathToString(renderPath_));
    }

    // ── Camera Update ────────────────────────────────────────────────────
    GetCameraController().Update(camera, *input, dt);

    // ── Upload PerFrameCB ────────────────────────────────────────────────
    cbRing->BeginFrame(GetFrameIndex());

    PerFrameCB perFrame{};
    perFrame.viewProj    = camera.GetViewProjection();
    perFrame.invViewProj = perFrame.viewProj.Inverse();
    Vec3 camPos          = camera.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};

    Vec3 sunDir           = Vec3{0.5f, 0.3f, 0.8f}.Normalized();
    perFrame.lightDir     = {sunDir.x, sunDir.y, sunDir.z, 0.f};
    perFrame.lightColor   = {3.0f, 2.85f, 2.7f, 0.f};
    perFrame.ambientColor = {0.03f, 0.03f, 0.05f, 0.f};

    CBAllocation perFrameAlloc = cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── LightingCB — 4 coloured point lights ────────────────────────────
    LightingCB lightingData{};
    lightingData.pointLightCount = kPointLightCount;
    lightingData.pointLights[0] = { {-3.f, 2.f,  2.f}, 8.f,  {4.f, 0.2f, 0.2f}, 0.f };
    lightingData.pointLights[1] = { { 0.f, 3.f, -2.f}, 10.f, {0.2f, 4.f, 0.2f}, 0.f };
    lightingData.pointLights[2] = { { 3.f, 2.f,  2.f}, 8.f,  {0.2f, 0.2f, 4.f}, 0.f };
    lightingData.pointLights[3] = { { 0.f, 1.f,  0.f}, 15.f, {3.f,  3.f,  3.f}, 0.f };

    CBAllocation lightingAlloc = cbRing->Allocate(sizeof(LightingCB));
    std::memcpy(lightingAlloc.cpuPtr, &lightingData, sizeof(LightingCB));

    // ── Compute shadow cascades ──────────────────────────────────────────
    ShadowConfig shadowCfg{};
    shadowCfg.cascadeCount = 4;
    shadowCfg.resolution   = kShadowResolution;
    shadowCfg.maxDistance   = 50.0f;
    shadowCfg.splitLambda  = 0.75f;
    shadowCfg.normalBias   = 0.02f;
    shadowCfg.depthBias    = 0.005f;

    ShadowCascade cascades[kMaxShadowCascades];
    Vec3 lightShineDir = Vec3{-sunDir.x, -sunDir.y, -sunDir.z};
    ComputeCascades(camera.GetView(), camera.GetProjection(), lightShineDir, shadowCfg, cascades);

    // ── Upload ShadowDataCB ──────────────────────────────────────────────
    ShadowDataCB shadowData{};
    for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
        shadowData.cascadeViewProj[i] = cascades[i].viewProj;
    }
    shadowData.cascadeSplits   = {cascades[0].splitFar, cascades[1].splitFar,
                                  cascades[2].splitFar, cascades[3].splitFar};
    shadowData.shadowAtlasIndex = shadowAtlasSrv_;
    shadowData.cascadeCount     = shadowCfg.cascadeCount;
    shadowData.normalBias       = shadowCfg.normalBias;
    shadowData.depthBias        = shadowCfg.depthBias;
    shadowData.shadowMapSize    = static_cast<float>(kShadowResolution);

    CBAllocation shadowAlloc = cbRing->Allocate(sizeof(ShadowDataCB));
    std::memcpy(shadowAlloc.cpuPtr, &shadowData, sizeof(ShadowDataCB));

    // ── Build draw list ──────────────────────────────────────────────────
    DrawList drawList;
    drawList.Clear();
    for (int i = 0; i < kCubeCount; ++i) {
        DrawCommand cmd{};
        cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(i));
        cmd.worldMatrix  = Mat4::TRS(kCubePositions[i], kCubeScale);
        cmd.normalMatrix = cmd.worldMatrix.NormalMatrix();
        cmd.meshId             = 0;
        cmd.albedoSrvIndex     = albedoTex_.srvIndex;
        cmd.normalSrvIndex     = normalTex_.srvIndex;
        cmd.roughnessSrvIndex  = roughnessTex_.srvIndex;
        cmd.metallicSrvIndex   = metallicTex_.srvIndex;
        cmd.aoSrvIndex         = aoTex_.srvIndex;
        cmd.albedoTexture      = albedoTex_.handle;
        cmd.normalTexture      = normalTex_.handle;
        cmd.roughnessTexture   = roughnessTex_.handle;
        cmd.metallicTexture    = metallicTex_.handle;
        cmd.aoTexture          = aoTex_.handle;
        drawList.Add(cmd);
    }
    {
        DrawCommand ground{};
        ground.sortKey      = SortKey::Opaque(0, 0, 0.f, static_cast<uint32_t>(kCubeCount));
        ground.worldMatrix  = Mat4::TRS(
            Vec3{0.f, 0.f, -0.82f},
            Vec3{15.f, 15.f, 0.02f});
        ground.normalMatrix      = ground.worldMatrix.NormalMatrix();
        ground.meshId            = 0;
        ground.albedoSrvIndex    = checkerTex_.srvIndex;
        ground.normalSrvIndex    = 0;
        ground.roughnessSrvIndex = 0;
        ground.metallicSrvIndex  = 0;
        ground.aoSrvIndex        = 0;
        ground.albedoTexture     = checkerTex_.handle;
        // normal/roughness/metallic/ao left default (invalid) — SM5 path unbinds them
        drawList.Add(ground);
    }
    drawList.Sort();

    // ── Record frame via render graph ────────────────────────────────────
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(device, GetRgPool());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    auto albedoRes     = rgBuilder.ImportTexture("GBuf_Albedo",      gbuffer_.albedo,      RHIResourceState::ShaderResource);
    auto normalRes     = rgBuilder.ImportTexture("GBuf_Normal",      gbuffer_.normal,      RHIResourceState::ShaderResource);
    auto depthRes      = rgBuilder.ImportTexture("GBuf_Depth",       gbuffer_.depth,       RHIResourceState::ShaderResource);
    auto positionWSRes = rgBuilder.ImportTexture("GBuf_PositionWS",  gbuffer_.positionWS,  RHIResourceState::ShaderResource);

    auto* pipelines = GetPipelines();

    // ── Shadow pass ──────────────────────────────────────────────────────
    auto shadowAtlasRes = rgBuilder.ImportTexture("ShadowAtlas", shadowAtlas_, RHIResourceState::ShaderResource);
    {
        ShadowPassDesc shadowDesc{};
        shadowDesc.shadowAtlas          = shadowAtlasRes;
        shadowDesc.shadowAtlasTexture   = shadowAtlas_;
        shadowDesc.drawList             = &drawList;
        shadowDesc.cbAllocator          = cbRing;
        shadowDesc.pipeline             = pipelines->GetShadowDepth(bindless);
        shadowDesc.vertexBufferSrvIndex = cubeMesh_.vbSrvIndex;
        shadowDesc.vertexBuffer         = cubeMesh_.vertexBuffer;
        shadowDesc.indexBuffer          = cubeMesh_.indexBuffer;
        shadowDesc.indexFormat          = cubeMesh_.indexFormat;
        shadowDesc.indexCount           = cubeMesh_.indexCount;
        shadowDesc.bindless             = bindless;
        for (uint32_t i = 0; i < shadowCfg.cascadeCount; ++i) {
            shadowDesc.cascades[i] = cascades[i];
        }
        shadowDesc.cascadeCount     = shadowCfg.cascadeCount;
        shadowDesc.atlasResolution  = kShadowResolution;
        AddShadowPass(rgBuilder, shadowDesc);
    }

    if (renderPath_ == RenderPath::Deferred) {
        // ── Deferred path: G-Buffer + lighting/debug ─────────────────────
        {
            GBufferPassDesc gbDesc{};
            gbDesc.albedoTarget         = albedoRes;
            gbDesc.normalTarget         = normalRes;
            gbDesc.positionTarget       = positionWSRes;
            gbDesc.depthBuffer          = depthRes;
            gbDesc.depthTexture         = gbuffer_.depth;
            gbDesc.drawList             = &drawList;
            gbDesc.cbAllocator          = cbRing;
            gbDesc.perFrameAlloc        = perFrameAlloc;
            gbDesc.pipeline             = pipelines->GetGBuffer(bindless);
            gbDesc.vertexBufferSrvIndex = cubeMesh_.vbSrvIndex;
            gbDesc.vertexBuffer         = cubeMesh_.vertexBuffer;
            gbDesc.indexBuffer          = cubeMesh_.indexBuffer;
            gbDesc.indexFormat          = cubeMesh_.indexFormat;
            gbDesc.indexCount           = cubeMesh_.indexCount;
            gbDesc.bindless             = bindless;
            gbDesc.fallbackWhite        = fallbackWhite_;
            gbDesc.fallbackBlack        = fallbackBlack_;
            gbDesc.fallbackNormal       = fallbackNormal_;
            gbDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
            gbDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
            AddGBufferPass(rgBuilder, gbDesc);
        }

        const bool debugActive = (debugModeValue_ < static_cast<uint32_t>(GBufferDebugMode::Count));

        if (!debugActive) {
            LightingPassDesc lightDesc{};
            lightDesc.backbuffer          = bbRes;
            lightDesc.albedoGBuffer       = albedoRes;
            lightDesc.normalGBuffer       = normalRes;
            lightDesc.depthGBuffer        = depthRes;
            lightDesc.positionGBuffer     = positionWSRes;
            lightDesc.perFrameAlloc       = perFrameAlloc;
            lightDesc.lightingAlloc       = lightingAlloc;
            lightDesc.pipeline            = pipelines->GetLighting();
            lightDesc.albedoSrvIndex      = gbuffer_.albedoSRV;
            lightDesc.normalSrvIndex      = gbuffer_.normalSRV;
            lightDesc.depthSrvIndex       = gbuffer_.depthSRV;
            lightDesc.positionSrvIndex    = gbuffer_.positionWSSRV;
            lightDesc.albedoTexture       = gbuffer_.albedo;
            lightDesc.normalTexture       = gbuffer_.normal;
            lightDesc.depthTextureForSrv  = gbuffer_.depth;
            lightDesc.positionTexture     = gbuffer_.positionWS;
            lightDesc.shadowAlloc          = shadowAlloc;
            lightDesc.shadowAtlasResource  = shadowAtlasRes;
            lightDesc.shadowAtlasTexture   = shadowAtlas_;
            lightDesc.bindless            = bindless;
            lightDesc.viewportWidth       = static_cast<float>(swapChain->GetWidth());
            lightDesc.viewportHeight      = static_cast<float>(swapChain->GetHeight());
            AddLightingPass(rgBuilder, lightDesc);
        } else {
            GBufferDebugPassDesc dbgDesc{};
            dbgDesc.backbuffer         = bbRes;
            dbgDesc.albedoGBuffer      = albedoRes;
            dbgDesc.normalGBuffer      = normalRes;
            dbgDesc.depthGBuffer       = depthRes;
            dbgDesc.perFrameAlloc      = perFrameAlloc;
            struct DebugCBData { uint32_t mode; uint32_t pad[3]; };
            CBAllocation debugAlloc = cbRing->Allocate(sizeof(DebugCBData));
            DebugCBData debugCBData{debugModeValue_, {0, 0, 0}};
            std::memcpy(debugAlloc.cpuPtr, &debugCBData, sizeof(DebugCBData));
            dbgDesc.debugAlloc         = debugAlloc;
            dbgDesc.pipeline           = pipelines->GetGBufferDebug();
            dbgDesc.mode               = static_cast<GBufferDebugMode>(debugModeValue_);
            dbgDesc.albedoSrvIndex     = gbuffer_.albedoSRV;
            dbgDesc.normalSrvIndex     = gbuffer_.normalSRV;
            dbgDesc.depthSrvIndex      = gbuffer_.depthSRV;
            dbgDesc.albedoTexture      = gbuffer_.albedo;
            dbgDesc.normalTexture      = gbuffer_.normal;
            dbgDesc.depthTextureForSrv = gbuffer_.depth;
            dbgDesc.bindless           = bindless;
            dbgDesc.viewportWidth      = static_cast<float>(swapChain->GetWidth());
            dbgDesc.viewportHeight     = static_cast<float>(swapChain->GetHeight());
            AddGBufferDebugPass(rgBuilder, dbgDesc);
        }
    } else {
        // ── Forward path: single-pass PBR directly to backbuffer ─────────
        PBRPassDesc fwdDesc{};
        fwdDesc.backbuffer           = bbRes;
        fwdDesc.depthBuffer          = depthRes;
        fwdDesc.depthTexture         = gbuffer_.depth;
        fwdDesc.drawList             = &drawList;
        fwdDesc.cbAllocator          = cbRing;
        fwdDesc.perFrameAlloc        = perFrameAlloc;
        fwdDesc.pipeline             = pipelines->GetForwardPBR(bindless);
        fwdDesc.vertexBufferSrvIndex = cubeMesh_.vbSrvIndex;
        fwdDesc.vertexBuffer         = cubeMesh_.vertexBuffer;
        fwdDesc.indexBuffer          = cubeMesh_.indexBuffer;
        fwdDesc.indexFormat          = cubeMesh_.indexFormat;
        fwdDesc.indexCount           = cubeMesh_.indexCount;
        fwdDesc.bindless             = bindless;
        fwdDesc.viewportWidth        = static_cast<float>(swapChain->GetWidth());
        fwdDesc.viewportHeight       = static_cast<float>(swapChain->GetHeight());
        AddPBRPass(rgBuilder, fwdDesc);
    }

    // ── Overlay pass (grid + stats) ──────────────────────────────────────
    rgBuilder.AddPass("OverlayPass", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.Write(depthRes, RHIResourceState::DepthStencilWrite);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb    = res.GetTexture(bbRes);
            auto depth = res.GetTexture(depthRes);

            RHIRenderPassDesc passDesc{};
            passDesc.colorAttachmentCount        = 1;
            passDesc.colorAttachments[0].texture = bb;
            passDesc.colorAttachments[0].clear   = false;
            passDesc.hasDepth                    = true;
            passDesc.depthAttachment.texture     = depth;
            passDesc.depthAttachment.clear       = false;
            cmd->BeginRenderPass(passDesc);

            RHIViewport vp{};
            vp.width    = static_cast<float>(swapChain->GetWidth());
            vp.height   = static_cast<float>(swapChain->GetHeight());
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right  = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            GetEditorGrid()->Draw(cmd, *cbRing, camera);

            GetStatsOverlay()->SetDrawCalls(kCubeCount + 1);
            GetStatsOverlay()->SetVertexCount(
                static_cast<uint32_t>(kCubeCount) * cubeMesh_.vertexCount + 3);
            GetStatsOverlay()->SetTriangleCount(
                static_cast<uint32_t>(kCubeCount) * (cubeMesh_.indexCount / 3) + 1);
            GetStatsOverlay()->Render(cmd, *cbRing, GetFrameIndex());

            cmd->EndRenderPass();
        });
    });

    // ── Present transition ───────────────────────────────────────────────
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // ── Screenshot capture (records copy commands — saved in Run() after Execute)
    if (ShouldCaptureScreenshot()) {
        GetScreenshotCapture()->CaptureFromPresent(cmdList, swapChain);
        SetScreenshotSaveDeferred();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown — Release demo-specific GPU resources
// ─────────────────────────────────────────────────────────────────────────────

void PBRTestDemo::OnShutdown() {
    auto* device = GetDevice();

    if (shadowAtlas_.IsValid()) device->DestroyTexture(shadowAtlas_);
    gbuffer_.Destroy(device);

    // Destroy PBR textures
    auto destroyTex = [&](TextureLoadResult& t) {
        if (t.handle.IsValid()) device->DestroyTexture(t.handle);
    };
    destroyTex(albedoTex_);
    destroyTex(normalTex_);
    destroyTex(roughnessTex_);
    destroyTex(metallicTex_);
    destroyTex(aoTex_);
    destroyTex(checkerTex_);

    if (fallbackWhite_.IsValid())  device->DestroyTexture(fallbackWhite_);
    if (fallbackBlack_.IsValid())  device->DestroyTexture(fallbackBlack_);
    if (fallbackNormal_.IsValid()) device->DestroyTexture(fallbackNormal_);

    DestroyGPUMesh(device, cubeMesh_);
}

} // namespace uz
