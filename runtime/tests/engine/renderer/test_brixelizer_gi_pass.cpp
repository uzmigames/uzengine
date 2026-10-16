/// Public API safety and real DX12 integration tests for the Brixelizer pass.
/// GPU tests forward every observed call to the loaded FidelityFX DLL.

#include "engine/renderer/passes/gi/brixelizer_gi_pass.h"
#include "engine/renderer/core/gbuffer/draw_list.h"
#include "engine/renderer/mesh/gpu_mesh.h"

#include <gtest/gtest.h>

#include <utility>

#if defined(_WIN32) && UZ_BRIXELIZER_ENABLED
#include "engine/renderer/passes/gi/brixelizer_loader.h"
#include "engine/core/cvar.h"
#include "engine/rhi/rhi.h"
#include "engine/rhi/rhi_backend.h"
#include "engine/rhi/dx12/dx12_device.h"
#include "engine/rhi/dx12/dx12_resources.h"

#include <cstring>
#endif

namespace uz::test {
namespace {

void ExpectUnavailableOutputs(const BrixelizerGIPass& pass) {
    EXPECT_FALSE(pass.IsInitialized());
    EXPECT_EQ(pass.GetDiffuseGISrv(), UINT32_MAX);
    EXPECT_EQ(pass.GetSpecularGISrv(), UINT32_MAX);
    EXPECT_EQ(pass.GetDiffuseGIUav(), UINT32_MAX);
    EXPECT_EQ(pass.GetSpecularGIUav(), UINT32_MAX);
    EXPECT_FALSE(pass.GetDiffuseGITexture().IsValid());
    EXPECT_FALSE(pass.GetSpecularGITexture().IsValid());
}

} // namespace

TEST(BrixelizerGIPass, DefaultOutputsAreUnavailable) {
    BrixelizerGIPass pass;
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, NullDeviceInitIsRejected) {
    BrixelizerGIPass pass;
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ZeroExtentInitIsRejectedBeforeDeviceAccess) {
    BrixelizerGIPass pass;
    auto* dummy = reinterpret_cast<IRHIDevice*>(uintptr_t{1});
    EXPECT_FALSE(pass.Init(dummy, 0, 64));
    EXPECT_FALSE(pass.Init(dummy, 64, 0));
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ShutdownIsIdempotentBeforeInit) {
    BrixelizerGIPass pass;
    pass.Shutdown();
    pass.Shutdown();
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, RejectedInitCanBeShutdownRepeatedly) {
    BrixelizerGIPass pass;
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    pass.Shutdown();
    EXPECT_FALSE(pass.Init(nullptr, 64, 64));
    pass.Shutdown();
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, RegistrationBeforeInitDoesNotExposeOutputs) {
    BrixelizerGIPass pass;
    DrawCommand draw{};
    draw.meshId = 0;
    GPUMesh mesh{};
    mesh.vertexBuffer = {1, 1};
    mesh.indexBuffer = {2, 1};
    mesh.vertexCount = 3;
    mesh.indexCount = 3;
    mesh.vertexStride = 48;
    mesh.boundsMin = Vec3(-1.0f, -2.0f, -3.0f);
    mesh.boundsMax = Vec3(1.0f, 2.0f, 3.0f);
    const std::vector<DrawCommand> draws{draw};
    pass.RegisterGeometry(draws, &mesh, 1);
    pass.RegisterGeometry(draws, nullptr, 1);
    pass.RegisterGeometry(draws, &mesh, 0);
    pass.RegisterGeometry({}, &mesh, 1);
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ExecuteBeforeInitDoesNotDereferenceInputs) {
    BrixelizerGIPass pass;
    pass.Execute(nullptr, nullptr, nullptr, nullptr, nullptr,
                 0.0f, 0.0f, 0.0f, 0, {}, {}, {}, {}, {});
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, ResizeBeforeInitDoesNotExposeOutputs) {
    BrixelizerGIPass pass;
    pass.OnResize(0, 64);
    pass.OnResize(64, 0);
    pass.OnResize(64, 64);
    pass.OnResize(1920, 1080);
    ExpectUnavailableOutputs(pass);
}

TEST(BrixelizerGIPass, UninitializedMoveConstructionAndAssignmentAreSafe) {
    BrixelizerGIPass source;
    BrixelizerGIPass moved(std::move(source));
    ExpectUnavailableOutputs(source);
    ExpectUnavailableOutputs(moved);
    BrixelizerGIPass assigned;
    assigned = std::move(moved);
    ExpectUnavailableOutputs(moved);
    ExpectUnavailableOutputs(assigned);
    source.Shutdown();
    moved.Shutdown();
    assigned.Shutdown();
}

#if defined(_WIN32) && UZ_BRIXELIZER_ENABLED
namespace {

// Installed only after loading the real DLL. GoogleTest runs these tests
// serially; restore the process-wide table even after a fatal assertion.
class BrixCallObserver {
public:
    BrixCallObserver() {
        active = this;
        pfn_uz_brix_GetInterfaceDX12 = GetInterface;
        pfn_uz_brix_BrixelizerRegisterBuffers = RegisterBuffers;
        pfn_uz_brix_BrixelizerCreateInstances = CreateInstances;
        pfn_uz_brix_BrixelizerDeleteInstances = DeleteInstances;
        pfn_uz_brix_BrixelizerBakeUpdate = BakeUpdate;
        pfn_uz_brix_BrixelizerUpdate = Update;
        pfn_uz_brix_BrixelizerContextDestroy = Destroy;
        pfn_uz_brix_BrixelizerGIContextDestroy = DestroyGI;
    }

