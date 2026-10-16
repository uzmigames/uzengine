// F-018 stage 1: characterize the dormant path before implementing instancing.
#include "instance_batcher.h"
#include "scene_renderer_frame.h"
#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>

using namespace uz;

namespace {
DrawCommand InstanceCommand(uint32_t mesh = 0, uint64_t key = 0) {
    DrawCommand command{};
    command.meshId = mesh;
    command.sortKey = key;
    command.worldMatrix = Mat4::Identity();
    command.normalMatrix = Mat4::Identity();
    return command;
}

class InstanceBatcher : public ::testing::Test {
protected:
    DrawList list;
    std::vector<InstanceBatch> batches;
    std::vector<InstanceData> instances;

    void Build() {
        list.Sort();
        BuildInstanceBatches(list, batches, instances);
    }
};
}

TEST_F(InstanceBatcher, EmptyList_NoBatches) {
    batches.resize(1);
    instances.resize(1);
    Build();
    EXPECT_TRUE(batches.empty());
    EXPECT_TRUE(instances.empty());
}

TEST_F(InstanceBatcher, SameMeshSamePayload_OneBatch_CountN_ContiguousFirstInstance) {
    for (uint32_t i = 0; i < 4; ++i) {
        auto command = InstanceCommand(7, i);
        command.worldMatrix(3, 0) = static_cast<float>(i);
        list.Add(command);
    }
    Build();
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].meshId, 7u);
    EXPECT_EQ(batches[0].firstInstance, 0u);
    EXPECT_EQ(batches[0].instanceCount, 4u);
    ASSERT_EQ(instances.size(), 4u);
    for (uint32_t i = 0; i < 4; ++i)
        EXPECT_EQ(instances[i].world(3, 0), static_cast<float>(i));
}

TEST_F(InstanceBatcher, DifferentMesh_SplitsBatch_KeepsMeshId) {
    list.Add(InstanceCommand(3, 0));
    list.Add(InstanceCommand(9, 1));
    list.Add(InstanceCommand(9, 2));
    Build();
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[0].meshId, 3u);
    EXPECT_EQ(batches[0].firstInstance, 0u);
    EXPECT_EQ(batches[0].instanceCount, 1u);
    EXPECT_EQ(batches[1].meshId, 9u);
    EXPECT_EQ(batches[1].firstInstance, 1u);
    EXPECT_EQ(batches[1].instanceCount, 2u);
}

TEST_F(InstanceBatcher, DifferentTexture_SplitsBatch) {
    auto command = InstanceCommand();
    list.Add(command);
    command.sortKey = 1;
    command.emissiveSrvIndex = 21;
    list.Add(command);
    Build();
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[1].emissiveSrvIndex, 21u);
}

// Negative controls: these are existing defects, not the stage-2 contract.
// Replace their expectations with remainder/payload correctness when stage 2 lands.
TEST_F(InstanceBatcher, Characterization_MaskedAndOpaqueCurrentlyMerge) {
    auto command = InstanceCommand();
    list.Add(command);
    command.sortKey = 1;
    command.materialFlags = 1; // Masked.
    list.Add(command);
    Build();
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].instanceCount, 2u);
}

TEST_F(InstanceBatcher, Characterization_OverLimitCurrentlyTruncates) {
    for (uint32_t i = 0; i < kMaxInstances + 3; ++i)
        list.Add(InstanceCommand(0, i));
    Build();
    ASSERT_EQ(batches.size(), 1u);
    EXPECT_EQ(batches[0].instanceCount, kMaxInstances);
    EXPECT_EQ(instances.size(), kMaxInstances);
    EXPECT_EQ(list.Count(), kMaxInstances + 3u); // Per-draw fallback loses nothing.
}

TEST_F(InstanceBatcher, InstanceData_Is128Bytes_WorldThenNormal) {
    auto command = InstanceCommand();
    command.worldMatrix(3, 0) = 17.f;
    command.normalMatrix(0, 0) = 3.f;
    list.Add(command);
    Build();
    ASSERT_EQ(instances.size(), 1u);
    EXPECT_EQ(sizeof(InstanceData), 128u);
    EXPECT_EQ(offsetof(InstanceData, world), 0u);
    EXPECT_EQ(offsetof(InstanceData, normalMatrix), 64u);
    EXPECT_EQ(std::memcmp(&instances[0].world, &command.worldMatrix, sizeof(Mat4)), 0);
    EXPECT_EQ(std::memcmp(&instances[0].normalMatrix, &command.normalMatrix, sizeof(Mat4)), 0);
}

