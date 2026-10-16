// F-002: previous transforms and production velocity shaders/pass readback.
#include "engine/renderer/core/gbuffer/draw_list.h"
#include "engine/renderer/core/gbuffer/prev_transform_cache.h"
#include "engine/renderer/passes/postprocess/velocity_pass.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace uz;

TEST(PrevTransformCache, FirstSight_ReturnsCurrent) {
    PrevTransformCache cache;
    const auto current = Mat4::Translation(10, 20, 30);
    EXPECT_EQ(cache.Lookup(7, current, 0), current);
    EXPECT_EQ(cache.Lookup(7, current, 100), current);
    EXPECT_FALSE(cache.HasPrevious(7, 100));
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(PrevTransformCache, NextFrame_ReturnsRecorded) {
    PrevTransformCache cache;
    const auto previous = Mat4::Translation(1, 2, 3);
    const auto current = Mat4::Translation(4, 5, 6);
    cache.Record(7, previous, 41);
    cache.Sweep(42);
    EXPECT_EQ(cache.Lookup(7, current, 42), previous);
    EXPECT_TRUE(cache.HasPrevious(7, 42));
    // Packed entity generation is part of the key, not just the entity index.
    EXPECT_EQ(cache.Lookup(0x100007, current, 42), current);
    EXPECT_EQ(cache.Lookup(7, current, 41), current);
    cache.Record(7, current, 42);
    EXPECT_EQ(cache.Lookup(7, previous, 43), current);
    EXPECT_EQ(cache.Size(), 1u);
}

TEST(PrevTransformCache, GapOfOneFrame_ReturnsCurrent_AndSweepEvicts) {
    PrevTransformCache cache;
    const auto current = Mat4::Translation(4, 5, 6);
    cache.Record(7, Mat4::Identity(), 40);
    cache.Record(8, current, 41);
    EXPECT_EQ(cache.Lookup(7, current, 42), current);
    EXPECT_FALSE(cache.HasPrevious(7, 42));
    cache.Sweep(42);
    EXPECT_EQ(cache.Size(), 1u);
    EXPECT_EQ(cache.Lookup(8, Mat4::Identity(), 42), current);
    cache.Sweep(43);
    EXPECT_EQ(cache.Size(), 0u);
}

TEST(PrevTransformCache, StaticMobility_NeverRecorded) {
    PrevTransformCache cache;
    for (const auto mobility : {EMobility::Static, EMobility::Stationary}) {
        DrawCommand draw{};
        draw.entityId = 7;
        draw.mobility = mobility;
        draw.worldMatrix = Mat4::Translation(1, 2, 3);
        FillPreviousTransform(draw, cache, 1, false, false);
        draw.worldMatrix = Mat4::Translation(4, 5, 6);
        FillPreviousTransform(draw, cache, 2, false, false);
        EXPECT_EQ(draw.prevWorldMatrix, draw.worldMatrix);
        EXPECT_FALSE(draw.hasObjectMotion);
        EXPECT_EQ(cache.Size(), 0u);
    }
}

TEST(PreviousTransform, MovableMotionGapAndDeformationFlags) {
    PrevTransformCache cache;
    DrawCommand draw{};
    draw.entityId = 19;
    FillPreviousTransform(draw, cache, 10, false, false);
    EXPECT_FALSE(draw.hasObjectMotion);
    const auto previous = draw.worldMatrix;
    draw.worldMatrix = Mat4::Translation(0.25f, 0, 0);
    FillPreviousTransform(draw, cache, 11, false, false);
    EXPECT_EQ(draw.prevWorldMatrix, previous);
    EXPECT_TRUE(draw.hasObjectMotion);
    FillPreviousTransform(draw, cache, 13, false, false);
    EXPECT_EQ(draw.prevWorldMatrix, draw.worldMatrix);
    EXPECT_FALSE(draw.hasObjectMotion);
    FillPreviousTransform(draw, cache, 14, true, false);
    EXPECT_TRUE(draw.hasObjectMotion);
    FillPreviousTransform(draw, cache, 15, false, true);
    EXPECT_TRUE(draw.hasObjectMotion);
}

namespace {
std::array<float, 2> ProjectPixels(const Mat4& matrix, const Vec3& point,
                                 float width, float height) {
    const Vec4 clip = matrix * Vec4{point.x, point.y, point.z, 1};
    return {(clip.x / clip.w * 0.5f + 0.5f) * width,
            (0.5f - clip.y / clip.w * 0.5f) * height};
}
} // namespace

TEST(VelocityConvention, NdcToRenderPixels_MatchesFullscreenPassFormula) {
    // Known clip positions with non-unit w exercise perspective division.
    const Vec4 current{0.4f, 0.2f, 0.5f, 2};
    const Vec4 previous{-0.2f, 0.6f, 0.5f, 2};
    constexpr float width = 640, height = 360;
    const float dx = (current.x / current.w - previous.x / previous.w) * 0.5f * width;
    const float dy = (current.y / current.w - previous.y / previous.w) * -0.5f * height;
    EXPECT_NEAR(dx, 96, 1e-5f);
    EXPECT_NEAR(dy, 36, 1e-5f);
    const float currentU = current.x / current.w * 0.5f + 0.5f;
    const float currentV = 0.5f - current.y / current.w * 0.5f;
    // TAA subtracts render-pixel velocity to recover previous UV.
    EXPECT_NEAR(currentU - dx / width, previous.x / previous.w * 0.5f + 0.5f, 1e-6f);
    EXPECT_NEAR(currentV - dy / height, 0.5f - previous.y / previous.w * 0.5f, 1e-6f);
    const auto currentPixels = ProjectPixels(Mat4::Translation(0.2f, -0.1f, 0),
                                             {0, 0, 0.5f}, width, height);
    const auto previousPixels = ProjectPixels(Mat4::Translation(-0.1f, 0.1f, 0),
                                              {0, 0, 0.5f}, width, height);
    EXPECT_NEAR(currentPixels[0] - previousPixels[0], dx, 1e-4f);
    EXPECT_NEAR(currentPixels[1] - previousPixels[1], dy, 1e-4f);
}

TEST(VelocityObjectCB, LayoutIs272Bytes) {
    EXPECT_EQ(sizeof(VelocityObjectCB), 272u);
    EXPECT_EQ(offsetof(VelocityObjectCB, model), 0u);
    EXPECT_EQ(offsetof(VelocityObjectCB, prevModel), 64u);
    EXPECT_EQ(offsetof(VelocityObjectCB, unjitteredViewProj), 128u);
    EXPECT_EQ(offsetof(VelocityObjectCB, prevViewProj), 192u);
    EXPECT_EQ(offsetof(VelocityObjectCB, screenSize), 256u);
    EXPECT_EQ(offsetof(VelocityObjectCB, pad), 264u);
}

#ifdef _WIN32
#include "rhi.h"
#include "rhi_backend.h"
#include "shader_compiler.h"
#include "engine_shaders.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "root_slots.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "frame/taa_jitter.h"

#include <memory>
#include <filesystem>

namespace {
float ReadHalf(uint16_t value) {
    const float magnitude = (value & 0x7c00u) == 0
        ? std::ldexp(float(value & 0x3ffu), -24)
        : std::ldexp(1.0f + float(value & 0x3ffu) / 1024.0f,
                     int((value >> 10) & 31u) - 15);
    return value & 0x8000u ? -magnitude : magnitude;
}

class VelocityObjectPass : public ::testing::Test {
protected:
    std::unique_ptr<IRHIDevice> device;
    ShaderCompiler compiler;
    CBRingAllocator ring;
    IRHICommandList* cmd = nullptr;
    std::vector<RHIShaderHandle> shaders;
    std::vector<RHIPipelineHandle> pipelines;
    std::vector<RHITextureHandle> textures;
    std::vector<RHIBufferHandle> buffers;
    std::vector<GPUMesh> meshes;
    RHIPipelineHandle depthPipeline, cameraPipeline, objectPipeline;
    uint64_t objectConstantBytes = 0;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend = RHIBackend::DX12;
        desc.enableValidation = true;
        device = CreateRHIDevice(desc);
        if (!device) GTEST_SKIP() << "DX12 device unavailable";
        ASSERT_TRUE(device->IsBindless()) << "Previous-pose SRVs require SM6.6";
        ASSERT_TRUE(compiler.Init()) << "DXC must be beside the test executable";
        ASSERT_TRUE(ring.Init(device.get(), 1, 64 * 1024));
        cmd = device->CreateCommandList(RHIQueueType::Graphics);
        ASSERT_NE(cmd, nullptr);

        RHIShaderHandle meshVS, objectVS, objectPS, fullscreenVS, cameraPS;
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::MeshVS, true, meshVS));
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::VelocityObjectVS, true, objectVS));
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::VelocityObjectPS, false, objectPS));
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::FullscreenVS, true, fullscreenVS));
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::VelocityPS, false, cameraPS));

        RHIGraphicsPipelineDesc pipeline{};
        pipeline.vertexShader = meshVS;
        pipeline.rasterizer.cullMode = RHICullMode::None;
        pipeline.renderTargetCount = 0;
        pipeline.depthStencilFormat = RHIFormat::D32Float;
        depthPipeline = device->CreateGraphicsPipeline(pipeline);
        ASSERT_TRUE(depthPipeline.IsValid());
        pipelines.push_back(depthPipeline);
        pipeline.vertexShader = objectVS;
        pipeline.pixelShader = objectPS;
        pipeline.renderTargetCount = 1;
        pipeline.renderTargetFormats[0] = RHIFormat::RG16Float;
        pipeline.depthStencil.depthWrite = false;
        pipeline.depthStencil.depthFunc = RHICompareFunc::Equal;
        objectPipeline = device->CreateGraphicsPipeline(pipeline);
        ASSERT_TRUE(objectPipeline.IsValid());
        pipelines.push_back(objectPipeline);
        pipeline.vertexShader = fullscreenVS;
        pipeline.pixelShader = cameraPS;
        pipeline.depthStencil.depthEnable = false;
        pipeline.depthStencilFormat = RHIFormat::Unknown;
        cameraPipeline = device->CreateGraphicsPipeline(pipeline);
        ASSERT_TRUE(cameraPipeline.IsValid());
        pipelines.push_back(cameraPipeline);
    }

    void TearDown() override {
        if (!device) return;
        device->WaitIdle();
        if (cmd) device->DestroyCommandList(cmd);
        for (auto pipeline : pipelines) device->DestroyPipeline(pipeline);
        for (auto shader : shaders) device->DestroyShader(shader);
        for (auto& mesh : meshes) DestroyGPUMesh(device.get(), mesh);
        for (auto texture : textures) device->DestroyTexture(texture);
        for (auto buffer : buffers) device->DestroyBuffer(buffer);
        ring.Shutdown();
    }

    void Compile(EngineShader source, bool vertex, RHIShaderHandle& shader) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(source);
        desc.sourceName = EngineShaders::GetName(source);
        desc.stage = vertex ? ShaderStageCompile::Vertex : ShaderStageCompile::Pixel;
        desc.defines = {"UZ_BINDLESS=1"};
        const auto result = compiler.Compile(desc);
        ASSERT_TRUE(result.success) << result.errors;
        RHIShaderDesc compiled{};
        compiled.stage = vertex ? RHIShaderStage::Vertex : RHIShaderStage::Pixel;
        compiled.bytecode = result.bytecode.data();
        compiled.bytecodeSize = result.bytecode.size();
        shader = device->CreateShader(compiled);
        ASSERT_TRUE(shader.IsValid());
        shaders.push_back(shader);
    }

    void UploadTriangle(float previousPoseOffset, GPUMesh& mesh, float boneAngle = 0.f) {
        Mesh cpu{};
        cpu.header.magic = kMeshMagic;
        cpu.header.version = kMeshVersion;
        cpu.header.vertexFormat = MeshVertexFormat::Standard;
        cpu.header.vertexStride = sizeof(Vertex);
        cpu.header.vertexCount = 3;
        cpu.header.lodCount = 1;
        Vertex vertices[] = {
            {{-0.75f + previousPoseOffset, -0.75f, 0.5f}, {0, 0, 1}, {0, 0}, {1, 0, 0, 1}},
            {{ 0.75f + previousPoseOffset, -0.75f, 0.5f}, {0, 0, 1}, {1, 0}, {1, 0, 0, 1}},
            {{previousPoseOffset, 0.75f, 0.5f}, {0, 0, 1}, {0.5f, 1}, {1, 0, 0, 1}},
        };
        // One bone, weight 1: linear blend skinning reduces to this transform.
        const auto bone = Mat4::RotationZ(boneAngle);
        for (auto& vertex : vertices) {
            const auto skinned = bone * Vec4{vertex.position.x, vertex.position.y, vertex.position.z, 1};
            vertex.position = {skinned.x, skinned.y, skinned.z};
        }
        cpu.vertexData.resize(sizeof(vertices));
        std::memcpy(cpu.vertexData.data(), vertices, sizeof(vertices));
        mesh = UploadMesh(device.get(), cmd, cpu);
        ASSERT_TRUE(mesh.IsValid());
        meshes.push_back(mesh);
    }

    void Render(uint32_t width, uint32_t height, DrawCommand draw,
                const Mat4& currentVP, const Mat4& previousVP, bool overlay,
                bool jitter, bool previousPose, std::vector<std::array<float, 2>>& pixels,
                bool particleConvention = false, bool boneRotation = false) {
        ring.BeginFrame(0); // Each preceding submission has completed before reuse.
        cmd->Begin();
        GPUMesh mesh;
        ASSERT_NO_FATAL_FAILURE(UploadTriangle(0, mesh));
        if (previousPose) {
            GPUMesh oldPose;
            ASSERT_NO_FATAL_FAILURE(UploadTriangle(boneRotation ? 0.f : -0.125f, oldPose,
                boneRotation ? -2.f * 3.14159265358979323846f / 180.f : 0.f));
            ASSERT_NE(oldPose.vbSrvIndex, 0u);
            draw.prevVertexBufferSrvIndex = oldPose.vbSrvIndex;
        }
        draw.meshId = 0;
        draw.twoSided = 1;
        DrawList draws;
        draws.Add(draw);
        PerFrameCB frame{};
        frame.viewProj = jitter
            ? ApplyTaaJitter(currentVP, 0.375f, -0.25f, float(width), float(height))
            : currentVP;
        frame.invViewProj = frame.viewProj.Inverse();
        const auto frameAlloc = ring.Allocate(sizeof(frame));
        ASSERT_NE(frameAlloc.cpuPtr, nullptr);
        std::memcpy(frameAlloc.cpuPtr, &frame, sizeof(frame));
        PerObjectCB object{};
        object.model = draw.worldMatrix;
        const auto objectAlloc = ring.Allocate(sizeof(object));
        ASSERT_NE(objectAlloc.cpuPtr, nullptr);
        std::memcpy(objectAlloc.cpuPtr, &object, sizeof(object));
        VelocityCB camera{};
        camera.prevViewProj = previousVP;
        camera.unjitteredInvViewProj = currentVP.Inverse();
        camera.screenWidth = float(width);
        camera.screenHeight = float(height);
        const auto cameraAlloc = ring.Allocate(sizeof(camera));
        ASSERT_NE(cameraAlloc.cpuPtr, nullptr);
        std::memcpy(cameraAlloc.cpuPtr, &camera, sizeof(camera));

        RHITextureDesc target{};
        target.width = width;
        target.height = height;
        target.format = RHIFormat::RG16Float;
        target.usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySrc;
        const auto velocity = device->CreateTexture(target);
        ASSERT_TRUE(velocity.IsValid());
        textures.push_back(velocity);
        target.format = RHIFormat::D32Float;
        target.usage = RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;
        const auto depth = device->CreateTexture(target);
        ASSERT_TRUE(depth.IsValid());
        textures.push_back(depth);
        const uint32_t pitch = (width * 4 + 255) & ~255u;
        RHIBufferDesc readbackDesc{};
        readbackDesc.sizeBytes = uint64_t(pitch) * height;
        readbackDesc.memory = RHIMemoryType::Readback;
        readbackDesc.usage = RHIBufferUsage::CopyDst;
        const auto readback = device->CreateBuffer(readbackDesc);
        ASSERT_TRUE(readback.IsValid());
        buffers.push_back(readback);

        rg::RgResourcePool pool(device.get());
        rg::RgBuilder graph(device.get(), &pool);
        const auto velocityRes = graph.ImportTexture("Velocity", velocity, RHIResourceState::Common);
        const auto depthRes = graph.ImportTexture("Depth", depth, RHIResourceState::DepthStencilWrite);
        graph.AddPass("MeshDepth", [&](rg::RgPassBuilder& pass) {
            pass.Write(depthRes, RHIResourceState::DepthStencilWrite);
            pass.SetExecutionCallback([&](IRHICommandList* list, const rg::RgPassResources&) {
                RHIRenderPassDesc render{};
                render.hasDepth = true;
                render.depthAttachment.texture = depth;
                render.depthAttachment.clear = true;
                list->BeginRenderPass(render);
                list->SetPipeline(depthPipeline);
                list->SetRootCBV(kSlotPerFrame, frameAlloc.gpuAddr);
                list->SetRootCBV(kSlotPerObject, objectAlloc.gpuAddr);
                const uint32_t push[] = {mesh.vbSrvIndex, 0, 0, 0};
                list->SetRootConstants(kSlotRootConstants, push, 4);
                list->SetViewport({0, 0, float(width), float(height), 0, 1});
                list->SetScissor({0, 0, int32_t(width), int32_t(height)});
                list->Draw(3);
                list->EndRenderPass();
            });
        });
        VelocityPassDesc fullscreen{};
        fullscreen.velocityTarget = velocityRes;
        fullscreen.depthBuffer = depthRes;
        fullscreen.velocityTexture = velocity;
        fullscreen.depthTexture = depth;
        fullscreen.depthSrvIndex = device->GetTextureSRVIndex(depth);
        ASSERT_NE(fullscreen.depthSrvIndex, UINT32_MAX);
        fullscreen.perFrameAlloc = frameAlloc;
        fullscreen.velocityCBAlloc = cameraAlloc;
        fullscreen.pipeline = cameraPipeline;
        if (particleConvention) {
            // Compile the shipped helper, not a C++ restatement: UV-unit output
            // would be 0.15/0.10 here instead of the required 9.6/4.8 pixels.
            ShaderCompileDesc source{};
            source.sourceName = "particle_velocity_units_ps.hlsl";
            source.stage = ShaderStageCompile::Pixel;
            source.source = R"(
#include "vfx_motion_vectors.hlsli"
float2 main() : SV_Target0 {
    return ComputeMotionVector(float4(0.4, 0.2, 0.5, 2),
                               float4(-0.2, 0.6, 0.5, 2), float2(64, 48));
})";
            const auto runtime = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path().parent_path();
            source.includeDirs.push_back((runtime / "shaders/runtime/vfx").string());
            const auto compiled = compiler.Compile(source);
            ASSERT_TRUE(compiled.success) << compiled.errors;
            RHIShaderDesc ps{};
            ps.stage = RHIShaderStage::Pixel;
            ps.bytecode = compiled.bytecode.data();
            ps.bytecodeSize = compiled.bytecode.size();
            const auto shader = device->CreateShader(ps);
            ASSERT_TRUE(shader.IsValid());
            shaders.push_back(shader);
            RHIGraphicsPipelineDesc pipeline{};
            pipeline.vertexShader = shaders[3]; // Shipped fullscreen VS, compiled in SetUp.
            pipeline.pixelShader = shader;
            pipeline.rasterizer.cullMode = RHICullMode::None;
            pipeline.depthStencil.depthEnable = false;
            pipeline.renderTargetCount = 1;
            pipeline.renderTargetFormats[0] = RHIFormat::RG16Float;
            fullscreen.pipeline = device->CreateGraphicsPipeline(pipeline);
            ASSERT_TRUE(fullscreen.pipeline.IsValid());
            pipelines.push_back(fullscreen.pipeline);
        }
        fullscreen.viewportWidth = float(width);
        fullscreen.viewportHeight = float(height);
        fullscreen.bindless = true;
        AddVelocityPass(graph, fullscreen);
        const auto before = ring.Allocate(256);
        ASSERT_NE(before.cpuPtr, nullptr);
        if (overlay) {
            VelocityObjectPassDesc pass{};
            pass.velocityTarget = velocityRes;
            pass.depthBuffer = depthRes;
            pass.perFrameAlloc = frameAlloc;
            pass.cbAllocator = &ring;
            pass.drawList = &draws;
            pass.meshes = &mesh;
            pass.meshCount = 1;
            pass.pipeline = pass.twoSidedPipeline = objectPipeline;
            pass.unjitteredViewProj = currentVP;
            pass.prevViewProj = previousVP;
            pass.viewportWidth = float(width);
            pass.viewportHeight = float(height);
            AddVelocityObjectPass(graph, pass);
        }
        graph.Compile();
        graph.Execute(cmd);
        const auto after = ring.Allocate(256);
        ASSERT_NE(after.cpuPtr, nullptr);
        objectConstantBytes = after.offsetBytes - before.offsetBytes - 256;
        RHIBarrierDesc copy{};
        copy.texture = velocity;
        copy.stateBefore = RHIResourceState::RenderTarget;
        copy.stateAfter = RHIResourceState::CopySrc;
        cmd->ResourceBarrier(&copy, 1);
        cmd->CopyTextureToBuffer(velocity, 0, readback, 0, pitch);
        cmd->End();
        cmd->Execute();
        device->WaitIdle();
        const auto* mapped = static_cast<const uint8_t*>(device->MapBuffer(readback));
        ASSERT_NE(mapped, nullptr);
        pixels.resize(size_t(width) * height);
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                uint16_t values[2];
                std::memcpy(values, mapped + size_t(y) * pitch + x * 4, sizeof(values));
                pixels[size_t(y) * width + x] = {ReadHalf(values[0]), ReadHalf(values[1])};
            }
        }
        device->UnmapBuffer(readback);
    }

    void ExpectInterior(const std::vector<std::array<float, 2>>& pixels,
                        uint32_t width, uint32_t height, float dx, float dy) {
        ASSERT_EQ(pixels.size(), size_t(width) * height);
        // Central 4x4 region is strictly inside the triangle for every pose.
        for (uint32_t y = height / 2 - 2; y < height / 2 + 2; ++y)
            for (uint32_t x = width / 2 - 2; x < width / 2 + 2; ++x) {
                EXPECT_NEAR(pixels[size_t(y) * width + x][0], dx, 0.1f);
                EXPECT_NEAR(pixels[size_t(y) * width + x][1], dy, 0.1f);
            }
        EXPECT_FLOAT_EQ(pixels.front()[0], 0);
        EXPECT_FLOAT_EQ(pixels.front()[1], 0);
        EXPECT_FLOAT_EQ(pixels.back()[0], 0);
        EXPECT_FLOAT_EQ(pixels.back()[1], 0);
    }
};