    ~BrixCallObserver() {
        pfn_uz_brix_GetInterfaceDX12 = realGetInterface;
        pfn_uz_brix_BrixelizerRegisterBuffers = realRegister;
        pfn_uz_brix_BrixelizerCreateInstances = realCreate;
        pfn_uz_brix_BrixelizerDeleteInstances = realDelete;
        pfn_uz_brix_BrixelizerBakeUpdate = realBake;
        pfn_uz_brix_BrixelizerUpdate = realUpdate;
        pfn_uz_brix_BrixelizerContextDestroy = realDestroy;
        pfn_uz_brix_BrixelizerGIContextDestroy = realDestroyGI;
        active = nullptr;
    }

    BrixCallObserver(const BrixCallObserver&) = delete;
    BrixCallObserver& operator=(const BrixCallObserver&) = delete;

    struct BufferSlot {
        void* resource;
        uint32_t slot;
    };
    std::vector<BufferSlot> buffers;
    std::vector<FfxBrixelizerInstanceDescription> instances;
    std::vector<FfxBrixelizerInstanceID> instanceIds;
    std::vector<std::pair<void*, size_t>> backendScratch;
    FfxBrixelizerResources bakeResources{};
    uint32_t registerCalls = 0;
    uint32_t createCalls = 0;
    uint32_t deleteCalls = 0;
    uint32_t bakeCalls = 0;
    uint32_t updateCalls = 0;
    uint32_t destroyCalls = 0;
    uint32_t destroyGICalls = 0;
    size_t requiredScratch = 0;

private:
    inline static BrixCallObserver* active = nullptr;
    PFN_uzGetInterfaceDX12 realGetInterface = pfn_uz_brix_GetInterfaceDX12;
    PFN_uzBrixelizerRegisterBuffers realRegister = pfn_uz_brix_BrixelizerRegisterBuffers;
    PFN_uzBrixelizerCreateInstances realCreate = pfn_uz_brix_BrixelizerCreateInstances;
    PFN_uzBrixelizerDeleteInstances realDelete = pfn_uz_brix_BrixelizerDeleteInstances;
    PFN_uzBrixelizerBakeUpdate realBake = pfn_uz_brix_BrixelizerBakeUpdate;
    PFN_uzBrixelizerUpdate realUpdate = pfn_uz_brix_BrixelizerUpdate;
    PFN_uzBrixelizerContextDestroy realDestroy = pfn_uz_brix_BrixelizerContextDestroy;
    PFN_uzBrixelizerGIContextDestroy realDestroyGI = pfn_uz_brix_BrixelizerGIContextDestroy;

    static FfxErrorCode GetInterface(FfxInterface* backend, FfxDevice device,
                                    void* scratch, size_t scratchSize, size_t maxContexts) {
        auto& self = *active;
        self.backendScratch.push_back({scratch, scratchSize});
        const auto result = self.realGetInterface(backend, device, scratch, scratchSize, maxContexts);
        EXPECT_EQ(result, FFX_OK);
        return result;
    }

