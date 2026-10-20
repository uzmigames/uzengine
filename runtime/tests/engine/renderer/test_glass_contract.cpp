// F-007: exercise the shipped translucent shader, not a replacement glass shader.
#include "constant_buffers.h"
#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

using namespace uz;

TEST(GlassContract, ReusedFieldsKeepConstantBufferLayout) {
    EXPECT_EQ(sizeof(PerFrameCB), 1024u);
    EXPECT_EQ(sizeof(PerObjectCB), 256u);
    EXPECT_EQ(offsetof(PerObjectCB, displacementScale), 224u);
    EXPECT_EQ(offsetof(PerObjectCB, heightmapSrvIndex), 228u);
    EXPECT_EQ(offsetof(PerObjectCB, materialFlags), 252u);
}

#ifdef _WIN32
#include "rhi.h"
#include "rhi_backend.h"
#include "shader_compiler.h"
#include "engine_shaders.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "root_slots.h"
#include "frame/taa_jitter.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12shader.h>
// Zig/Clang does not provide __uuidof. Pass explicit IIDs to DXC instead.
#undef CROSS_PLATFORM_UUIDOF
#define CROSS_PLATFORM_UUIDOF(iface, spec) struct iface;
#include <dxcapi.h>
#undef CROSS_PLATFORM_UUIDOF

namespace {
using Pixel = std::array<float, 4>;
constexpr uint32_t kSize = 64;
constexpr uint32_t kPitch = kSize * sizeof(Pixel); // Already 256-byte aligned.
constexpr uint32_t kSpecular = 128;

struct ReleaseGlassCom {
    template <typename T> void operator()(T* object) const { object->Release(); }
};

class GlassDraw : public ::testing::Test {
protected:
    std::unique_ptr<IRHIDevice> device;
    ShaderCompiler compiler;
    CBRingAllocator ring;
    IRHICommandList* cmd = nullptr;
    RHIPipelineHandle pipeline;
    std::vector<RHIShaderHandle> shaders;
    std::vector<RHITextureHandle> textures;
    std::vector<RHIBufferHandle> buffers;
    std::vector<GPUMesh> meshes;
    std::vector<uint8_t> pixelBytecode, vertexBytecode;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend = RHIBackend::DX12;
        desc.enableValidation = true;
        device = CreateRHIDevice(desc);
        if (!device) GTEST_SKIP() << "DX12 device unavailable";
        ASSERT_TRUE(device->IsBindless()) << "Glass requires SM6.6 descriptor heaps";
        ASSERT_TRUE(compiler.Init()) << "DXC must be beside the test executable";
        ASSERT_TRUE(ring.Init(device.get(), 1, 64 * 1024));
        cmd = device->CreateCommandList(RHIQueueType::Graphics);
        ASSERT_NE(cmd, nullptr);

