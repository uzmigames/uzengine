/// IBL Test Demo Implementation
///
/// Generates a procedural sky cubemap on the CPU, bakes IBL textures via
/// IBLBaker (BRDF LUT, irradiance, prefiltered specular), then renders a
/// 5x5 grid of cubes using the forward PBR pass with IBL ambient lighting.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "ibl_test_demo.h"

#include "uzengine.h"

#include <DirectXPackedVector.h>

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

/// Grid layout: 5x5 cubes with varying metallic (columns) and roughness (rows).
/// Since PerObjectCB only has SRV indices (not material parameter overrides),
/// all cubes use default materials. The key visual test is IBL ambient reflections
/// vs flat gray ambient.
static constexpr uint32_t kGridRows    = 5;
static constexpr uint32_t kGridCols    = 5;
static constexpr uint32_t kCubeCount   = kGridRows * kGridCols;
static constexpr float    kGridSpacing = 2.5f;
static constexpr float    kCubeScale   = 0.8f;

/// Procedural env cubemap resolution per face.
static constexpr uint32_t kEnvCubemapSize = 64;

/// Sun direction for the procedural sky (normalized).
static const Vec3 kSunDir = Vec3{0.4f, 0.6f, 0.3f}.Normalized();

// ─────────────────────────────────────────────────────────────────────────────
// Cubemap face direction computation
// ─────────────────────────────────────────────────────────────────────────────

/// Computes the world-space direction for a texel on a cubemap face.
///
/// # Arguments
/// * `face` — Cubemap face index (0=+X, 1=-X, 2=+Y, 3=-Y, 4=+Z, 5=-Z).
/// * `u` — Horizontal coordinate in [0, 1].
/// * `v` — Vertical coordinate in [0, 1].
///
/// # Returns
/// Normalized direction vector from the cubemap center toward the texel.
static Vec3 ComputeCubeFaceDir(uint32_t face, float u, float v) noexcept {
    // Map [0,1] to [-1,1]
    const float cu = u * 2.0f - 1.0f;
    const float cv = v * 2.0f - 1.0f;

    Vec3 dir{};
    switch (face) {
        case 0: dir = { 1.0f,   -cv,   -cu}; break; // +X
        case 1: dir = {-1.0f,   -cv,    cu}; break; // -X
        case 2: dir = {  cu,   1.0f,    cv}; break; // +Y
        case 3: dir = {  cu,  -1.0f,   -cv}; break; // -Y
        case 4: dir = {  cu,   -cv,   1.0f}; break; // +Z
        case 5: dir = { -cu,   -cv,  -1.0f}; break; // -Z
        default: break;
    }
    return dir.Normalized();
}

// ─────────────────────────────────────────────────────────────────────────────
// IBLTestDemo — Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

IBLTestDemo::IBLTestDemo(const IBLTestConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 IBL Test",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.frameCount,
        .vsync  = false,
        .screenshotPath = config.screenshotPath,
        .screenshotFrame = config.screenshotFrame,
        .cameraPositionX = 0.f,
        .cameraPositionY = 8.f,
        .cameraPositionZ = 15.f,
        .cameraYawDeg = -90.f,
        .cameraPitchDeg = -25.f,
    })
    , config_(config)
{}