    static FfxErrorCode RegisterBuffers(FfxBrixelizerContext* ctx,
                                       const FfxBrixelizerBufferDescription* descs,
                                       uint32_t count) {
        auto& self = *active;
        ++self.registerCalls;
        const auto result = self.realRegister(ctx, descs, count);
        EXPECT_EQ(result, FFX_OK);
        if (result == FFX_OK) {
            for (uint32_t i = 0; i < count; ++i) {
                self.buffers.push_back({descs[i].buffer.resource, *descs[i].outIndex});
            }
        }
        return result;
    }

    static FfxErrorCode CreateInstances(FfxBrixelizerContext* ctx,
                                       const FfxBrixelizerInstanceDescription* descs,
                                       uint32_t count) {
        auto& self = *active;
        ++self.createCalls;
        self.instances.assign(descs, descs + count);
        // outInstanceID belongs to RegisterGeometry's temporary output vector.
        for (auto& instance : self.instances) instance.outInstanceID = nullptr;
        const auto result = self.realCreate(ctx, descs, count);
        EXPECT_EQ(result, FFX_OK);
        if (result == FFX_OK) {
            for (uint32_t i = 0; i < count; ++i) {
                self.instanceIds.push_back(*descs[i].outInstanceID);
            }
        }
        return result;
    }

    static FfxErrorCode DeleteInstances(FfxBrixelizerContext* ctx,
                                       const FfxBrixelizerInstanceID* ids, uint32_t count) {
        ++active->deleteCalls;
        const auto result = active->realDelete(ctx, ids, count);
        EXPECT_EQ(result, FFX_OK);
        return result;
    }

    static FfxErrorCode BakeUpdate(FfxBrixelizerContext* ctx,
                                  const FfxBrixelizerUpdateDescription* desc,
                                  FfxBrixelizerBakedUpdateDescription* baked) {
        auto& self = *active;
        ++self.bakeCalls;
        self.bakeResources = desc->resources;
        const auto result = self.realBake(ctx, desc, baked);
        EXPECT_EQ(result, FFX_OK);
        if (result == FFX_OK && desc->outScratchBufferSize) {
            self.requiredScratch = *desc->outScratchBufferSize;
        }
        return result;
    }

    static FfxErrorCode Update(FfxBrixelizerContext* ctx,
                              FfxBrixelizerBakedUpdateDescription* baked,
                              FfxResource scratch, FfxCommandList cmd) {
        auto& self = *active;
        ++self.updateCalls;
        EXPECT_GE(static_cast<size_t>(scratch.description.width), self.requiredScratch);
        EXPECT_EQ(scratch.description.stride, sizeof(uint32_t));
        const auto result = self.realUpdate(ctx, baked, scratch, cmd);
        EXPECT_EQ(result, FFX_OK);
        return result;
    }

    static FfxErrorCode Destroy(FfxBrixelizerContext* ctx) {
        ++active->destroyCalls;
        const auto result = active->realDestroy(ctx);
        EXPECT_EQ(result, FFX_OK);
        return result;
    }

    static FfxErrorCode DestroyGI(FfxBrixelizerGIContext* ctx) {
        ++active->destroyGICalls;
        const auto result = active->realDestroyGI(ctx);
        EXPECT_EQ(result, FFX_OK);
        return result;
    }
};

class BrixelizerGIGpuTest : public ::testing::Test {
protected:
    std::unique_ptr<IRHIDevice> device;
    std::unique_ptr<BrixCallObserver> calls;
    BrixelizerGIPass pass;
    std::vector<RHIBufferHandle> buffers;
    std::vector<std::pair<CVar*, float>> savedCVars;