        RHIShaderHandle vs, ps;
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::MeshVS, true, vs, vertexBytecode));
        ASSERT_NO_FATAL_FAILURE(Compile(EngineShader::TranslucentPS, false, ps, pixelBytecode));
        RHIGraphicsPipelineDesc pso{};
        pso.vertexShader = vs;
        pso.pixelShader = ps;
        pso.rasterizer.cullMode = RHICullMode::None;
        pso.depthStencil.depthEnable = false;
        pso.renderTargetFormats[0] = RHIFormat::RGBA32Float;
        auto& blend = pso.renderTargetBlend[0];
        blend.blendEnable = true;
        blend.srcColor = blend.srcAlpha = RHIBlendFactor::One;
        blend.dstColor = blend.dstAlpha = RHIBlendFactor::InvSrcAlpha;
        pipeline = device->CreateGraphicsPipeline(pso);
        ASSERT_TRUE(pipeline.IsValid());
    }

    void TearDown() override {
        if (!device) return;
        device->WaitIdle();
        if (cmd) device->DestroyCommandList(cmd);
        if (pipeline.IsValid()) device->DestroyPipeline(pipeline);
        for (auto shader : shaders) device->DestroyShader(shader);
        for (auto& mesh : meshes) DestroyGPUMesh(device.get(), mesh);
        for (auto texture : textures) device->DestroyTexture(texture);
        for (auto buffer : buffers) device->DestroyBuffer(buffer);
        ring.Shutdown();
    }

    void Compile(EngineShader source, bool vertex, RHIShaderHandle& shader,
                 std::vector<uint8_t>& bytecode) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(source);
        desc.sourceName = EngineShaders::GetName(source);
        desc.stage = vertex ? ShaderStageCompile::Vertex : ShaderStageCompile::Pixel;
        desc.defines = {"UZ_BINDLESS=1"};
        const auto result = compiler.Compile(desc);
        ASSERT_TRUE(result.success) << result.errors;
        bytecode = result.bytecode;
        RHIShaderDesc compiled{};
        compiled.stage = vertex ? RHIShaderStage::Vertex : RHIShaderStage::Pixel;
        compiled.bytecode = bytecode.data();
        compiled.bytecodeSize = bytecode.size();
        shader = device->CreateShader(compiled);
        ASSERT_TRUE(shader.IsValid());
        shaders.push_back(shader);
    }

    void Reflect(const std::vector<uint8_t>& bytecode,
                 std::unique_ptr<ID3D12ShaderReflection, ReleaseGlassCom>& reflection) {
        const auto module = GetModuleHandleW(L"dxcompiler.dll");
        ASSERT_NE(module, nullptr);
        const auto create = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(module, "DxcCreateInstance"));
        ASSERT_NE(create, nullptr);
        // IDxcUtils from dxcapi.h; no statically linked DXC dependency.
        constexpr GUID utilsIID{0x4605c4cb, 0x2019, 0x492a,
                               {0xad, 0xa4, 0x65, 0xf2, 0x0b, 0xb7, 0xd6, 0x7f}};
        IDxcUtils* rawUtils = nullptr;
        ASSERT_TRUE(SUCCEEDED(create(CLSID_DxcUtils, utilsIID,
                                     reinterpret_cast<void**>(&rawUtils))));
        std::unique_ptr<IDxcUtils, ReleaseGlassCom> utils(rawUtils);
        const DxcBuffer buffer{bytecode.data(), bytecode.size(), 0};
        ID3D12ShaderReflection* rawReflection = nullptr;
        constexpr GUID reflectionIID{0x5a58797d, 0xa72c, 0x478d,
                                    {0x8b, 0xa2, 0xef, 0xc6, 0xb0, 0xef, 0xe8, 0x8e}};
        ASSERT_TRUE(SUCCEEDED(utils->CreateReflection(
            &buffer, reflectionIID, reinterpret_cast<void**>(&rawReflection))));
        reflection.reset(rawReflection);
    }

    static Mat4 ViewProjection(bool jitter) {
        auto projection = Mat4::Perspective(1.57079632679f, 1, 0.1f, 100);
        // The factory starts from identity and leaves _44=1. Use a genuine
        // pinhole projection (clip.w=z) for this independent Snell oracle.
        projection(3, 3) = 0;
        // Several pixels of jitter distinguish the copy's projection from an
        // unjittered projection; ordinary subpixel jitter could hide in tolerance.
        return jitter ? ApplyTaaJitter(projection, 2.25f, -1.75f, kSize, kSize) : projection;
    }

    static PerObjectCB Material(uint32_t transmission = 15, uint32_t shadingModel = 9) {
        PerObjectCB object{};
        object.model = object.normalMatrix = Mat4::Identity();
        object.baseColorTintR = 0.8f;
        object.baseColorTintG = 0.6f;
        object.baseColorTintB = 0.4f;
        object.emissiveColorR = 0.02f;
        object.emissiveColorG = 0.03f;
        object.emissiveColorB = 0.04f;
        object.emissiveIntensity = 1;
        object.opacity = 0.35f;
        object.displacementScale = 0.8f;
        object.materialFlags = 2u | (kSpecular << 8u)
                             | (shadingModel << 24u) | (transmission << 28u);
        return object;
    }

    static std::vector<Vertex> Surface(bool sphere) {
        std::vector<Vertex> vertices;
        // A front spherical cap is sufficient: unlike a full sphere it needs no
        // depth buffer to exclude back faces. It uses varying positions/normals.
        const int divisions = sphere ? 12 : 1;
        const float extent = sphere ? 0.65f : 2.f;
        for (int y = 0; y < divisions; ++y) {
            for (int x = 0; x < divisions; ++x) {
                Vertex corner[4]{};
                for (int i = 0; i < 4; ++i) {
                    const float px = -extent + 2 * extent * (x + (i & 1)) / divisions;
                    const float py = -extent + 2 * extent * (y + (i >> 1)) / divisions;
                    const float nz = sphere ? -std::sqrt(1 - px * px - py * py) : -1.f;
                    corner[i] = {{px, py, sphere ? 3 + nz : 2.f},
                                 {sphere ? px : 0.f, sphere ? py : 0.f, nz},
                                 {0.5f, 0.5f}, {1, 0, 0, 1}};
                }
                for (int i : {0, 1, 2, 2, 1, 3}) vertices.push_back(corner[i]);
            }
        }
        return vertices;
    }

    void Barrier(RHITextureHandle texture, RHIResourceState before, RHIResourceState after) {
        RHIBarrierDesc barrier{};
        barrier.texture = texture;
        barrier.stateBefore = before;
        barrier.stateAfter = after;
        cmd->ResourceBarrier(&barrier, 1);
    }

    void Render(PerObjectCB object, bool sphere, bool jitter, std::vector<Pixel>& pixels) {
        ring.BeginFrame(0); // The preceding render waits for completion before reuse.
        cmd->Begin();
        auto vertices = Surface(sphere);
        const auto inverse = object.model.Inverse();
        // Rebase vertices to keep exactly the same world surface while changing
        // pivot/scale. This isolates the slab computation from screen coverage.
        for (auto& vertex : vertices) vertex.position = inverse * vertex.position;
        Mesh cpu{};
        cpu.header.magic = kMeshMagic;
        cpu.header.version = kMeshVersion;
        cpu.header.vertexFormat = MeshVertexFormat::Standard;
        cpu.header.vertexStride = sizeof(Vertex);
        cpu.header.vertexCount = static_cast<uint32_t>(vertices.size());
        cpu.header.lodCount = 1;
        cpu.vertexData.resize(vertices.size() * sizeof(Vertex));
        std::memcpy(cpu.vertexData.data(), vertices.data(), cpu.vertexData.size());
        const auto mesh = UploadMesh(device.get(), cmd, cpu);
        ASSERT_TRUE(mesh.IsValid());
        meshes.push_back(mesh);

        RHITextureDesc tex{};
        tex.width = tex.height = kSize;
        tex.format = RHIFormat::RGBA32Float;
        tex.usage = RHITextureUsage::ShaderResource | RHITextureUsage::CopyDst;
        const auto scene = device->CreateTexture(tex);
        ASSERT_TRUE(scene.IsValid());
        textures.push_back(scene);
        tex.usage = RHITextureUsage::RenderTarget | RHITextureUsage::CopySrc;
        const auto target = device->CreateTexture(tex);
        ASSERT_TRUE(target.IsValid());
        textures.push_back(target);

        RHIBufferDesc staging{};
        staging.sizeBytes = uint64_t(kPitch) * kSize;
        staging.memory = RHIMemoryType::Upload;
        staging.usage = RHIBufferUsage::CopySrc;
        const auto upload = device->CreateBuffer(staging);
        ASSERT_TRUE(upload.IsValid());
        buffers.push_back(upload);
        auto* mapped = static_cast<Pixel*>(device->MapBuffer(upload));
        ASSERT_NE(mapped, nullptr);
        // Linear UV ramps turn the real scene texture lookup into a measurable
        // sample coordinate. Blue is a nonzero source/coverage control.
        for (uint32_t y = 0; y < kSize; ++y)
            for (uint32_t x = 0; x < kSize; ++x)
                mapped[y * kSize + x] = {(x + 0.5f) / kSize, (y + 0.5f) / kSize, 1, 1};
        device->UnmapBuffer(upload);
        staging.memory = RHIMemoryType::Readback;
        staging.usage = RHIBufferUsage::CopyDst;
        const auto readback = device->CreateBuffer(staging);
        ASSERT_TRUE(readback.IsValid());
        buffers.push_back(readback);
        Barrier(scene, RHIResourceState::Common, RHIResourceState::CopyDst);
        cmd->CopyBufferToTexture(upload, 0, kPitch, scene, 0, 0);
        Barrier(scene, RHIResourceState::CopyDst, RHIResourceState::ShaderResource);
        object.heightmapSrvIndex = device->GetTextureSRVIndex(scene);
        ASSERT_NE(object.heightmapSrvIndex, UINT32_MAX);
        PerFrameCB frame{};
        frame.viewProj = ViewProjection(jitter);
        frame.cameraPos = {0, 0, 0, 1};
        frame.lightDir = {0, 0, -1, 0};
        frame.lightColor = {0.2f, 0.3f, 0.4f, 1};
        frame.ambientColor = {0.1f, 0.15f, 0.2f, 1};
        frame.irradianceSrvIndex = frame.prefilteredSrvIndex = UINT32_MAX;
        const auto frameAlloc = ring.Allocate(sizeof(frame));
        const auto objectAlloc = ring.Allocate(sizeof(object));
        ASSERT_NE(frameAlloc.cpuPtr, nullptr);
        ASSERT_NE(objectAlloc.cpuPtr, nullptr);
        std::memcpy(frameAlloc.cpuPtr, &frame, sizeof(frame));
        std::memcpy(objectAlloc.cpuPtr, &object, sizeof(object));

        Barrier(target, RHIResourceState::Common, RHIResourceState::RenderTarget);
        RHIRenderPassDesc pass{};
        pass.colorAttachmentCount = 1;
        pass.colorAttachments[0].texture = target;
        pass.colorAttachments[0].clear = true;
        const Pixel background{0.11f, 0.17f, 0.23f, 0.29f};
        std::memcpy(pass.colorAttachments[0].clearColor, background.data(), sizeof(background));
        cmd->BeginRenderPass(pass);
        cmd->SetPipeline(pipeline);
        cmd->SetRootCBV(kSlotPerFrame, frameAlloc.gpuAddr);
        cmd->SetRootCBV(kSlotPerObject, objectAlloc.gpuAddr);
        const uint32_t push[] = {mesh.vbSrvIndex, 0, 0, 0};
        cmd->SetRootConstants(kSlotRootConstants, push, 4);
        cmd->SetViewport({0, 0, float(kSize), float(kSize), 0, 1});
        cmd->SetScissor({0, 0, int32_t(kSize), int32_t(kSize)});
        cmd->Draw(cpu.header.vertexCount);
        cmd->EndRenderPass();
        Barrier(target, RHIResourceState::RenderTarget, RHIResourceState::CopySrc);
        cmd->CopyTextureToBuffer(target, 0, readback, 0, kPitch);
        cmd->End();
        cmd->Execute();
        device->WaitIdle();
        const auto* result = static_cast<const Pixel*>(device->MapBuffer(readback));
        ASSERT_NE(result, nullptr);
        pixels.assign(result, result + kSize * kSize);
        device->UnmapBuffer(readback);
    }

    void ExpectPaneSample(const Pixel& pixel, uint32_t x, uint32_t y,
                          const Vec3& scale, bool jitter, uint32_t specular = kSpecular) {
        const auto vp = ViewProjection(jitter);
        const Vec4 ray = vp.Inverse() * Vec4{2 * (x + 0.5f) / kSize - 1,
                                            1 - 2 * (y + 0.5f) / kSize, 0.5f, 1};
        const Vec3 point{2 * ray.x / ray.z, 2 * ray.y / ray.z, 2};
        const float length = std::sqrt(point.x * point.x + point.y * point.y + 4);
        // Independent analytic Snell oracle for an air/glass pane facing -Z.
        const float f0 = (float(specular) / 255) * 0.08f;
        const float ior = (1 + std::sqrt(f0)) / (1 - std::sqrt(f0));
        const float rx = point.x / length / ior;
        const float ry = point.y / length / ior;
        const float rz = std::sqrt(1 - rx * rx - ry * ry);
        const Vec4 clip = vp * Vec4{point.x + rx * 0.8f * scale.x,
                                   point.y + ry * 0.8f * scale.y,
                                   point.z + rz * 0.8f * scale.z, 1};
        const float expectedU = clip.x / clip.w * 0.5f + 0.5f;
        const float expectedV = 0.5f - clip.y / clip.w * 0.5f;
        const float fresnel = f0 + (1 - f0) * std::pow(1 - 2 / length, 5.f);
        const float measuredU = (pixel[0] - 0.02f) / ((1 - fresnel) * 0.8f);
        const float measuredV = (pixel[1] - 0.03f) / ((1 - fresnel) * 0.6f);
        EXPECT_NEAR(measuredU * kSize, expectedU * kSize, 1.f);
        EXPECT_NEAR(measuredV * kSize, expectedV * kSize, 1.f);
        EXPECT_NEAR(pixel[2], (1 - fresnel) * 0.4f + 0.04f, 2e-4f);
        EXPECT_FLOAT_EQ(pixel[3], 1);
    }
};