TEST_F(VelocityObjectPass, TranslatingTriangle_FixedCamera_MatchesProjection) {
    DrawCommand draw{};
    draw.worldMatrix = Mat4::Translation(0.125f, -0.0625f, 0);
    draw.hasObjectMotion = true;
    const auto current = ProjectPixels(draw.worldMatrix, {0, 0, 0.5f}, 64, 48);
    const auto previous = ProjectPixels(draw.prevWorldMatrix, {0, 0, 0.5f}, 64, 48);
    for (bool jitter : {false, true}) {
        std::vector<std::array<float, 2>> pixels;
        ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, Mat4{}, Mat4{}, true, jitter, false, pixels));
        EXPECT_EQ(objectConstantBytes, 512u);
        ExpectInterior(pixels, 64, 48, current[0] - previous[0], current[1] - previous[1]);
    }
}

TEST_F(VelocityObjectPass, MovingCamera_SumsCameraAndObject) {
    DrawCommand draw{};
    draw.worldMatrix = Mat4::Translation(0.125f, 0, 0);
    draw.hasObjectMotion = true;
    const auto camera = Mat4::Translation(-0.0625f, 0.03125f, 0);
    const auto current = ProjectPixels(draw.worldMatrix * camera, {0, 0, 0.5f}, 64, 48);
    const auto previous = ProjectPixels(draw.prevWorldMatrix, {0, 0, 0.5f}, 64, 48);
    std::vector<std::array<float, 2>> pixels;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, camera, Mat4{}, true, false, false, pixels));
    ExpectInterior(pixels, 64, 48, current[0] - previous[0], current[1] - previous[1]);
}