    void SetUp() override {
        RHIDeviceDesc desc{};
        desc.backend = RHIBackend::DX12;
        desc.enableValidation = true;
        device = CreateRHIDevice(desc);
        if (!device) GTEST_SKIP() << "A real DX12 GPU is required";
        ASSERT_TRUE(BrixLoaderInit()) << "The real uz_brixelizer.dll must be next to the test executable";
        calls = std::make_unique<BrixCallObserver>();
        for (const auto& setting : std::vector<std::pair<const char*, float>>{
                 {"r.BrixelizerGI", 1.0f}, {"r.BrixelizerGI.RealInit", 1.0f},
                 {"r.BrixelizerGI.AllocSdf", 1.0f}, {"r.BrixelizerGI.BakeUpdate", 1.0f},
                 {"r.BrixelizerGI.Dispatch", 1.0f}, {"r.BrixelizerGI.GiDispatch", 0.0f},
                 {"r.BrixelizerGI.Cascades", 2.0f}}) {
            auto* value = CVarRegistry::Get().Find(setting.first);
            ASSERT_NE(value, nullptr) << setting.first;
            savedCVars.push_back({value, value->value});
            CVarRegistry::Get().Set(setting.first, setting.second);
        }
        ASSERT_TRUE(pass.Init(device.get(), 64, 64));
        ASSERT_TRUE(pass.IsInitialized());
    }

    void TearDown() override {
        if (device) device->WaitIdle();
        pass.Shutdown();
        calls.reset();
        if (device) {
            for (const auto buffer : buffers) device->DestroyBuffer(buffer);
            device.reset();
        }
        for (const auto& saved : savedCVars) saved.first->value = saved.second;
    }

