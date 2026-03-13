/// Textured Quad Demo — DemoApplication-based backend-agnostic checkerboard quad.
///
/// Renders a procedural checkerboard quad using only abstract RHI interfaces.
/// Automatically selects SM6.6 bindless or SM5 Input Assembler path based on
/// GPU capabilities. Validates the full texture upload pipeline:
/// staging buffer -> CopyBufferToTexture -> SRV access.

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include "textured_quad_demo.h"
#include "rhi.h"
#include "rhi_backend.h"
#include "rhi_config.h"
#include "rhi_types.h"
#include "log.h"
#include "vertex.h"
#include "upload_context.h"
#include "paths.h"

#ifdef _WIN32
#include "shader_pipeline.h"
#include "shader_compiler.h"
#include "shader_compiler_fxc.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#endif

#include <cstring>
#include <memory>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Embedded MSL Shaders (macOS only)
// ─────────────────────────────────────────────────────────────────────────────

#if defined(__APPLE__)

static const char kTexturedQuadVSSource[] = R"msl(
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

vertex VertexOut vertex_main(VertexIn in [[stage_in]]) {
    VertexOut out;
    out.position = float4(in.position, 1.0);
    out.color    = in.color;
    out.texcoord = in.texcoord;
    return out;
}
)msl";

static const char kTexturedQuadPSSource[] = R"msl(
#include <metal_stdlib>
using namespace metal;

struct FragmentIn {
    float4 position [[position]];
    float4 color;
    float2 texcoord;
};

fragment float4 fragment_main(FragmentIn in [[stage_in]],
                               texture2d<float> tex [[texture(0)]],
                               sampler samp [[sampler(0)]]) {
    float4 texColor = tex.sample(samp, in.texcoord);
    return texColor * in.color;
}
)msl";

#endif // __APPLE__

// ─────────────────────────────────────────────────────────────────────────────
// Constants
// ─────────────────────────────────────────────────────────────────────────────

static constexpr uint32_t kTexWidth  = 256;
static constexpr uint32_t kTexHeight = 256;
static constexpr uint32_t kTileSize  = 32;
static constexpr uint32_t kVertexCount = 6;

/// Bindless root constants — must match HLSL cbuffer RootConstants : register(b0).
struct RootConstants {
    uint32_t vertexBufferIndex;
    uint32_t textureIndex;
    uint32_t samplerIndex;
};

// ─────────────────────────────────────────────────────────────────────────────
// Procedural Checkerboard
// ─────────────────────────────────────────────────────────────────────────────

/// Generates a 256x256 RGBA8 checkerboard pattern.
/// Each tile is 32x32 pixels, with a slight blue tint on dark tiles.
static std::vector<uint8_t> GenerateCheckerboard(uint32_t width, uint32_t height,
                                                  uint32_t tileSize) {
    std::vector<uint8_t> pixels(width * height * 4);
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            const bool isWhite = ((x / tileSize) + (y / tileSize)) % 2 == 0;
            const uint32_t idx = (y * width + x) * 4;
            pixels[idx + 0] = isWhite ? 240 : 40;   // R
            pixels[idx + 1] = isWhite ? 240 : 40;   // G
            pixels[idx + 2] = isWhite ? 240 : 200;  // B — slight blue tint on dark tiles
            pixels[idx + 3] = 255;                   // A
        }
    }
    return pixels;
}

/// Returns 6 vertices forming a quad (two triangles) in NDC space.
static void GetQuadVertices(SimpleVertex outVerts[kVertexCount]) {
    outVerts[0] = { { -0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 0.0f } };
    outVerts[1] = { {  0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 0.0f } };
    outVerts[2] = { { -0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 1.0f } };
    outVerts[3] = { {  0.8f,  0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 0.0f } };
    outVerts[4] = { {  0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 1.0f, 1.0f } };
    outVerts[5] = { { -0.8f, -0.8f, 0.0f }, { 1, 1, 1, 1 }, { 0.0f, 1.0f } };
}

// ─────────────────────────────────────────────────────────────────────────────
// TexturedQuadDemo
// ─────────────────────────────────────────────────────────────────────────────

