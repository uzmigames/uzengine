/// Camera Test Demo Implementation — DemoApplication-based
///
/// Renders a colored cube in 3D with FPS camera controls.
/// WASD to move, right-click + mouse to look, Shift to sprint, Space/Ctrl up/down.
/// Uses only abstract RHI interfaces — no DX12, Vulkan, or Metal types.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "camera_test_demo.h"

#include <chrono>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kCubeVertexCount = 36;

// ─────────────────────────────────────────────────────────────────────────────
// Cube Geometry — 36 vertices (6 faces x 2 triangles x 3 verts)
// ─────────────────────────────────────────────────────────────────────────────

/// Fills outVerts with a unit cube centered at origin. Each face has a distinct color.
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
// CameraTestDemo — Construction / Destruction
// ─────────────────────────────────────────────────────────────────────────────

CameraTestDemo::CameraTestDemo(const CameraTestConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 Camera Test (WASD + RMB Look)",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.frameCount,
        .vsync  = false,
        .screenshotPath = config.screenshotPath,
        .screenshotFrame = config.screenshotFrame,
    })
    , config_(config)
{}

CameraTestDemo::~CameraTestDemo() = default;

// ─────────────────────────────────────────────────────────────────────────────
// OnInit — Create demo-specific rendering resources
// ─────────────────────────────────────────────────────────────────────────────