    // Upload-heap buffers are real GPU-readable resources in GENERIC_READ,
    // avoiding staging lifetime and resource-state assumptions in this test.
    void CreateTriangle(GPUMesh& mesh, uint32_t stride, RHIIndexFormat format) {
        mesh.vertexCount = 3;
        mesh.indexCount = 3;
        mesh.vertexStride = stride;
        mesh.indexFormat = format;
        mesh.boundsMin = Vec3(-1.0f, -2.0f, -3.0f);
        mesh.boundsMax = Vec3(1.0f, 2.0f, 3.0f);
        RHIBufferDesc desc{};
        desc.sizeBytes = stride * 3;
        desc.memory = RHIMemoryType::Upload;
        desc.usage = RHIBufferUsage::VertexBuffer;
        mesh.vertexBuffer = device->CreateBuffer(desc);
        ASSERT_TRUE(mesh.vertexBuffer.IsValid());
        buffers.push_back(mesh.vertexBuffer);
        auto* vertices = static_cast<uint8_t*>(device->MapBuffer(mesh.vertexBuffer));
        ASSERT_NE(vertices, nullptr);
        std::memset(vertices, 0, static_cast<size_t>(desc.sizeBytes));
        const float positions[3][3] = {{-1, -2, -3}, {1, 2, -3}, {1, -2, 3}};
        for (uint32_t i = 0; i < 3; ++i) {
            std::memcpy(vertices + i * stride, positions[i], sizeof(positions[i]));
        }
        device->UnmapBuffer(mesh.vertexBuffer);

        desc.sizeBytes = 12; // Also pad uint16 data to a whole raw-buffer word.
        desc.usage = RHIBufferUsage::IndexBuffer;
        mesh.indexBuffer = device->CreateBuffer(desc);
        ASSERT_TRUE(mesh.indexBuffer.IsValid());
        buffers.push_back(mesh.indexBuffer);
        void* indices = device->MapBuffer(mesh.indexBuffer);
        ASSERT_NE(indices, nullptr);
        const uint32_t indices32[3] = {0, 1, 2};
        const uint16_t indices16[6] = {0, 1, 2, 0, 0, 0};
        std::memcpy(indices, format == RHIIndexFormat::Uint32
                                 ? static_cast<const void*>(indices32)
                                 : static_cast<const void*>(indices16), 12);
        device->UnmapBuffer(mesh.indexBuffer);
    }
};

} // namespace

TEST_F(BrixelizerGIGpuTest, RegistersActualWorldSpaceInstanceDescriptions) {
    GPUMesh meshes[2];
    CreateTriangle(meshes[0], 32, RHIIndexFormat::Uint32);
    ASSERT_FALSE(HasFatalFailure());
    CreateTriangle(meshes[1], 48, RHIIndexFormat::Uint16);
    ASSERT_FALSE(HasFatalFailure());
    // x' = -3y + 10, y' = -2x + 20, z' = 4z + 30:
    // rotation, reflection, nonuniform scale, and translation.
    const float world[16] = {0, -2, 0, 0, -3, 0, 0, 0, 0, 0, 4, 0, 10, 20, 30, 1};
    DrawCommand transformed{};
    transformed.worldMatrix = Mat4(world);
    DrawCommand identity{};
    identity.meshId = 1;
    DrawCommand invalid{};
    invalid.meshId = 2;
    pass.RegisterGeometry({invalid, transformed, identity}, meshes, 2);
    ASSERT_EQ(calls->registerCalls, 1u);
    ASSERT_EQ(calls->createCalls, 1u);
    ASSERT_EQ(calls->buffers.size(), 4u);
    ASSERT_EQ(calls->instances.size(), 2u);
    ASSERT_EQ(calls->instanceIds.size(), 2u);
    EXPECT_NE(calls->instanceIds[0], calls->instanceIds[1]);

    const float expectedTransform[12] = {0, -3, 0, 10, -2, 0, 0, 20, 0, 0, 4, 30};
    const float expectedIdentity[12] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0};
    for (size_t i = 0; i < 12; ++i) {
        EXPECT_FLOAT_EQ(calls->instances[0].transform[i], expectedTransform[i]);
        EXPECT_FLOAT_EQ(calls->instances[1].transform[i], expectedIdentity[i]);
    }
    const float expectedMin[3] = {4, 18, 18};
    const float expectedMax[3] = {16, 22, 42};
    for (size_t axis = 0; axis < 3; ++axis) {
        EXPECT_FLOAT_EQ(calls->instances[0].aabb.min[axis], expectedMin[axis]);
        EXPECT_FLOAT_EQ(calls->instances[0].aabb.max[axis], expectedMax[axis]);
        EXPECT_FLOAT_EQ(calls->instances[1].aabb.min[axis], meshes[1].boundsMin.data()[axis]);
        EXPECT_FLOAT_EQ(calls->instances[1].aabb.max[axis], meshes[1].boundsMax.data()[axis]);
    }
    auto& pool = static_cast<DX12Device*>(device.get())->GetBufferPool();
    for (size_t i = 0; i < 2; ++i) {
        const auto& instance = calls->instances[i];
        const auto& mesh = meshes[i];
        EXPECT_EQ(calls->buffers[i * 2].resource,
                  pool.Get(mesh.vertexBuffer.index, mesh.vertexBuffer.generation)->resource);
        EXPECT_EQ(calls->buffers[i * 2 + 1].resource,
                  pool.Get(mesh.indexBuffer.index, mesh.indexBuffer.generation)->resource);
        EXPECT_EQ(instance.vertexBuffer, calls->buffers[i * 2].slot);
        EXPECT_EQ(instance.indexBuffer, calls->buffers[i * 2 + 1].slot);
        // SDK 1.1's free list descends: this must exercise separate slot mapping.
        EXPECT_NE(calls->buffers[i * 2 + 1].slot, calls->buffers[i * 2].slot + 1);
        EXPECT_EQ(instance.vertexStride, mesh.vertexStride);
        EXPECT_EQ(instance.vertexCount, 3u);
        EXPECT_EQ(instance.triangleCount, 1u);
        EXPECT_EQ(instance.vertexFormat, FFX_SURFACE_FORMAT_R32G32B32_FLOAT);
        EXPECT_EQ(instance.indexFormat, i == 0 ? FFX_INDEX_TYPE_UINT32 : FFX_INDEX_TYPE_UINT16);
        EXPECT_EQ(instance.vertexBufferOffset, 0u);
        EXPECT_EQ(instance.indexBufferOffset, 0u);
        EXPECT_EQ(instance.flags, FFX_BRIXELIZER_INSTANCE_FLAG_NONE);
    }
    pass.RegisterGeometry({invalid, transformed, identity}, meshes, 2);
    EXPECT_EQ(calls->registerCalls, 1u);
    EXPECT_EQ(calls->createCalls, 1u);
    transformed.worldMatrix.m._41 += 5.0f;
    pass.RegisterGeometry({invalid, transformed, identity}, meshes, 2);
    EXPECT_EQ(calls->registerCalls, 1u);
    EXPECT_EQ(calls->createCalls, 2u);
    EXPECT_EQ(calls->deleteCalls, 1u);
    ASSERT_EQ(calls->instances.size(), 2u);
    EXPECT_FLOAT_EQ(calls->instances[0].transform[3], 15.0f);
    pass.RegisterGeometry({}, meshes, 2);
    EXPECT_EQ(calls->deleteCalls, 2u);
    pass.Shutdown();
    EXPECT_EQ(calls->destroyCalls, 1u);
    EXPECT_EQ(calls->destroyGICalls, 1u);
    ExpectUnavailableOutputs(pass);
}

