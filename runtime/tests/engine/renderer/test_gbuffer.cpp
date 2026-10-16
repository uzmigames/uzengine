/// Tests for GBuffer default state, IsValid() predicate, and field defaults.
///
/// Struct tests are CPU-only; MaterialContextDraw uses a real DX12 device.
/// They verify the zero-initialized struct field layout, the three-handle
/// validity contract of IsValid(), and move semantics on default instances.

#include <gtest/gtest.h>

#include "gbuffer.h"
#include "rhi_types.h"

#include <cstdint>
#include <limits>
#include <utility>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — texture handles
// ─────────────────────────────────────────────────────────────────────────────

/// Default-constructed GBuffer albedo handle must be invalid.
TEST(GBuffer, Default_AlbedoHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.albedo.IsValid())
        << "Default GBuffer albedo handle must be invalid";
}

/// Default-constructed GBuffer normal handle must be invalid.
TEST(GBuffer, Default_NormalHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.normal.IsValid())
        << "Default GBuffer normal handle must be invalid";
}

/// Default-constructed GBuffer depth handle must be invalid.
TEST(GBuffer, Default_DepthHandle_IsInvalid) {
    GBuffer gb;
    EXPECT_FALSE(gb.depth.IsValid())
        << "Default GBuffer depth handle must be invalid";
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — SRV indices
// ─────────────────────────────────────────────────────────────────────────────

/// Default albedoSRV must be UINT32_MAX.
TEST(GBuffer, Default_AlbedoSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.albedoSRV, UINT32_MAX)
        << "albedoSRV must default to UINT32_MAX (unassigned sentinel)";
}

/// Default normalSRV must be UINT32_MAX.
TEST(GBuffer, Default_NormalSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.normalSRV, UINT32_MAX);
}