IBLTestDemo::~IBLTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::OnInit() {
    startTime_ = std::chrono::high_resolution_clock::now();

    LoadMeshData();
    CreateDepthBuffer();
    CreateProceduralEnvCubemap();

    // ── Initialize IBL baker and bake all IBL textures ───────────────────
    if (!iblBaker_.Init(GetDevice(), GetShaderManager())) {
        UZ_LOG(Renderer, Error, "IBL test: failed to init IBLBaker");
        SetReady();
        return;
    }

    brdfLutSrv_ = iblBaker_.BakeBRDFLut();
    if (brdfLutSrv_ == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBL test: failed to bake BRDF LUT");
    }

    auto bakeResult = iblBaker_.BakeEnvironment(envCubemapSrv_);
    irradianceSrv_   = bakeResult.irradianceSrvIndex;
    prefilteredSrv_  = bakeResult.prefilteredSrvIndex;

    if (irradianceSrv_ == UINT32_MAX || prefilteredSrv_ == UINT32_MAX) {
        UZ_LOG(Renderer, Error, "IBL test: failed to bake environment maps");
    }

    UZ_LOG(Renderer, Info,
           "IBL test demo started — {}x{} grid, BRDF LUT SRV={}, irradiance SRV={}, "
           "prefiltered SRV={}",
           kGridCols, kGridRows, brdfLutSrv_, irradianceSrv_, prefilteredSrv_);

    SetReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// LoadMeshData
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::LoadMeshData() {
    std::string meshPath = (uz::GetExeDir() / "assets/cube.uzmesh").string();
    Mesh cpuMesh = LoadMesh(meshPath);
    if (!cpuMesh.IsValid()) {
        UZ_LOG(Renderer, Error, "IBL test: failed to load cube.uzmesh from '{}'", meshPath);
        return;
    }
    cubeMesh_ = UploadMesh(GetDevice(), GetCommandList(), cpuMesh);
    if (!cubeMesh_.IsValid()) {
        UZ_LOG(Renderer, Error, "IBL test: failed to upload cube mesh to GPU");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateDepthBuffer
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::CreateDepthBuffer() {
    RHITextureDesc depthDesc{};
    depthDesc.width     = GetWidth();
    depthDesc.height    = GetHeight();
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    depthDesc.debugName = "IBLTest_Depth";
    depthBuffer_ = GetDevice()->CreateTexture(depthDesc);
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateProceduralEnvCubemap
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::CreateProceduralEnvCubemap() {
    auto* device  = GetDevice();
    auto* cmdList = GetCommandList();

    constexpr uint32_t kFaceCount   = 6;
    constexpr uint32_t kChannels    = 4; // RGBA
    constexpr uint32_t kPixelsPerFace = kEnvCubemapSize * kEnvCubemapSize;
    constexpr uint32_t kHalfsPerFace  = kPixelsPerFace * kChannels;

    // Generate all 6 faces of sky gradient as RGBA16F (half-float)
    using Half = DirectX::PackedVector::HALF;
    std::vector<Half> faceData(static_cast<size_t>(kHalfsPerFace) * kFaceCount);

    for (uint32_t face = 0; face < kFaceCount; ++face) {
        Half* facePtr = faceData.data() + static_cast<size_t>(face) * kHalfsPerFace;

        for (uint32_t y = 0; y < kEnvCubemapSize; ++y) {
            for (uint32_t x = 0; x < kEnvCubemapSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) / static_cast<float>(kEnvCubemapSize);
                const float v = (static_cast<float>(y) + 0.5f) / static_cast<float>(kEnvCubemapSize);

                Vec3 dir = ComputeCubeFaceDir(face, u, v);
                float yDir = dir.y;

                // Sky gradient: blue zenith, white horizon, dark ground
                float r, g, b;
                if (yDir > 0.0f) {
                    // Lerp from horizon white to zenith blue
                    r = 0.8f + (0.3f - 0.8f) * yDir;
                    g = 0.85f + (0.5f - 0.85f) * yDir;
                    b = 0.9f + (0.9f - 0.9f) * yDir;
                } else {
                    // Ground: dark brown fade
                    float t = -yDir;
                    r = 0.3f + (0.1f - 0.3f) * t;
                    g = 0.25f + (0.08f - 0.25f) * t;
                    b = 0.2f + (0.06f - 0.2f) * t;
                }

                // Add sun disk for specular highlight
                float sunDot = dir.x * kSunDir.x + dir.y * kSunDir.y + dir.z * kSunDir.z;
                if (sunDot > 0.995f) {
                    float intensity = (sunDot - 0.995f) / (1.0f - 0.995f);
                    r += 10.0f * intensity;
                    g += 9.0f * intensity;
                    b += 8.0f * intensity;
                }

                uint32_t idx = (y * kEnvCubemapSize + x) * kChannels;
                facePtr[idx + 0] = DirectX::PackedVector::XMConvertFloatToHalf(r);
                facePtr[idx + 1] = DirectX::PackedVector::XMConvertFloatToHalf(g);
                facePtr[idx + 2] = DirectX::PackedVector::XMConvertFloatToHalf(b);
                facePtr[idx + 3] = DirectX::PackedVector::XMConvertFloatToHalf(1.0f);
            }
        }
    }

    // Create cubemap texture (Texture2DArray with 6 slices)
    RHITextureDesc cubemapDesc{};
    cubemapDesc.width            = kEnvCubemapSize;
    cubemapDesc.height           = kEnvCubemapSize;
    cubemapDesc.depthOrArraySize = kFaceCount;
    cubemapDesc.format           = RHIFormat::RGBA16Float;
    cubemapDesc.usage            = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    cubemapDesc.debugName        = "IBLTest_EnvCubemap";
    envCubemap_ = device->CreateTexture(cubemapDesc);

    if (!envCubemap_.IsValid()) {
        UZ_LOG(Renderer, Error, "IBL test: failed to create env cubemap texture");
        return;
    }

    // Upload each face via staging buffer
    constexpr uint32_t kBytesPerFace = kHalfsPerFace * sizeof(Half);
    constexpr uint32_t kRowPitch     = kEnvCubemapSize * kChannels * sizeof(Half);

    RHIBufferDesc uploadDesc{};
    uploadDesc.sizeBytes = kBytesPerFace;
    uploadDesc.usage     = RHIBufferUsage::CopySrc;
    uploadDesc.memory    = RHIMemoryType::Upload;
    uploadDesc.debugName = "IBLTest_EnvUpload";

    for (uint32_t face = 0; face < kFaceCount; ++face) {
        auto uploadBuf = device->CreateBuffer(uploadDesc);
        if (!uploadBuf.IsValid()) {
            UZ_LOG(Renderer, Error, "IBL test: failed to create upload buffer for face {}", face);
            continue;
        }

        void* mapped = device->MapBuffer(uploadBuf);
        if (mapped) {
            const Half* srcFace = faceData.data() + static_cast<size_t>(face) * kHalfsPerFace;
            std::memcpy(mapped, srcFace, kBytesPerFace);
            device->UnmapBuffer(uploadBuf);
        }

        RHIBarrierDesc toCopy{};
        toCopy.texture     = envCubemap_;
        toCopy.stateBefore = RHIResourceState::ShaderResource;
        toCopy.stateAfter  = RHIResourceState::CopyDst;
        cmdList->ResourceBarrier(&toCopy, 1);
        cmdList->CopyBufferToTexture(uploadBuf, 0, kRowPitch, envCubemap_, 0, face);
        RHIBarrierDesc toSrv{};
        toSrv.texture     = envCubemap_;
        toSrv.stateBefore = RHIResourceState::CopyDst;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toSrv, 1);

        device->DestroyBuffer(uploadBuf);
    }

    envCubemapSrv_ = device->GetTextureSRVIndex(envCubemap_);
    UZ_LOG(Renderer, Info, "IBL test: procedural env cubemap created ({}x{}, SRV={})",
           kEnvCubemapSize, kEnvCubemapSize, envCubemapSrv_);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::OnResize(uint32_t width, uint32_t height) {
    auto* device = GetDevice();
    if (depthBuffer_.IsValid()) {
        device->DestroyTexture(depthBuffer_);
    }

    RHITextureDesc depthDesc{};
    depthDesc.width     = width;
    depthDesc.height    = height;
    depthDesc.format    = RHIFormat::D32Float;
    depthDesc.usage     = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
    depthDesc.debugName = "IBLTest_Depth";
    depthBuffer_ = device->CreateTexture(depthDesc);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::OnFrame(float dt) {
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
    GetCameraController().Update(camera, *input, dt);

    // ── CB ring begin frame ──────────────────────────────────────────────
    cbRing->BeginFrame(GetFrameIndex());

    // ── Fill PerFrameCB with IBL SRV indices ─────────────────────────────
    PerFrameCB perFrame{};
    perFrame.viewProj    = camera.GetViewProjection();
    perFrame.invViewProj = perFrame.viewProj.Inverse();
    Vec3 camPos          = camera.GetPosition();
    perFrame.cameraPos   = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams  = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};

    // Directional sun light
    perFrame.lightDir    = {kSunDir.x, kSunDir.y, kSunDir.z, 0.f};
    perFrame.lightColor  = {3.0f, 2.85f, 2.7f, 0.f};
    perFrame.ambientColor = {0.03f, 0.03f, 0.05f, 0.f};

    // IBL descriptor indices — this is what makes the demo meaningful
    perFrame.irradianceSrvIndex   = irradianceSrv_;
    perFrame.prefilteredSrvIndex  = prefilteredSrv_;
    perFrame.brdfLutSrvIndex      = brdfLutSrv_;
    perFrame.prefilteredMipLevels = 5;

    CBAllocation perFrameAlloc = cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(perFrameAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── Build draw list: 5x5 grid + ground plane ─────────────────────────
    DrawList drawList;
    drawList.Clear();

    const float gridOffsetX = -static_cast<float>(kGridCols - 1) * kGridSpacing * 0.5f;
    const float gridOffsetZ = -static_cast<float>(kGridRows - 1) * kGridSpacing * 0.5f;

    uint32_t drawIdx = 0;
    for (uint32_t row = 0; row < kGridRows; ++row) {
        for (uint32_t col = 0; col < kGridCols; ++col) {
            const float px = gridOffsetX + static_cast<float>(col) * kGridSpacing;
            const float pz = gridOffsetZ + static_cast<float>(row) * kGridSpacing;

            DrawCommand cmd{};
            cmd.sortKey      = SortKey::Opaque(0, 0, 0.f, drawIdx);
            cmd.worldMatrix  = Mat4::TRS(Vec3{px, 0.f, pz}, kCubeScale);
            cmd.normalMatrix = cmd.worldMatrix.NormalMatrix();
            cmd.meshId       = 0;
            // Default SRV indices (0) — albedo=white, roughness=0.5, metallic=0.0
            drawList.Add(cmd);
            ++drawIdx;
        }
    }

    // Ground plane
    {
        DrawCommand ground{};
        ground.sortKey      = SortKey::Opaque(0, 0, 0.f, drawIdx);
        ground.worldMatrix  = Mat4::TRS(Vec3{0.f, 0.f, -0.82f}, Vec3{20.f, 20.f, 0.02f});
        ground.normalMatrix = ground.worldMatrix.NormalMatrix();
        ground.meshId       = 0;
        drawList.Add(ground);
    }
    drawList.Sort();

    // ── Record frame via render graph ────────────────────────────────────
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();
    rg::RgBuilder rgBuilder(device, GetRgPool());
    auto bbRes    = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);
    auto depthRes = rgBuilder.ImportTexture("DepthBuffer", depthBuffer_, RHIResourceState::DepthStencilWrite);

    auto* pipelines = GetPipelines();

    // ── Forward PBR pass with IBL ────────────────────────────────────────
    PBRPassDesc fwdDesc{};
    fwdDesc.backbuffer           = bbRes;
    fwdDesc.depthBuffer          = depthRes;
    fwdDesc.depthTexture         = depthBuffer_;
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
                (kCubeCount + 1) * cubeMesh_.vertexCount);
            GetStatsOverlay()->SetTriangleCount(
                (kCubeCount + 1) * (cubeMesh_.indexCount / 3));
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

    // ── Screenshot capture ───────────────────────────────────────────────
    if (ShouldCaptureScreenshot()) {
        GetScreenshotCapture()->CaptureFromPresent(cmdList, swapChain);
        SetScreenshotSaveDeferred();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown
// ─────────────────────────────────────────────────────────────────────────────

void IBLTestDemo::OnShutdown() {
    auto* device = GetDevice();

    iblBaker_.Shutdown();

    if (envCubemap_.IsValid()) device->DestroyTexture(envCubemap_);
    if (depthBuffer_.IsValid()) device->DestroyTexture(depthBuffer_);

    DestroyGPUMesh(device, cubeMesh_);
}

} // namespace uz