TEST_F(BrixelizerGIGpuTest, CreatesRegistersBakesAndDestroysRealContexts) {
    // Both backends need distinct, nonempty scratch allocations. The real
    // bake and context destruction below exercise their retained state.
    ASSERT_EQ(calls->backendScratch.size(), 2u);
    EXPECT_NE(calls->backendScratch[0].first, nullptr);
    EXPECT_NE(calls->backendScratch[1].first, nullptr);
    EXPECT_NE(calls->backendScratch[0].first, calls->backendScratch[1].first);
    EXPECT_GT(calls->backendScratch[0].second, 0u);
    EXPECT_GT(calls->backendScratch[1].second, 0u);
    ASSERT_NE(pass.GetDiffuseGISrv(), UINT32_MAX);
    ASSERT_NE(pass.GetSpecularGISrv(), UINT32_MAX);
    ASSERT_NE(pass.GetDiffuseGIUav(), UINT32_MAX);
    ASSERT_NE(pass.GetSpecularGIUav(), UINT32_MAX);
    ASSERT_TRUE(pass.GetDiffuseGITexture().IsValid());
    ASSERT_TRUE(pass.GetSpecularGITexture().IsValid());
    const auto diffuse = pass.GetDiffuseGITexture();
    const auto specular = pass.GetSpecularGITexture();
    const auto diffuseDesc = device->GetTextureDesc(pass.GetDiffuseGITexture());
    EXPECT_EQ(diffuseDesc.width, 64u);
    EXPECT_EQ(diffuseDesc.height, 64u);
    EXPECT_EQ(diffuseDesc.format, RHIFormat::RGBA16Float);
    GPUMesh mesh;
    CreateTriangle(mesh, 48, RHIIndexFormat::Uint32);
    ASSERT_FALSE(HasFatalFailure());
    const std::vector<DrawCommand> draws{DrawCommand{}};
    pass.RegisterGeometry(draws, &mesh, 1);
    ASSERT_EQ(calls->instanceIds.size(), 1u);
    pass.RegisterGeometry(draws, &mesh, 1);
    EXPECT_EQ(calls->registerCalls, 1u);
    EXPECT_EQ(calls->createCalls, 1u);

    auto* cmd = device->CreateCommandList(RHIQueueType::Graphics);
    ASSERT_NE(cmd, nullptr);
    const Mat4 identity;
    cmd->Begin();
    // GI dispatch is disabled by the fixture, not replaced by a mock.
    pass.Execute(cmd, identity.data(), identity.data(), identity.data(), identity.data(),
                 0, 0, 0, 0, {}, {}, {}, {}, {});
    cmd->End();
    cmd->Execute();
    device->WaitIdle();
    device->DestroyCommandList(cmd);
    EXPECT_EQ(calls->bakeCalls, 1u);
    EXPECT_EQ(calls->updateCalls, 1u);
    EXPECT_GT(calls->requiredScratch, 0u);
    const auto& resources = calls->bakeResources;
    ASSERT_NE(resources.sdfAtlas.resource, nullptr);
    EXPECT_EQ(resources.sdfAtlas.description.type, FFX_RESOURCE_TYPE_TEXTURE3D);
    EXPECT_EQ(resources.sdfAtlas.description.width, 512u);
    EXPECT_EQ(resources.sdfAtlas.description.height, 512u);
    EXPECT_EQ(resources.sdfAtlas.description.depth, 512u);
    EXPECT_EQ(resources.sdfAtlas.description.format, FFX_SURFACE_FORMAT_R8_UNORM);
    ASSERT_NE(resources.brickAABBs.resource, nullptr);
    EXPECT_EQ(resources.brickAABBs.description.width, (1u << 18) * sizeof(uint32_t));
    // Two logical cascades require static [0,1], dynamic [2,3], and
    // merged [4,5] raw cascades, not just two pairs of external buffers.
    for (size_t i = 0; i < 24; ++i) {
        const auto& cascade = resources.cascadeResources[i];
        if (i < 6) {
            ASSERT_NE(cascade.aabbTree.resource, nullptr) << i;
            ASSERT_NE(cascade.brickMap.resource, nullptr) << i;
            EXPECT_EQ(cascade.aabbTree.description.type, FFX_RESOURCE_TYPE_BUFFER);
            EXPECT_EQ(cascade.brickMap.description.type, FFX_RESOURCE_TYPE_BUFFER);
            EXPECT_EQ(cascade.aabbTree.description.width, 17944u);
            EXPECT_EQ(cascade.brickMap.description.width, 1048576u);
            EXPECT_EQ(cascade.aabbTree.description.stride, sizeof(uint32_t));
            EXPECT_EQ(cascade.brickMap.description.stride, sizeof(uint32_t));
            EXPECT_NE(cascade.aabbTree.resource, cascade.brickMap.resource);
            for (size_t previous = 0; previous < i; ++previous) {
                const auto& other = resources.cascadeResources[previous];
                EXPECT_NE(cascade.aabbTree.resource, other.aabbTree.resource);
                EXPECT_NE(cascade.aabbTree.resource, other.brickMap.resource);
                EXPECT_NE(cascade.brickMap.resource, other.aabbTree.resource);
                EXPECT_NE(cascade.brickMap.resource, other.brickMap.resource);
            }
        } else {
            EXPECT_EQ(cascade.aabbTree.resource, nullptr) << i;
            EXPECT_EQ(cascade.brickMap.resource, nullptr) << i;
        }
    }
    EXPECT_EQ(static_cast<DX12Device*>(device.get())->GetDevice()->GetDeviceRemovedReason(), S_OK);
    pass.Shutdown();
    EXPECT_EQ(calls->destroyCalls, 1u);
    EXPECT_EQ(calls->destroyGICalls, 1u);
    ExpectUnavailableOutputs(pass);
    auto& textures = static_cast<DX12Device*>(device.get())->GetTexturePool();
    // Free retains the slot generation until reuse; verify the owned GPU
    // resource, allocation, and descriptors are actually released.
    for (const auto texture : {diffuse, specular}) {
        const auto* slot = textures.Get(texture.index, texture.generation);
        ASSERT_NE(slot, nullptr);
        EXPECT_EQ(slot->resource, nullptr);
        EXPECT_EQ(slot->allocation, nullptr);
        EXPECT_EQ(slot->srvIndex, UINT32_MAX);
        EXPECT_EQ(slot->uavIndex, UINT32_MAX);
    }
    pass.Shutdown();
    EXPECT_EQ(calls->destroyCalls, 1u);
    EXPECT_EQ(calls->destroyGICalls, 1u);
}