TEST(InstancingCharacterization, DrawingAndGpuCullingAreOffByDefault) {
    const FrameContext frame{};
    EXPECT_FALSE(frame.useInstancing);
    EXPECT_FALSE(frame.gpuCullingActive);
    EXPECT_TRUE(frame.instanceBatches.empty());
    EXPECT_TRUE(frame.instanceDataVec.empty());
    EXPECT_EQ(frame.instanceBufferSrvIndex, UINT32_MAX);
    EXPECT_FALSE(frame.visibleIndexBuffer.IsValid());
    EXPECT_FALSE(frame.indirectArgsBuffer.IsValid());
}

#ifdef _WIN32
#include "shader_compiler.h"
#include "engine_shaders.h"
#include <memory>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d12shader.h>
#undef CROSS_PLATFORM_UUIDOF
#define CROSS_PLATFORM_UUIDOF(iface, spec) struct iface;
#include <dxcapi.h>
#undef CROSS_PLATFORM_UUIDOF

namespace {
struct ReleaseInstanceCom {
    template <typename T> void operator()(T* object) const { object->Release(); }
};

class InstancingShaderCharacterization : public ::testing::Test {
protected:
    ShaderCompiler compiler;

    void SetUp() override {
        ASSERT_TRUE(compiler.Init()) << "DXC is required, but no GPU device is needed";
    }

    void CheckUnimplemented(EngineShader shader) {
        ShaderCompileDesc desc{};
        desc.source = EngineShaders::GetSource(shader);
        desc.sourceName = EngineShaders::GetName(shader);
        desc.defines = {"UZ_BINDLESS=1"};
        const auto perDraw = compiler.Compile(desc);
        ASSERT_TRUE(perDraw.success) << perDraw.errors;
        desc.defines.push_back("UZ_INSTANCED=1");
        const auto instanced = compiler.Compile(desc);
        ASSERT_TRUE(instanced.success) << instanced.errors;
        // The macro currently has no effect on either shipped VS body.
        EXPECT_EQ(instanced.bytecode, perDraw.bytecode);

        const auto module = GetModuleHandleW(L"dxcompiler.dll");
        ASSERT_NE(module, nullptr);
        const auto create = reinterpret_cast<DxcCreateInstanceProc>(
            GetProcAddress(module, "DxcCreateInstance"));
        ASSERT_NE(create, nullptr);
        constexpr GUID utilsIID{0x4605c4cb, 0x2019, 0x492a,
                               {0xad, 0xa4, 0x65, 0xf2, 0x0b, 0xb7, 0xd6, 0x7f}};
        IDxcUtils* rawUtils = nullptr;
        ASSERT_TRUE(SUCCEEDED(create(CLSID_DxcUtils, utilsIID,
                                     reinterpret_cast<void**>(&rawUtils))));
        std::unique_ptr<IDxcUtils, ReleaseInstanceCom> utils(rawUtils);
        const DxcBuffer buffer{instanced.bytecode.data(), instanced.bytecode.size(), 0};
        constexpr GUID reflectionIID{0x5a58797d, 0xa72c, 0x478d,
                                    {0x8b, 0xa2, 0xef, 0xc6, 0xb0, 0xef, 0xe8, 0x8e}};
        ID3D12ShaderReflection* rawReflection = nullptr;
        ASSERT_TRUE(SUCCEEDED(utils->CreateReflection(
            &buffer, reflectionIID, reinterpret_cast<void**>(&rawReflection))));
        std::unique_ptr<ID3D12ShaderReflection, ReleaseInstanceCom> reflection(rawReflection);
        D3D12_SHADER_DESC reflected{};
        ASSERT_TRUE(SUCCEEDED(reflection->GetDesc(&reflected)));
        for (UINT i = 0; i < reflected.InputParameters; ++i) {
            D3D12_SIGNATURE_PARAMETER_DESC input{};
            ASSERT_TRUE(SUCCEEDED(reflection->GetInputParameterDesc(i, &input)));
            EXPECT_NE(input.SystemValueType, D3D_NAME_INSTANCE_ID);
        }
        for (UINT i = 0; i < reflected.BoundResources; ++i) {
            D3D12_SHADER_INPUT_BIND_DESC resource{};
            ASSERT_TRUE(SUCCEEDED(reflection->GetResourceBindingDesc(i, &resource)));
            EXPECT_STRNE(resource.Name, "instances");
        }
    }
};
}

TEST_F(InstancingShaderCharacterization, MeshVS_MacroIsNoOp_NoInstanceIdOrInstanceSrv) {
    CheckUnimplemented(EngineShader::MeshVS);
}

TEST_F(InstancingShaderCharacterization, ShadowDepthVS_MacroIsNoOp_NoInstanceIdOrInstanceSrv) {
    CheckUnimplemented(EngineShader::ShadowDepthVS);
}
#endif