void CameraTestDemo::OnInit() {
    startTime_ = std::chrono::high_resolution_clock::now();

    auto* device = GetDevice();
    const bool bindless = IsBindless();

    // ── Cube vertex buffer ──────────────────────────────────────────────
    SimpleVertex cubeVerts[kCubeVertexCount];
    GetCubeVertices(cubeVerts);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(cubeVerts);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = bindless ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "CubeVertexBuffer";

    cubeVB_ = device->CreateBuffer(vbDesc);
    void* mapped = device->MapBuffer(cubeVB_);
    std::memcpy(mapped, cubeVerts, sizeof(cubeVerts));
    device->UnmapBuffer(cubeVB_);

    if (bindless) {
        cubeVBSrvIndex_ = device->GetBufferSRVIndex(cubeVB_);
    }

    // ── Shaders + Pipeline ──────────────────────────────────────────────
    {
        auto& shaderPipeline = GetShaderManager()->GetPipeline();

        std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
        shaderPipeline.Mount("/demo/camera", shadersDir);

        auto vsBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_vs.hlsl", "");
        auto psBytecode = shaderPipeline.LoadBytecode("/demo/camera/camera_ps.hlsl", "");
        if (vsBytecode.empty() || psBytecode.empty()) {
            UZ_LOG(Renderer, Error, "Failed to load camera shaders");
            return;
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

        vsHandle_ = device->CreateShader(vsDesc);
        psHandle_ = device->CreateShader(psDesc);

        if (!vsHandle_.IsValid() || !psHandle_.IsValid()) {
            UZ_LOG(Renderer, Error, "Failed to create camera shaders");
            return;
        }

        RHIInputElementDesc inputElements[] = {
            { RHIInputSemantic::Position, 0, RHIFormat::RGB32Float,  0, 0  },
            { RHIInputSemantic::Color,    0, RHIFormat::RGBA32Float, 0, 12 },
            { RHIInputSemantic::Texcoord, 0, RHIFormat::RG32Float,   0, 28 },
        };

        RHIGraphicsPipelineDesc pipeDesc{};
        pipeDesc.vertexShader = vsHandle_;
        pipeDesc.pixelShader = psHandle_;
        pipeDesc.topology = RHIPrimitiveTopology::TriangleList;
        pipeDesc.rasterizer.cullMode = RHICullMode::None;
        pipeDesc.depthStencil.depthEnable = true;
        pipeDesc.depthStencil.depthWrite = true;
        pipeDesc.renderTargetFormats[0] = GetSwapChain()->GetFormat();
        pipeDesc.renderTargetCount = 1;
        pipeDesc.depthStencilFormat = RHIFormat::D32Float;
        pipeDesc.debugName = "CameraCubePipeline";

        if (!bindless) {
            pipeDesc.inputElements = inputElements;
            pipeDesc.inputElementCount = 3;
            pipeDesc.vertexStride = sizeof(SimpleVertex);
        }

        pipeline_ = device->CreateGraphicsPipeline(pipeDesc);
        if (!pipeline_.IsValid()) {
            UZ_LOG(Renderer, Error, "Failed to create camera cube pipeline");
            return;
        }
    }

    // ── Depth buffer ────────────────────────────────────────────────────
    RHITextureDesc depthDesc{};
    depthDesc.width = GetWidth();
    depthDesc.height = GetHeight();
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";

    depthBuffer_ = device->CreateTexture(depthDesc);

    UZ_LOG(Renderer, Info, "Camera test demo started — WASD move, RMB+mouse look, Shift sprint");

    SetReady();
}

// ─────────────────────────────────────────────────────────────────────────────
// OnFrame — Record one frame's rendering commands
// ─────────────────────────────────────────────────────────────────────────────

void CameraTestDemo::OnFrame(float dt) {
    auto* device    = GetDevice();
    auto* cmdList   = GetCommandList();
    auto* swapChain = GetSwapChain();
    auto* cbRing    = GetCBRing();
    auto& camera    = GetCamera();
    auto* input     = GetInput();

    const bool bindless = IsBindless();

    auto now = std::chrono::high_resolution_clock::now();
    totalTime_ = std::chrono::duration<float>(now - startTime_).count();

    // ── Stats ────────────────────────────────────────────────────────────
    GetStatsOverlay()->BeginFrame(dt);

    // ── Input ────────────────────────────────────────────────────────────
    input->BeginFrame();

    // ── Camera Update ────────────────────────────────────────────────────
    GetCameraController().Update(camera, *input, dt);

    // ── Upload PerFrameCB ────────────────────────────────────────────────
    cbRing->BeginFrame(GetFrameIndex());

    PerFrameCB perFrame{};
    perFrame.viewProj = camera.GetViewProjection();
    Vec3 camPos = camera.GetPosition();
    perFrame.cameraPos = {camPos.x, camPos.y, camPos.z, 0.f};
    perFrame.timeParams = {totalTime_, dt, static_cast<float>(GetFrameIndex()), 0.f};

    CBAllocation cbAlloc = cbRing->Allocate(sizeof(PerFrameCB));
    std::memcpy(cbAlloc.cpuPtr, &perFrame, sizeof(PerFrameCB));

    // ── Record via render graph ─────────────────────────────────────────
    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    rg::RgBuilder rgBuilder(device, GetRgPool());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    // 3D scene pass: clear + cube + grid.
    rgBuilder.AddPass("CameraScene", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb = res.GetTexture(bbRes);

            float clearColor[4] = {0.05f, 0.05f, 0.08f, 1.0f};
            cmd->ClearRenderTarget(bb, clearColor);
            cmd->ClearDepthStencil(depthBuffer_, 1.0f, 0);

            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachments[0].texture = bb;
            rpDesc.colorAttachmentCount = 1;
            rpDesc.hasDepth = true;
            rpDesc.depthAttachment.texture = depthBuffer_;
            cmd->BeginRenderPass(rpDesc);

            cmd->SetPipeline(pipeline_);

            if (bindless) {
                cmd->SetDescriptorIndex(0, cubeVBSrvIndex_);
            } else {
                cmd->SetVertexBuffer(0, cubeVB_, 0);
            }

            cmd->SetRootCBV(1, cbAlloc.bufferHandle, cbAlloc.offsetBytes,
                            cbAlloc.cpuPtr, sizeof(PerFrameCB));

            RHIViewport vp{};
            vp.width = static_cast<float>(swapChain->GetWidth());
            vp.height = static_cast<float>(swapChain->GetHeight());
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right = static_cast<int32_t>(swapChain->GetWidth());
            sc.bottom = static_cast<int32_t>(swapChain->GetHeight());
            cmd->SetScissor(sc);

            cmd->Draw(kCubeVertexCount);

            GetEditorGrid()->Draw(cmd, *cbRing, camera);

            cmd->EndRenderPass();
        });
    });

    // Stats overlay pass: color only, no depth — 2D UI on top of 3D.
    rgBuilder.AddPass("StatsOverlay", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb = res.GetTexture(bbRes);

            GetStatsOverlay()->SetDrawCalls(2);
            GetStatsOverlay()->SetVertexCount(kCubeVertexCount + 3);
            GetStatsOverlay()->SetTriangleCount(kCubeVertexCount / 3 + 1);

            RHIRenderPassDesc overlayRpDesc{};
            overlayRpDesc.colorAttachments[0].texture = bb;
            overlayRpDesc.colorAttachmentCount = 1;
            overlayRpDesc.hasDepth = false;
            cmd->BeginRenderPass(overlayRpDesc);
            GetStatsOverlay()->Render(cmd, *cbRing, GetFrameIndex());
            cmd->EndRenderPass();
        });
    });

    // Present transition pass.
    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // ── Screenshot capture ──────────────────────────────────────────────
    if (ShouldCaptureScreenshot()) {
        GetScreenshotCapture()->CaptureFromPresent(cmdList, swapChain);
        SetScreenshotSaveDeferred();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// OnResize — Recreate size-dependent resources
// ─────────────────────────────────────────────────────────────────────────────

void CameraTestDemo::OnResize(uint32_t width, uint32_t height) {
    auto* device = GetDevice();

    // Recreate depth buffer at new size
    if (depthBuffer_.IsValid()) {
        device->DestroyTexture(depthBuffer_);
    }

    RHITextureDesc depthDesc{};
    depthDesc.width = width;
    depthDesc.height = height;
    depthDesc.format = RHIFormat::D32Float;
    depthDesc.usage = RHITextureUsage::DepthStencil;
    depthDesc.debugName = "DepthBuffer";

    depthBuffer_ = device->CreateTexture(depthDesc);
}

// ─────────────────────────────────────────────────────────────────────────────
// OnShutdown — Release demo-specific GPU resources
// ─────────────────────────────────────────────────────────────────────────────

void CameraTestDemo::OnShutdown() {
    auto* device = GetDevice();

    if (depthBuffer_.IsValid()) device->DestroyTexture(depthBuffer_);
    if (pipeline_.IsValid()) device->DestroyPipeline(pipeline_);
    if (vsHandle_.IsValid()) device->DestroyShader(vsHandle_);
    if (psHandle_.IsValid()) device->DestroyShader(psHandle_);
    if (cubeVB_.IsValid()) device->DestroyBuffer(cubeVB_);
}

} // namespace uz