TEST_F(GlassDraw, ReflectionUsesSceneColorAndThicknessWithoutGrowingBindings) {
    std::unique_ptr<ID3D12ShaderReflection, ReleaseGlassCom> reflection;
    ASSERT_NO_FATAL_FAILURE(Reflect(pixelBytecode, reflection));
    auto* object = reflection->GetConstantBufferByName("PerObjectCB");
    D3D12_SHADER_BUFFER_DESC buffer{};
    ASSERT_TRUE(SUCCEEDED(object->GetDesc(&buffer)));
    EXPECT_EQ(buffer.Size, 256u);
    D3D12_SHADER_VARIABLE_DESC variable{};
    ASSERT_TRUE(SUCCEEDED(object->GetVariableByName("sceneColorSrvIndex")->GetDesc(&variable)));
    EXPECT_EQ(variable.StartOffset, 228u);
    EXPECT_NE(variable.uFlags & D3D_SVF_USED, 0u);
    ASSERT_TRUE(SUCCEEDED(object->GetVariableByName("glassThickness")->GetDesc(&variable)));
    EXPECT_EQ(variable.StartOffset, 224u);
    EXPECT_NE(variable.uFlags & D3D_SVF_USED, 0u);
    ASSERT_TRUE(SUCCEEDED(reflection->GetConstantBufferByName("PerFrameCB")->GetDesc(&buffer)));
    EXPECT_EQ(buffer.Size, 1024u);

    D3D12_SHADER_DESC shader{};
    ASSERT_TRUE(SUCCEEDED(reflection->GetDesc(&shader)));
    uint32_t cbuffers = 0;
    for (uint32_t i = 0; i < shader.BoundResources; ++i) {
        D3D12_SHADER_INPUT_BIND_DESC binding{};
        ASSERT_TRUE(SUCCEEDED(reflection->GetResourceBindingDesc(i, &binding)));
        if (binding.Type == D3D_SIT_CBUFFER) {
            ++cbuffers;
            EXPECT_EQ(binding.Space, 0u);
            EXPECT_TRUE(binding.BindPoint == 1 || binding.BindPoint == 2);
        }
    }
    EXPECT_EQ(cbuffers, 2u); // No new root CBV or PS root constants.
    D3D12_SHADER_INPUT_BIND_DESC sampler{};
    ASSERT_TRUE(SUCCEEDED(reflection->GetResourceBindingDescByName("clampSampler", &sampler)));
    EXPECT_EQ(sampler.Type, D3D_SIT_SAMPLER);
    EXPECT_EQ(sampler.BindPoint, 2u);

    ASSERT_NO_FATAL_FAILURE(Reflect(vertexBytecode, reflection));
    ASSERT_TRUE(SUCCEEDED(reflection->GetConstantBufferByName("push")->GetDesc(&buffer)));
    EXPECT_EQ(buffer.Size, 16u); // Existing mesh payload; shared root block is 24 DWORDs.
    EXPECT_LE(buffer.Size, 24u * sizeof(uint32_t));
}