TEST_F(VelocityObjectPass, PerspectiveTranslation_JitteredAndUnjittered_MatchesProjection) {
    DrawCommand draw{};
    draw.worldMatrix = Mat4::Translation(0.0625f, -0.03125f, 0);
    draw.hasObjectMotion = true;
    const auto projection = Mat4::Perspective(1.2f, 64.f / 48.f, 0.1f, 100.f);
    const auto current = ProjectPixels(draw.worldMatrix * projection, {0, 0, 0.5f}, 64, 48);
    const auto previous = ProjectPixels(projection, {0, 0, 0.5f}, 64, 48);
    for (const bool jitter : {false, true}) {
        std::vector<std::array<float, 2>> pixels;
        ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, projection, projection,
            true, jitter, false, pixels));
        for (uint32_t y = 22; y < 26; ++y)
            for (uint32_t x = 30; x < 34; ++x) {
                EXPECT_NEAR(pixels[y * 64 + x][0], current[0] - previous[0], 0.1f);
                EXPECT_NEAR(pixels[y * 64 + x][1], current[1] - previous[1], 0.1f);
            }
    }
}

TEST_F(VelocityObjectPass, StaticTriangle_NotRedrawn) {
    DrawCommand draw{};
    PrevTransformCache cache;
    draw.entityId = 7;
    draw.mobility = EMobility::Static;
    FillPreviousTransform(draw, cache, 1, false, false);
    ASSERT_FALSE(draw.hasObjectMotion);
    const auto camera = Mat4::Translation(-0.0625f, 0, 0);
    std::vector<std::array<float, 2>> baseline, overlay;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, camera, Mat4{}, false, false, false, baseline));
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, camera, Mat4{}, true, false, false, overlay));
    // The object pass allocates one 512-byte CB per submitted draw.
    EXPECT_EQ(objectConstantBytes, 0u);
    EXPECT_EQ(overlay, baseline);
    ExpectInterior(overlay, 64, 48, -2, 0);
}