/// Default depthSRV must be UINT32_MAX.
TEST(GBuffer, Default_DepthSRV_IsMaxUint32) {
    GBuffer gb;
    EXPECT_EQ(gb.depthSRV, UINT32_MAX);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer default state — dimensions
// ─────────────────────────────────────────────────────────────────────────────

/// Default width must be 0.
TEST(GBuffer, Default_Width_IsZero) {
    GBuffer gb;
    EXPECT_EQ(gb.width, 0u);
}

/// Default height must be 0.
TEST(GBuffer, Default_Height_IsZero) {
    GBuffer gb;
    EXPECT_EQ(gb.height, 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer::IsValid()
// ─────────────────────────────────────────────────────────────────────────────

/// Default GBuffer must not be valid.
TEST(GBuffer, Default_IsValid_ReturnsFalse) {
    GBuffer gb;
    EXPECT_FALSE(gb.IsValid())
        << "Default GBuffer must report IsValid() == false";
}

/// GBuffer with only albedo valid must not be valid (all three required).
TEST(GBuffer, OnlyAlbedoValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u;
    gb.albedo.generation = 1u;
    EXPECT_FALSE(gb.IsValid())
        << "IsValid() requires all three handles to be valid";
}

/// GBuffer with only normal valid must not be valid.
TEST(GBuffer, OnlyNormalValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.normal.index      = 1u;
    gb.normal.generation = 1u;
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with only depth valid must not be valid.
TEST(GBuffer, OnlyDepthValid_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.depth.index      = 1u;
    gb.depth.generation = 1u;
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with two of three handles valid (missing depth) must not be valid.
TEST(GBuffer, TwoHandlesValid_MissingDepth_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    // depth intentionally left invalid
    EXPECT_FALSE(gb.IsValid());
}

/// GBuffer with all six handles valid must be valid.
TEST(GBuffer, AllSixHandlesValid_IsValid_ReturnsTrue) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    gb.depth.index       = 3u; gb.depth.generation       = 1u;
    gb.velocity.index    = 4u; gb.velocity.generation    = 1u;
    gb.positionWS.index  = 5u; gb.positionWS.generation  = 1u;
    gb.emissive.index    = 6u; gb.emissive.generation    = 1u;
    EXPECT_TRUE(gb.IsValid())
        << "IsValid() must return true when all six texture handles are valid";
}

/// Invalidating one handle on a fully-valid GBuffer must make IsValid() false.
TEST(GBuffer, AfterInvalidatingOneHandle_IsValid_ReturnsFalse) {
    GBuffer gb;
    gb.albedo.index      = 1u; gb.albedo.generation      = 1u;
    gb.normal.index      = 2u; gb.normal.generation      = 1u;
    gb.depth.index       = 3u; gb.depth.generation       = 1u;
    gb.velocity.index    = 4u; gb.velocity.generation    = 1u;
    gb.positionWS.index  = 5u; gb.positionWS.generation  = 1u;
    gb.emissive.index    = 6u; gb.emissive.generation    = 1u;
    ASSERT_TRUE(gb.IsValid());

    gb.normal.generation = 0u; // invalidate normal
    EXPECT_FALSE(gb.IsValid());
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — SRV index round-trips
// ─────────────────────────────────────────────────────────────────────────────

/// albedoSRV stores assigned value without modification.
TEST(GBuffer, AlbedoSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.albedoSRV = 10u;
    EXPECT_EQ(gb.albedoSRV, 10u);
}

/// normalSRV stores assigned value without modification.
TEST(GBuffer, NormalSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.normalSRV = 11u;
    EXPECT_EQ(gb.normalSRV, 11u);
}

/// depthSRV stores assigned value without modification.
TEST(GBuffer, DepthSRV_Assignment_RoundTrip) {
    GBuffer gb;
    gb.depthSRV = 13u;
    EXPECT_EQ(gb.depthSRV, 13u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — dimension field round-trips
// ─────────────────────────────────────────────────────────────────────────────

/// Width field stores assigned value correctly.
TEST(GBuffer, Width_Assignment_RoundTrip) {
    GBuffer gb;
    gb.width = 1920u;
    EXPECT_EQ(gb.width, 1920u);
}

/// Height field stores assigned value correctly.
TEST(GBuffer, Height_Assignment_RoundTrip) {
    GBuffer gb;
    gb.height = 1080u;
    EXPECT_EQ(gb.height, 1080u);
}

/// Width and height together round-trip for a 4K resolution.
TEST(GBuffer, WidthHeight_4K_RoundTrip) {
    GBuffer gb;
    gb.width  = 3840u;
    gb.height = 2160u;
    EXPECT_EQ(gb.width,  3840u);
    EXPECT_EQ(gb.height, 2160u);
}

// ─────────────────────────────────────────────────────────────────────────────
// GBuffer — move semantics on default instances
// ─────────────────────────────────────────────────────────────────────────────

/// Move-constructing from a default GBuffer produces a default GBuffer.
TEST(GBuffer, MoveConstruct_FromDefault_ProducesDefaultState) {
    GBuffer src;
    GBuffer dst(std::move(src));

    EXPECT_FALSE(dst.IsValid());
    EXPECT_EQ(dst.albedoSRV,     UINT32_MAX);
    EXPECT_EQ(dst.normalSRV,     UINT32_MAX);
    EXPECT_EQ(dst.depthSRV,      UINT32_MAX);
    EXPECT_EQ(dst.width,  0u);
    EXPECT_EQ(dst.height, 0u);
}

/// Move-assigning a default GBuffer into another default GBuffer is a no-op / safe.
TEST(GBuffer, MoveAssign_DefaultIntoDefault_IsNoOp) {
    GBuffer a;
    GBuffer b;
    b = std::move(a);

    EXPECT_FALSE(b.IsValid());
    EXPECT_EQ(b.albedoSRV,     UINT32_MAX);
    EXPECT_EQ(b.normalSRV,     UINT32_MAX);
    EXPECT_EQ(b.depthSRV,      UINT32_MAX);
}

/// Move-constructing from a fully-valid GBuffer transfers all handles and SRVs.
TEST(GBuffer, MoveConstruct_FromValid_TransfersAllFields) {
    GBuffer src;
    src.albedo.index      = 1u; src.albedo.generation      = 1u;
    src.normal.index      = 2u; src.normal.generation      = 1u;
    src.depth.index       = 3u; src.depth.generation       = 1u;
    src.velocity.index    = 4u; src.velocity.generation    = 1u;
    src.positionWS.index  = 5u; src.positionWS.generation  = 1u;
    src.emissive.index    = 6u; src.emissive.generation    = 1u;
    src.albedoSRV         = 10u;
    src.normalSRV         = 11u;
    src.depthSRV          = 12u;
    src.width             = 1920u;
    src.height            = 1080u;

    GBuffer dst(std::move(src));

    EXPECT_TRUE(dst.IsValid());
    EXPECT_EQ(dst.albedoSRV,     10u);
    EXPECT_EQ(dst.normalSRV,     11u);
    EXPECT_EQ(dst.depthSRV,      12u);
    EXPECT_EQ(dst.width,         1920u);
    EXPECT_EQ(dst.height,        1080u);
}

#ifdef _WIN32
#include "rhi.h"
#include "rhi_backend.h"
#include "shader_compiler.h"
#include "engine_shaders.h"
#include "constant_buffers.h"
#include "root_slots.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "engine/material/compiler/hlsl_material_translator.h"

#include <array>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

namespace {

// Compile the shipped mesh VS and inject a real translated graph into the
// shipped G-buffer template. No test shader duplicates the context call.
class MaterialContextDraw : public ::testing::Test {
protected:
    std::unique_ptr<IRHIDevice> device;
    ShaderCompiler compiler;
    IRHICommandList* cmd = nullptr;
    std::vector<RHIBufferHandle> buffers;
    std::vector<RHITextureHandle> textures;
    std::vector<RHIShaderHandle> shaders;
    std::vector<RHIPipelineHandle> pipelines;
    std::vector<GPUMesh> meshes;
    RHIPipelineHandle pipeline;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend = RHIBackend::DX12;
        desc.enableValidation = true;
        device = CreateRHIDevice(desc);
        if (!device) GTEST_SKIP() << "DX12 device unavailable";
        ASSERT_TRUE(device->IsBindless()) << "F-004 color streams require SM6.6";
        ASSERT_TRUE(compiler.Init()) << "DXC must be available beside the test executable";
        cmd = device->CreateCommandList(RHIQueueType::Graphics);
        ASSERT_NE(cmd, nullptr);
    }

    void TearDown() override {
        if (!device) return;
        device->WaitIdle();
        if (cmd) device->DestroyCommandList(cmd);
        for (auto p : pipelines) device->DestroyPipeline(p);
        for (auto s : shaders) device->DestroyShader(s);
        for (auto& m : meshes) DestroyGPUMesh(device.get(), m);
        for (auto t : textures) device->DestroyTexture(t);
        for (auto b : buffers) device->DestroyBuffer(b);
    }

    void CompileGraph(MatExprType context, bool fractional = false, bool alpha = false) {
        MaterialGraph graph;
        uint32_t node = graph.AddExpression(context).id;
        uint32_t pin = 0;
        if (alpha) {
            // VertexColor output 0 is RGBA; extract A into all three channels.
            auto& mask = graph.AddExpression(MatExprType::ComponentMask);
            const uint32_t maskId = mask.id;
            mask.intParams["r"] = 0; mask.intParams["g"] = 0;
            mask.intParams["b"] = 0; mask.intParams["a"] = 1;
            mask.outputs[0].type = MatPinType::Float;
            ASSERT_TRUE(graph.Connect(node, pin, maskId, 0));
            node = maskId;
        }
        if (fractional) {
            const uint32_t frac = graph.AddExpression(MatExprType::Frac).id;
            ASSERT_TRUE(graph.Connect(node, pin, frac, 0));
            node = frac;
        }
        ASSERT_TRUE(graph.Connect(node, pin, graph.GetPBROutput().id, 0));
        HlslMaterialTranslator translator;
        const auto material = translator.Compile(graph);
        ASSERT_TRUE(material.success) << material.errorMessage;
        if (context == MatExprType::VertexColor)
            EXPECT_NE(material.contextMask & MaterialCompileResult::VertexColor, 0u);

        std::string ps = EngineShaders::GetSource(EngineShader::GbufferPS);
        const std::string marker = "#include \"material_body.hlsli\"";
        const auto pos = ps.find(marker);
        ASSERT_NE(pos, std::string::npos);
        ps.replace(pos, marker.size(), material.hlslSource);
        RHIShaderHandle stages[2];
        for (int i = 0; i < 2; ++i) {
            ShaderCompileDesc desc{};
            desc.source = i == 0 ? EngineShaders::GetSource(EngineShader::MeshVS) : ps;
            desc.sourceName = i == 0 ? "material_context_vs.hlsl" : "material_context_ps.hlsl";
            desc.stage = i == 0 ? ShaderStageCompile::Vertex : ShaderStageCompile::Pixel;
            desc.defines = {"UZ_BINDLESS=1", "UZ_MATERIAL_GRAPH=1"};
            const auto result = compiler.Compile(desc);
            ASSERT_TRUE(result.success) << result.errors;
            RHIShaderDesc shader{};
            shader.stage = i == 0 ? RHIShaderStage::Vertex : RHIShaderStage::Pixel;
            shader.bytecode = result.bytecode.data();
            shader.bytecodeSize = result.bytecode.size();
            stages[i] = device->CreateShader(shader);
            ASSERT_TRUE(stages[i].IsValid());
            shaders.push_back(stages[i]);
        }
        RHIGraphicsPipelineDesc desc{};
        desc.vertexShader = stages[0];
        desc.pixelShader = stages[1];
        desc.rasterizer.cullMode = RHICullMode::None;
        desc.depthStencil.depthEnable = false;
        desc.depthStencil.depthWrite = false;
        // Float RT0 preserves pixel-centre UV and signed camera vectors. The
        // production shader and all four MRT outputs remain unchanged.
        desc.renderTargetCount = 4;
        desc.renderTargetFormats[0] = RHIFormat::RGBA32Float;
        desc.renderTargetFormats[1] = RHIFormat::RGBA16Float;
        desc.renderTargetFormats[2] = RHIFormat::RGBA32Float;
        desc.renderTargetFormats[3] = RHIFormat::RGBA16Float;
        pipeline = device->CreateGraphicsPipeline(desc);
        ASSERT_TRUE(pipeline.IsValid());
        pipelines.push_back(pipeline);
    }

    void Draw(uint32_t width, uint32_t height, const PerFrameCB& frame,
              const PerObjectCB& object, const std::vector<uint32_t>& colors,
              std::vector<std::array<float, 4>>& pixels) {
        Mesh mesh{};
        mesh.header.magic = kMeshMagic;
        mesh.header.version = kMeshVersion;
        mesh.header.vertexFormat = MeshVertexFormat::Standard;
        mesh.header.vertexStride = sizeof(Vertex);
        mesh.header.vertexCount = 3;
        mesh.header.lodCount = 1;
        // Oversized triangle covers every pixel, including translated objects.
        const Vertex vertices[] = {
            {{-4, -4, 0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
            {{12, -4, 0.5f}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
            {{-4, 12, 0.5f}, {0, 0, 1}, {0, 1}, {1, 0, 0, 1}},
        };
        mesh.vertexData.resize(sizeof(vertices));
        std::memcpy(mesh.vertexData.data(), vertices, sizeof(vertices));
        mesh.vertexColors = colors;
        cmd->Begin();
        auto gpu = UploadMesh(device.get(), cmd, mesh);
        ASSERT_TRUE(gpu.IsValid());
        meshes.push_back(gpu);

        auto makeBuffer = [&](uint64_t size, RHIMemoryType memory, RHIBufferUsage usage) {
            RHIBufferDesc desc{};
            desc.sizeBytes = size; desc.memory = memory; desc.usage = usage;
            auto buffer = device->CreateBuffer(desc);
            if (buffer.IsValid()) buffers.push_back(buffer);
            return buffer;
        };
        const auto frameBuffer = makeBuffer(sizeof(frame), RHIMemoryType::Upload, RHIBufferUsage::ConstantBuffer);
        const auto objectBuffer = makeBuffer(sizeof(object), RHIMemoryType::Upload, RHIBufferUsage::ConstantBuffer);
        const auto materialBuffer = makeBuffer(256, RHIMemoryType::Upload, RHIBufferUsage::ConstantBuffer);
        ASSERT_TRUE(frameBuffer.IsValid());
        ASSERT_TRUE(objectBuffer.IsValid());
        ASSERT_TRUE(materialBuffer.IsValid());
        void* data = device->MapBuffer(frameBuffer);
        ASSERT_NE(data, nullptr);
        std::memcpy(data, &frame, sizeof(frame));
        data = device->MapBuffer(objectBuffer);
        ASSERT_NE(data, nullptr);
        std::memcpy(data, &object, sizeof(object));
        data = device->MapBuffer(materialBuffer);
        ASSERT_NE(data, nullptr);
        std::memset(data, 0, 256);

        RHIRenderPassDesc pass{};
        pass.colorAttachmentCount = 4;
        RHIBarrierDesc targetBarriers[4]{};
        const RHIFormat formats[] = {RHIFormat::RGBA32Float, RHIFormat::RGBA16Float,
                                    RHIFormat::RGBA32Float, RHIFormat::RGBA16Float};
        for (uint32_t i = 0; i < 4; ++i) {
            RHITextureDesc desc{};
            desc.width = width; desc.height = height; desc.format = formats[i];
            desc.usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySrc;
            auto target = device->CreateTexture(desc);
            ASSERT_TRUE(target.IsValid());
            textures.push_back(target);
            pass.colorAttachments[i].texture = target;
            pass.colorAttachments[i].clear = true;
            pass.colorAttachments[i].clearColor[0] = -99.0f;
            targetBarriers[i].texture = target;
            targetBarriers[i].stateBefore = RHIResourceState::Common;
            targetBarriers[i].stateAfter = RHIResourceState::RenderTarget;
        }
        const uint32_t pitch = (width * 16 + 255) & ~255u;
        const auto readback = makeBuffer(uint64_t(pitch) * height, RHIMemoryType::Readback, RHIBufferUsage::CopyDst);
        ASSERT_TRUE(readback.IsValid());
        cmd->SetPipeline(pipeline);
        cmd->SetRootCBV(kSlotPerFrame, device->GetBufferGPUAddress(frameBuffer));
        cmd->SetRootCBV(kSlotPerObject, device->GetBufferGPUAddress(objectBuffer));
        cmd->SetRootCBV(kSlotMaterialParams, device->GetBufferGPUAddress(materialBuffer));
        const uint32_t push[] = {gpu.vbSrvIndex, 0, gpu.colorSrvIndex, 0};
        cmd->SetRootConstants(kSlotRootConstants, push, 4);
        cmd->SetViewport({0, 0, float(width), float(height), 0, 1});
        cmd->SetScissor({0, 0, int32_t(width), int32_t(height)});
        cmd->ResourceBarrier(targetBarriers, 4);
        cmd->BeginRenderPass(pass);
        cmd->Draw(3);
        cmd->EndRenderPass();
        RHIBarrierDesc barrier{};
        barrier.texture = pass.colorAttachments[0].texture;
        barrier.stateBefore = RHIResourceState::RenderTarget;
        barrier.stateAfter = RHIResourceState::CopySrc;
        cmd->ResourceBarrier(&barrier, 1);
        cmd->CopyTextureToBuffer(barrier.texture, 0, readback, 0, pitch);
        cmd->End();
        cmd->Execute();
        device->WaitIdle();
        const auto* mapped = static_cast<const uint8_t*>(device->MapBuffer(readback));
        ASSERT_NE(mapped, nullptr);
        pixels.resize(size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y)
            std::memcpy(pixels.data() + size_t(y) * width, mapped + size_t(y) * pitch, width * 16);
    }
};

TEST_F(MaterialContextDraw, ScreenUV_TwoResolutions_Match) {
    ASSERT_NO_FATAL_FAILURE(CompileGraph(MatExprType::ScreenPosition));
    std::array<float, 2> reference{};
    const std::array<uint32_t, 2> sizes[] = {{1280, 720}, {1920, 1080}, {640, 360}, {960, 540}};
    for (const auto& size : sizes) {
        const auto w = size[0], h = size[1];
        SCOPED_TRACE(::testing::Message() << w << "x" << h);
        PerFrameCB frame{};
        // Nonzero projection jitter must still agree with raster pixel centres.
        frame.viewProj.m._41 = 0.37f / w;
        frame.viewProj.m._42 = -0.23f / h;
        std::vector<std::array<float, 4>> pixels;
        ASSERT_NO_FATAL_FAILURE(Draw(w, h, frame, PerObjectCB{}, {}, pixels));
        for (uint32_t y : {0u, h / 4, h / 2, h - 1})
            for (uint32_t x : {0u, w / 4, w / 2, w - 1}) {
                const auto& p = pixels[size_t(y) * w + x];
                EXPECT_NEAR(p[0], (x + 0.5f) / w, 1.0f / w);
                EXPECT_NEAR(p[1], (y + 0.5f) / h, 1.0f / h);
            }
        const auto& p = pixels[size_t(h / 4) * w + w / 4];
        if (w == 1280) reference = {p[0], p[1]};
        else {
            EXPECT_NEAR(p[0], reference[0], 1.0f / w + 1.0f / 1280);
            EXPECT_NEAR(p[1], reference[1], 1.0f / h + 1.0f / 720);
        }
    }
}

TEST_F(MaterialContextDraw, Time_AdvancesWithFrames) {
    ASSERT_NO_FATAL_FAILURE(CompileGraph(MatExprType::Time, true));
    constexpr float dt = 0.125f;
    float previous = 0;
    for (uint32_t i = 0; i < 4; ++i) {
        PerFrameCB frame{};
        frame.timeParams = {2.25f + i * dt, dt, float(i), 0};
        std::vector<std::array<float, 4>> pixels;
        ASSERT_NO_FATAL_FAILURE(Draw(32, 32, frame, PerObjectCB{}, {}, pixels));
        const auto& p = pixels[16 * 32 + 16];
        for (int c = 0; c < 3; ++c) EXPECT_NEAR(p[c], 0.25f + i * dt, 1e-5f);
        if (i) EXPECT_NEAR(p[0] - previous, dt, 1e-5f);
        previous = p[0];
    }
}

TEST_F(MaterialContextDraw, CameraVector_FollowsCamera) {
    ASSERT_NO_FATAL_FAILURE(CompileGraph(MatExprType::CameraVector));
    for (float cameraX : {-2.0f, 2.0f}) {
        PerFrameCB frame{};
        frame.cameraPos = {cameraX, 1, 3, 0};
        std::vector<std::array<float, 4>> pixels;
        ASSERT_NO_FATAL_FAILURE(Draw(32, 32, frame, PerObjectCB{}, {}, pixels));
        // Identity projection: pixel (16,16) is world (1/32,-1/32,0.5).
        const float v[] = {cameraX - 1.0f / 32, 1 + 1.0f / 32, 2.5f};
        const float length = std::sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
        for (int c = 0; c < 3; ++c)
            EXPECT_NEAR(pixels[16 * 32 + 16][c], v[c] / length, 1e-5f);
    }
}

TEST_F(MaterialContextDraw, ObjectPosition_FollowsTransform) {
    ASSERT_NO_FATAL_FAILURE(CompileGraph(MatExprType::ObjectPosition));
    for (const Vec3 translation : {Vec3{0.1f, 0.2f, 0.15f}, Vec3{0.4f, 0.3f, 0.25f}}) {
        PerObjectCB object{};
        object.model = Mat4::Translation(translation);
        std::vector<std::array<float, 4>> pixels;
        ASSERT_NO_FATAL_FAILURE(Draw(32, 32, PerFrameCB{}, object, {}, pixels));
        const auto& p = pixels[16 * 32 + 16];
        EXPECT_NEAR(p[0], translation.x, 1e-5f);
        EXPECT_NEAR(p[1], translation.y, 1e-5f);
        EXPECT_NEAR(p[2], translation.z, 1e-5f);
    }
}

TEST_F(MaterialContextDraw, VertexColor_StreamDecoded_And_WhiteWithoutStream) {
    // Distinct channels expose RGBA ordering, UNORM scaling, and alpha decode.
    constexpr uint32_t rgba = 0xC78B4513u;
    for (bool alpha : {false, true}) {
        ASSERT_NO_FATAL_FAILURE(CompileGraph(MatExprType::VertexColor, false, alpha));
        for (bool present : {true, false}) {
            std::vector<std::array<float, 4>> pixels;
            const std::vector<uint32_t> colors = present ? std::vector<uint32_t>(3, rgba) : std::vector<uint32_t>{};
            ASSERT_NO_FATAL_FAILURE(Draw(32, 32, PerFrameCB{}, PerObjectCB{}, colors, pixels));
            for (int c = 0; c < 3; ++c) {
                const float expected = present ? float((rgba >> (8 * (alpha ? 3 : c))) & 255) / 255 : 1.0f;
                EXPECT_NEAR(pixels[16 * 32 + 16][c], expected, 1e-5f);
            }
        }
    }
}

} // namespace
#endif