TEST_F(GlassDraw, PaneSamplesSceneAtSnellProjectedExitWithinOnePixel) {
    for (bool jitter : {false, true}) {
        SCOPED_TRACE(jitter);
        std::vector<Pixel> pixels;
        ASSERT_NO_FATAL_FAILURE(Render(Material(), false, jitter, pixels));
        for (const auto& xy : {std::array<uint32_t, 2>{45, 19}, {20, 43}, {32, 32}})
            ExpectPaneSample(pixels[xy[1] * kSize + xy[0]], xy[0], xy[1], {1, 1, 1}, jitter);
    }
}

TEST_F(GlassDraw, NonuniformScaleUsesEachModelAxisForSlabExit) {
    constexpr uint32_t x = 45, y = 19;
    std::array<Pixel, 4> samples{};
    const Vec3 scales[] = {{1, 1, 1}, {3, 1, 1}, {1, 3, 1}, {1, 1, 3}};
    for (size_t i = 0; i < samples.size(); ++i) {
        SCOPED_TRACE(i);
        auto object = Material();
        object.model = Mat4::Scaling(scales[i]) * Mat4::RotationZ(0.4f);
        std::vector<Pixel> pixels;
        ASSERT_NO_FATAL_FAILURE(Render(object, false, true, pixels));
        samples[i] = pixels[y * kSize + x];
        ExpectPaneSample(samples[i], x, y, scales[i], true);
    }
    EXPECT_GT(std::abs(samples[1][0] - samples[0][0]), 0.02f);
    EXPECT_NEAR(samples[1][1], samples[0][1], 2e-4f);
    EXPECT_GT(std::abs(samples[2][1] - samples[0][1]), 0.02f);
    EXPECT_NEAR(samples[2][0], samples[0][0], 2e-4f);
    EXPECT_GT(std::abs(samples[3][0] - samples[0][0]), 0.02f);
    EXPECT_GT(std::abs(samples[3][1] - samples[0][1]), 0.02f);
}