TEST_F(VelocityObjectPass, PreviousPoseVertexSrv_ContributesMotion) {
    DrawCommand draw{};
    draw.hasObjectMotion = true;
    std::vector<std::array<float, 2>> pixels;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, Mat4{}, Mat4{}, true, false, true, pixels));
    // Previous pose vertices are 0.125 NDC left of the current pose.
    ExpectInterior(pixels, 64, 48, 4, 0);
}

TEST_F(VelocityObjectPass, ScreenPercentage50_RescalesToOutput) {
    DrawCommand draw{};
    draw.worldMatrix = Mat4::Translation(0.125f, -0.0625f, 0);
    draw.hasObjectMotion = true;
    std::vector<std::array<float, 2>> full, half;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, Mat4{}, Mat4{}, true, false, false, full));
    ASSERT_NO_FATAL_FAILURE(Render(32, 24, draw, Mat4{}, Mat4{}, true, false, false, half));
    ExpectInterior(full, 64, 48, 4, 1.5f);
    ExpectInterior(half, 32, 24, 2, 0.75f);
    for (size_t component = 0; component < 2; ++component)
        EXPECT_NEAR(half[12 * 32 + 16][component] * 2,
                    full[24 * 64 + 32][component], 0.1f);
}

TEST_F(VelocityObjectPass, ParticleMotion_InRenderPixels) {
    std::vector<std::array<float, 2>> pixels;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, DrawCommand{}, Mat4{}, Mat4{},
        false, false, false, pixels, true));
    ASSERT_EQ(pixels.size(), 64u * 48u);
    for (const auto& pixel : pixels) {
        EXPECT_NEAR(pixel[0], 9.6f, 0.01f);
        EXPECT_NEAR(pixel[1], 4.8f, 0.01f);
    }
}

TEST_F(VelocityObjectPass, SkinnedBoneRotation_PreviousPoseSrv_MatchesCpuSkinning) {
    DrawCommand draw{};
    draw.hasObjectMotion = true;
    std::vector<std::array<float, 2>> pixels;
    ASSERT_NO_FATAL_FAILURE(Render(64, 48, draw, Mat4{}, Mat4{},
        true, false, true, pixels, false, true));
    const auto previousBone = Mat4::RotationZ(-2.f * 3.14159265358979323846f / 180.f);
    for (uint32_t y = 20; y < 28; ++y) {
        for (uint32_t x = 28; x < 36; ++x) {
            const Vec3 surface{(float(x) + 0.5f) * 2.f / 64.f - 1.f,
                               1.f - (float(y) + 0.5f) * 2.f / 48.f, 0.5f};
            const auto previous = ProjectPixels(previousBone, surface, 64, 48);
            EXPECT_NEAR(pixels[y * 64 + x][0], float(x) + 0.5f - previous[0], 0.25f);
            EXPECT_NEAR(pixels[y * 64 + x][1], float(y) + 0.5f - previous[1], 0.25f);
        }
    }
}
} // namespace
#endif