TexturedQuadDemo::TexturedQuadDemo(const TexturedQuadConfig& config)
    : DemoApplication(DemoConfig{
        .title  = "UzEngine \xe2\x80\x94 Textured Quad Demo",
        .width  = 1280,
        .height = 720,
        .maxFrames = config.maxFrames,
        .vsync  = false,
        .screenshotPath = config.screenshotPath,
        .screenshotFrame = config.screenshotFrame,
    }) {}

TexturedQuadDemo::~TexturedQuadDemo() = default;

void TexturedQuadDemo::OnInit() {
    auto* device = GetDevice();

#if defined(__APPLE__)
    bindless_ = false;
#else
    bindless_ = device->IsBindless();
#endif

    CompileShaders();
    CreatePipeline();
    CreateVertexBuffer();
    CreateCheckerboardTexture();

    // ── Sampler ─────────────────────────────────────────────────────────
    RHISamplerDesc samplerDesc{};
    samplerDesc.filter = RHIFilterMode::Linear;
    samplerDesc.addressU = RHIAddressMode::Wrap;
    samplerDesc.addressV = RHIAddressMode::Wrap;
    samplerDesc.debugName = "LinearWrapSampler";

    samplerHandle_ = device->CreateSampler(samplerDesc);
    if (!samplerHandle_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create sampler");
        return;
    }

    samplerIdx_ = device->GetSamplerIndex(samplerHandle_);
    UZ_LOG(Renderer, Info, "Sampler index: {}", samplerIdx_);

    // Signal that all resources are loaded — screenshots can now be captured.
    SetReady();
}

void TexturedQuadDemo::OnFrame(float /*dt*/) {
    auto* device = GetDevice();
    auto* cmdList = GetCommandList();
    auto* swapChain = GetSwapChain();

    RHITextureHandle backBuffer = swapChain->GetCurrentBackBuffer();

    // ── Render graph: textured quad pass + present transition ────────
    rg::RgBuilder rgBuilder(device, GetRgPool());
    auto bbRes = rgBuilder.ImportTexture("Backbuffer", backBuffer, RHIResourceState::Present);

    rgBuilder.AddPass("TexturedQuad", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::RenderTarget);
        pb.SetExecutionCallback([&](IRHICommandList* cmd, const rg::RgPassResources& res) {
            auto bb = res.GetTexture(bbRes);

            RHIRenderPassDesc rpDesc{};
            rpDesc.colorAttachments[0].texture = bb;
            rpDesc.colorAttachments[0].clear = false;
            rpDesc.colorAttachmentCount = 1;
            cmd->BeginRenderPass(rpDesc);

            cmd->SetPipeline(pipeline_);

            if (bindless_) {
                RootConstants rc{ vbSrvIndex_, texSrvIndex_, samplerIdx_ };
                cmd->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&rc), 3, 0);
            } else {
                cmd->SetVertexBuffer(0, vbHandle_, 0);
#if defined(__APPLE__)
                cmd->SetFragmentTexture(0, texHandle_);
                cmd->SetFragmentSampler(0, samplerHandle_);
#else
                cmd->SetDescriptorTable(4, texSrvIndex_);
#endif
            }

            RHIViewport vp{};
            vp.width = static_cast<float>(swapChain->GetWidth());
            vp.height = static_cast<float>(swapChain->GetHeight());
            vp.maxDepth = 1.0f;
            cmd->SetViewport(vp);

            RHIScissorRect sc{};
            sc.right = swapChain->GetWidth();
            sc.bottom = swapChain->GetHeight();
            cmd->SetScissor(sc);

            cmd->Draw(kVertexCount);
            cmd->EndRenderPass();
        });
    });

    rgBuilder.AddPass("Present", [&](rg::RgPassBuilder& pb) {
        pb.Write(bbRes, RHIResourceState::Present);
        pb.SetExecutionCallback([](IRHICommandList*, const rg::RgPassResources&) {});
    });

    rgBuilder.Compile();
    rgBuilder.Execute(cmdList);

    // Screenshot capture — after render graph, backbuffer is in Present state.
    if (ShouldCaptureScreenshot()) {
        GetScreenshotCapture()->CaptureFromPresent(cmdList, swapChain);
        SetScreenshotSaveDeferred();
    }
}