TEST_F(GlassDraw, PackedSpecularBitsChangeIORAndProjectedSceneSample) {
    constexpr uint32_t x = 45, y = 19;
    std::array<Pixel, 2> samples{};
    const uint32_t specular[] = {0, 255};
    for (size_t i = 0; i < samples.size(); ++i) {
        auto object = Material();
        object.materialFlags = (object.materialFlags & ~(255u << 8u)) | (specular[i] << 8u);
        std::vector<Pixel> pixels;
        ASSERT_NO_FATAL_FAILURE(Render(object, false, false, pixels));
        samples[i] = pixels[y * kSize + x];
        ExpectPaneSample(samples[i], x, y, {1, 1, 1}, false, specular[i]);
    }
    // Zero specular means IOR=1: no refraction shift. The other endpoint must
    // actually alter the rendered sample, not merely pass a CPU packing check.
    EXPECT_GT(std::abs(samples[1][0] - samples[0][0]), 0.01f);
    EXPECT_GT(std::abs(samples[1][2] - samples[0][2]), 0.02f); // Fresnel, independent of UV.
    // This covers the packed PerObjectCB -> shader boundary. Asset ior and
    // thicknessScale -> DrawCommand transport remains scene_sync integration
    // coverage; this fixture deliberately does not restate its packing formulas.
}