TEST_F(BrixelizerGIGpuTest, InitializedMoveAndReinitPreserveOwnership) {
    const auto originalDiffuse = pass.GetDiffuseGITexture();
    BrixelizerGIPass moved(std::move(pass));
    ExpectUnavailableOutputs(pass);
    EXPECT_TRUE(moved.IsInitialized());
    EXPECT_EQ(moved.GetDiffuseGITexture().index, originalDiffuse.index);

    BrixelizerGIPass destination;
    ASSERT_TRUE(destination.Init(device.get(), 64, 64));
    destination = std::move(moved);
    ExpectUnavailableOutputs(moved);
    EXPECT_EQ(calls->destroyCalls, 1u);
    EXPECT_EQ(calls->destroyGICalls, 1u);
    EXPECT_EQ(destination.GetDiffuseGITexture().index, originalDiffuse.index);

    ASSERT_TRUE(destination.Init(device.get(), 64, 64));
    EXPECT_EQ(calls->destroyCalls, 2u);
    EXPECT_EQ(calls->destroyGICalls, 2u);
    auto& textures = static_cast<DX12Device*>(device.get())->GetTexturePool();
    EXPECT_EQ(textures.Get(originalDiffuse.index, originalDiffuse.generation), nullptr);
    destination.Shutdown();
    EXPECT_EQ(calls->destroyCalls, 3u);
    EXPECT_EQ(calls->destroyGICalls, 3u);
    ExpectUnavailableOutputs(destination);
}
#else
TEST(BrixelizerGIGpuTest, RealImplementationRequired) {
    GTEST_SKIP() << "Build the tests and runtime with UZ_BRIXELIZER_ENABLED=1 and the FidelityFX SDK";
}
#endif

} // namespace uz::test