void TexturedQuadDemo::OnShutdown() {
    auto* device = GetDevice();

#ifdef _WIN32
    if (shaderPipeline_) shaderPipeline_->Shutdown();
#endif

    if (samplerHandle_.IsValid()) device->DestroySampler(samplerHandle_);
    if (texHandle_.IsValid())     device->DestroyTexture(texHandle_);
    if (vbHandle_.IsValid())      device->DestroyBuffer(vbHandle_);
    if (pipeline_.IsValid())      device->DestroyPipeline(pipeline_);
    if (vsHandle_.IsValid())      device->DestroyShader(vsHandle_);
    if (psHandle_.IsValid())      device->DestroyShader(psHandle_);
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource Creation Helpers
// ─────────────────────────────────────────────────────────────────────────────

void TexturedQuadDemo::CompileShaders() {
    auto* device = GetDevice();

#ifdef _WIN32
    compiler_ = std::make_unique<ShaderCompiler>();
    if (!compiler_->Init()) {
        UZ_LOG(Renderer, Error, "Failed to init shader compiler");
        return;
    }

    shaderPipeline_ = std::make_unique<ShaderPipeline>();
    ShaderPipeline::Config cfg;
    cfg.cacheDir = (uz::GetExeDir() / "cache/shaders").string();
    cfg.targetBackend = bindless_ ? "dx12_sm66" : "dx12_sm50";
    cfg.bindless = bindless_;
    shaderPipeline_->Init(cfg);
    shaderPipeline_->SetCompiler(compiler_.get());

    if (!bindless_) {
        compilerFXC_ = std::make_unique<ShaderCompilerFXC>();
        if (!compilerFXC_->Init()) {
            UZ_LOG(Renderer, Error, "Failed to init FXC shader compiler");
            return;
        }
        shaderPipeline_->SetCompilerFXC(compilerFXC_.get());
    }

    const std::string shadersDir = (uz::GetExeDir() / "../../shaders").string();
    shaderPipeline_->Mount("/demo/textured_quad", shadersDir);

    auto vsBytecode = shaderPipeline_->LoadBytecode("/demo/textured_quad/textured_vs.hlsl", "");
    auto psBytecode = shaderPipeline_->LoadBytecode("/demo/textured_quad/textured_ps.hlsl", "");
    if (vsBytecode.empty() || psBytecode.empty()) {
        UZ_LOG(Renderer, Error, "Failed to load shaders via pipeline");
        return;
    }
    UZ_LOG(Renderer, Info, "Shaders loaded: VS={} bytes, PS={} bytes",
           vsBytecode.size(), psBytecode.size());

    {
        RHIShaderDesc vsDesc{};
        vsDesc.bytecode = vsBytecode.data();
        vsDesc.bytecodeSize = vsBytecode.size();
        vsDesc.stage = RHIShaderStage::Vertex;
        vsDesc.entryPoint = "main";
        vsDesc.debugName = "textured_vs";

        RHIShaderDesc psDesc{};
        psDesc.bytecode = psBytecode.data();
        psDesc.bytecodeSize = psBytecode.size();
        psDesc.stage = RHIShaderStage::Pixel;
        psDesc.entryPoint = "main";
        psDesc.debugName = "textured_ps";

        vsHandle_ = device->CreateShader(vsDesc);
        psHandle_ = device->CreateShader(psDesc);
    }
#elif defined(__APPLE__)
    {
        RHIShaderDesc vsDesc{};
        vsDesc.bytecode = reinterpret_cast<const uint8_t*>(kTexturedQuadVSSource);
        vsDesc.bytecodeSize = sizeof(kTexturedQuadVSSource) - 1;
        vsDesc.stage = RHIShaderStage::Vertex;
        vsDesc.entryPoint = "vertex_main";
        vsDesc.debugName = "textured_quad_vs_msl";

        RHIShaderDesc psDesc{};
        psDesc.bytecode = reinterpret_cast<const uint8_t*>(kTexturedQuadPSSource);
        psDesc.bytecodeSize = sizeof(kTexturedQuadPSSource) - 1;
        psDesc.stage = RHIShaderStage::Pixel;
        psDesc.entryPoint = "fragment_main";
        psDesc.debugName = "textured_quad_ps_msl";

        vsHandle_ = device->CreateShader(vsDesc);
        psHandle_ = device->CreateShader(psDesc);
    }
#endif

    if (!vsHandle_.IsValid() || !psHandle_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create shader handles");
    }
}

void TexturedQuadDemo::CreatePipeline() {
    auto* device = GetDevice();

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
    pipeDesc.depthStencil.depthEnable = false;
    pipeDesc.renderTargetFormats[0] = GetSwapChain()->GetFormat();
    pipeDesc.renderTargetCount = 1;
    pipeDesc.debugName = "TexturedQuadPipeline";

    if (!bindless_) {
        pipeDesc.inputElements = inputElements;
        pipeDesc.inputElementCount = 3;
        pipeDesc.vertexStride = sizeof(SimpleVertex);
    }

    pipeline_ = device->CreateGraphicsPipeline(pipeDesc);
    if (!pipeline_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create textured quad pipeline");
    }
}

void TexturedQuadDemo::CreateVertexBuffer() {
    auto* device = GetDevice();

    SimpleVertex vertices[kVertexCount];
    GetQuadVertices(vertices);

    RHIBufferDesc vbDesc{};
    vbDesc.sizeBytes = sizeof(vertices);
    vbDesc.stride = sizeof(SimpleVertex);
    vbDesc.usage = bindless_ ? RHIBufferUsage::StructuredBuffer : RHIBufferUsage::VertexBuffer;
    vbDesc.memory = RHIMemoryType::Upload;
    vbDesc.debugName = "QuadVertexBuffer";

    vbHandle_ = device->CreateBuffer(vbDesc);
    if (!vbHandle_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create vertex buffer");
        return;
    }

    void* mapped = device->MapBuffer(vbHandle_);
    if (!mapped) {
        UZ_LOG(Renderer, Error, "Failed to map vertex buffer");
        return;
    }
    std::memcpy(mapped, vertices, sizeof(vertices));
    device->UnmapBuffer(vbHandle_);

    if (bindless_) {
        vbSrvIndex_ = device->GetBufferSRVIndex(vbHandle_);
        UZ_LOG(Renderer, Info, "Vertex buffer SRV index: {}", vbSrvIndex_);
    }
}

void TexturedQuadDemo::CreateCheckerboardTexture() {
    auto* device = GetDevice();
    auto* cmdList = GetCommandList();

    // TextureLoader doesn't support raw pixel upload (only encoded images),
    // so we use UploadContext directly for the procedural checkerboard.
    RHITextureDesc texDesc{};
    texDesc.width = kTexWidth;
    texDesc.height = kTexHeight;
    texDesc.format = RHIFormat::RGBA8Unorm;
    texDesc.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
    texDesc.debugName = "CheckerboardTexture";

    texHandle_ = device->CreateTexture(texDesc);
    if (!texHandle_.IsValid()) {
        UZ_LOG(Renderer, Error, "Failed to create checkerboard texture");
        return;
    }

    texSrvIndex_ = device->GetTextureSRVIndex(texHandle_);
    UZ_LOG(Renderer, Info, "Texture SRV index: {}", texSrvIndex_);

    auto pixels = GenerateCheckerboard(kTexWidth, kTexHeight, kTileSize);

    // Upload into the already-open command list (DemoApplication opens it before OnInit).
    // Flush immediately so staging buffer stays alive until GPU completes, then re-open
    // for DemoApplication's post-OnInit work (stats overlay init).
    {
        UploadContext upload(device);

        if (!upload.UploadTexture(cmdList, texHandle_, pixels.data(), pixels.size())) {
            UZ_LOG(Renderer, Error, "Failed to upload checkerboard texture");
            return;
        }

        RHIBarrierDesc toShaderResource{};
        toShaderResource.texture = texHandle_;
        toShaderResource.stateBefore = RHIResourceState::CopyDst;
        toShaderResource.stateAfter = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&toShaderResource, 1);

        cmdList->End();
        cmdList->Execute();
        device->WaitIdle();
        upload.ReleaseStaging();

        cmdList->Begin();
    }
    UZ_LOG(Renderer, Info, "Checkerboard texture uploaded ({}x{})", kTexWidth, kTexHeight);
}

} // namespace uz