TEST_F(GlassDraw, PaneAndSphericalCapAreIndependentOfMeshPivot) {
    for (bool sphere : {false, true}) {
        SCOPED_TRACE(sphere);
        auto object = Material();
        std::vector<Pixel> centered, rebased;
        ASSERT_NO_FATAL_FAILURE(Render(object, sphere, false, centered));
        object.model = Mat4::Translation(7, -11, 5);
        ASSERT_NO_FATAL_FAILURE(Render(object, sphere, false, rebased));
        // Central pixels lie inside both surfaces, not in their clear background.
        for (uint32_t y = 29; y < 35; ++y) {
            for (uint32_t x = 29; x < 35; ++x) {
                const auto index = y * kSize + x;
                ASSERT_FLOAT_EQ(centered[index][3], 1);
                EXPECT_GT(centered[index][2], 0.35f);
                for (size_t c = 0; c < 4; ++c)
                    EXPECT_NEAR(rebased[index][c], centered[index][c], 2e-4f);
            }
        }
    }
}

TEST_F(GlassDraw, ZeroTransmissionIsIdenticalToStandardTranslucentBranch) {
    std::vector<Pixel> glass, standard;
    ASSERT_NO_FATAL_FAILURE(Render(Material(0), false, false, glass));
    ASSERT_NO_FATAL_FAILURE(Render(Material(0, 0), false, false, standard));
    ASSERT_EQ(glass.size(), standard.size());
    // Includes nonzero direct/ambient light, emission, opacity and background:
    // zero transmission must preserve the existing premultiplied composite.
    for (size_t i = 0; i < glass.size(); ++i)
        for (size_t c = 0; c < 4; ++c) EXPECT_FLOAT_EQ(glass[i][c], standard[i][c]);
    EXPECT_NEAR(standard[32 * kSize + 32][3], 0.35f + 0.29f * 0.65f, 1e-6f);
}

TEST_F(GlassDraw, LegacyGlassDoesNotSampleTheSceneBehind) {
    auto object = Material();
    object.displacementScale = -1.0f;
    std::vector<Pixel> legacy, thin;
    ASSERT_NO_FATAL_FAILURE(Render(object, false, false, legacy));
    ASSERT_NO_FATAL_FAILURE(Render(Material(), false, false, thin));
    // This fixture has no environment. Legacy glass can only return emission,
    // whereas the opt-in path must consume the nonzero scene-color texture.
    for (uint32_t y = 29; y < 35; ++y)
        for (uint32_t x = 29; x < 35; ++x) {
            const auto i = y * kSize + x;
            EXPECT_NEAR(legacy[i][0], object.emissiveColorR, 1e-6f);
            EXPECT_NEAR(legacy[i][1], object.emissiveColorG, 1e-6f);
            EXPECT_NEAR(legacy[i][2], object.emissiveColorB, 1e-6f);
            EXPECT_FLOAT_EQ(legacy[i][3], 1);
            EXPECT_GT(thin[i][2] - legacy[i][2], 0.3f);
        }
}

TEST_F(GlassDraw, PartialTransmissionLerpsPremultipliedColorAndAlpha) {
    std::vector<Pixel> standard, glass, mixed;
    ASSERT_NO_FATAL_FAILURE(Render(Material(0, 0), false, false, standard));
    ASSERT_NO_FATAL_FAILURE(Render(Material(), false, false, glass));
    ASSERT_NO_FATAL_FAILURE(Render(Material(7), false, false, mixed));
    for (uint32_t y = 20; y < 44; ++y)
        for (uint32_t x = 20; x < 44; ++x)
            for (size_t c = 0; c < 4; ++c) {
                const auto i = y * kSize + x;
                EXPECT_NEAR(mixed[i][c], standard[i][c] * (8.f / 15)
                                           + glass[i][c] * (7.f / 15), 2e-5f);
            }
}
} // namespace
#endif
