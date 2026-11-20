/// Unit tests for the froxel volumetric fog (phase2_froxel-volumetric-fog).
///
/// CPU-only — no GPU device. Covers the C++ side of the feature:
/// - `VolumetricFogComponent`       — defaults, the documented default medium
/// - `volumetric_fog` serializer    — every JSON key, defaults for missing keys, round trip,
///                                    absent component, the scene loader's registry path
/// - `VolumetricFogCB`              — 512-byte size, every field offset the three HLSL copies
///                                    declare (scripts/rendering/test_cbuffer_layouts.py checks
///                                    the shader side), defaults, memcpy round trip
/// - froxel grid constants          — 160x90x64, the dispatch group counts, the volume sizes
/// - exponential depth slicing      — a CPU mirror of `SliceCoordToDepth` / `SliceToDepth` /
///                                    `DepthToSliceCoord` / `SliceThickness` (inject + scatter)
///                                    and the composite's `DepthToFroxelUVW`: edges, monotonic,
///                                    constant ratio, telescoping thickness, inverse, saturation
/// - `VolumetricFogPass`            — Init / Shutdown / failure cleanup, the lazy temporal
///                                    history, the uploaded CB's resolved flags and indices, all
///                                    on a recording mock device
/// - height fog handoff             — `AddHeightFogFromECS` starts the analytic fog at the grid's
///                                    far edge (no double application), CPU side
///
/// The pass lambdas that record GPU work need real pipelines; they are exercised by the capture
/// scripts (scripts/rendering/test_froxel_fog.py) rather than here.

#include "volumetric_fog_pass.h"
#include "volumetric_fog_component.h"
#include "height_fog_component.h"
#include "fog_renderer.h"
#include "component_serializer.h"
#include "constant_buffers.h"
#include "camera.h"
#include "world.h"
#include "scene_loader.h"
#include "pipeline_library.h"
#include "render_graph_builder.h"
#include "render_graph_resources.h"
#include "render_graph_states.h"
#include "engine/rhi/rhi.h"

#include <json.hpp>
#include <entt/entt.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace uz;

namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

entt::entity ToEntt(Entity e) { return static_cast<entt::entity>(static_cast<uint32_t>(e.id)); }

std::vector<uint8_t> ToBytes(const std::string& s) { return {s.begin(), s.end()}; }

/// A fully-populated component, every field away from its default.
VolumetricFogComponent Authored() {
    VolumetricFogComponent c{};
    c.density              = 0.002f;
    c.globalDensity        = 0.5f;
    c.albedo               = {0.9f, 0.8f, 0.7f};
    c.phaseG               = 0.2f;
    c.heightFalloff        = 0.3f;
    c.heightOffset         = 150.0f;
    c.extinctionScale      = 1.5f;
    c.emissive             = {0.01f, 0.02f, 0.03f};
    c.distance             = 12000.0f;
    c.startDistance        = 50.0f;
    c.nearFadeInDistance   = 600.0f;
    c.localLightScattering = 12.0f;
    c.ambientIntensity     = 0.25f;
    c.enabled              = false;
    return c;
}

void ExpectSame(const VolumetricFogComponent& a, const VolumetricFogComponent& b) {
    EXPECT_FLOAT_EQ(a.density, b.density);
    EXPECT_FLOAT_EQ(a.globalDensity, b.globalDensity);
    EXPECT_FLOAT_EQ(a.albedo.x, b.albedo.x);
    EXPECT_FLOAT_EQ(a.albedo.y, b.albedo.y);
    EXPECT_FLOAT_EQ(a.albedo.z, b.albedo.z);
    EXPECT_FLOAT_EQ(a.phaseG, b.phaseG);
    EXPECT_FLOAT_EQ(a.heightFalloff, b.heightFalloff);
    EXPECT_FLOAT_EQ(a.heightOffset, b.heightOffset);
    EXPECT_FLOAT_EQ(a.extinctionScale, b.extinctionScale);
    EXPECT_FLOAT_EQ(a.emissive.x, b.emissive.x);
    EXPECT_FLOAT_EQ(a.emissive.y, b.emissive.y);
    EXPECT_FLOAT_EQ(a.emissive.z, b.emissive.z);
    EXPECT_FLOAT_EQ(a.distance, b.distance);
    EXPECT_FLOAT_EQ(a.startDistance, b.startDistance);
    EXPECT_FLOAT_EQ(a.nearFadeInDistance, b.nearFadeInDistance);
    EXPECT_FLOAT_EQ(a.localLightScattering, b.localLightScattering);
    EXPECT_FLOAT_EQ(a.ambientIntensity, b.ambientIntensity);
    EXPECT_EQ(a.enabled, b.enabled);
}

constexpr const char* kSerializerKey = "volumetric_fog";

// ─────────────────────────────────────────────────────────────────────────────
// CPU mirror of the shaders' exponential depth slicing
// ─────────────────────────────────────────────────────────────────────────────
// volumetric_fog_inject_cs.hlsl : SliceCoordToDepth(t)
// volumetric_fog_scatter_cs.hlsl: StartZ(), SliceToDepth(slice, n), DepthToSliceCoord(d), SliceThickness(slice, n)
// volumetric_fog_composite_ps.hlsl: SliceToDepth(slice, n), DepthToFroxelUVW(uv, d).z
// The formulas are copied verbatim; a change on either side must be mirrored here.

struct SliceParams {
    float nearZ = 5.0f;              ///< Sponza-like camera near plane (cm)
    float farZ = 6400.0f;            ///< VolumetricFogComponent::distance default (cm)
    float fogStartDistance = 0.0f;   ///< VolumetricFogComponent::startDistance default
    uint32_t numSlices = kFroxelGridZ;
};

float StartZ(const SliceParams& p) { return std::max(p.nearZ, p.fogStartDistance); }

float SliceCoordToDepth(const SliceParams& p, float t) {
    const float startZ = StartZ(p);
    return startZ * std::pow(p.farZ / std::max(startZ, 0.01f), t);
}

float SliceToDepth(const SliceParams& p, uint32_t slice) {
    return SliceCoordToDepth(p, (static_cast<float>(slice) + 0.5f) / static_cast<float>(p.numSlices));
}

float DepthToSliceCoord(const SliceParams& p, float viewDepth) {
    const float startZ = StartZ(p);
    return std::log(std::max(viewDepth, 1e-3f) / startZ) / std::log(p.farZ / startZ);
}

float SliceThickness(const SliceParams& p, uint32_t slice) {
    const float d0 = (slice > 0) ? SliceToDepth(p, slice - 1) : StartZ(p);
    const float d1 = SliceToDepth(p, slice);
    return d1 - d0;
}

/// The composite's volume W coordinate for a view depth (saturated).
float CompositeSliceW(const SliceParams& p, float viewDepthCm) {
    const float startZ = StartZ(p);
    const float t = std::log(std::max(viewDepthCm, startZ) / startZ) / std::log(p.farZ / startZ);
    return std::clamp(t, 0.0f, 1.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Recording mock device: descriptor indices per texture, every created desc, failure injection
// ─────────────────────────────────────────────────────────────────────────────

struct CreatedTexture {
    RHITextureHandle handle;
    RHITextureDesc desc;
    std::string name;
};

class FogMockDevice final : public IRHIDevice {
public:
    std::vector<CreatedTexture> created;
    std::vector<RHITextureHandle> destroyed;
    /// 1-based index of the CreateTexture call that returns an invalid handle (0 = never).
    uint32_t failCreateCall = 0;
    uint32_t createCalls = 0;

    static constexpr uint32_t kSrvBase = 100;
    static constexpr uint32_t kUavBase = 200;

    const CreatedTexture* Find(const char* name) const {
        for (const auto& t : created) if (t.name == name) return &t;
        return nullptr;
    }
    bool Destroyed(RHITextureHandle h) const {
        for (const auto& d : destroyed) if (d.index == h.index && d.generation == h.generation) return true;
        return false;
    }

    RHITextureHandle CreateTexture(const RHITextureDesc& desc) override {
        ++createCalls;
        if (createCalls == failCreateCall) return RHITextureHandle::Invalid();
        const uint32_t idx = static_cast<uint32_t>(created.size()) + 1;
        RHITextureHandle h{idx, 1u};
        created.push_back({h, desc, desc.debugName ? desc.debugName : ""});
        return h;
    }
    void DestroyTexture(RHITextureHandle h) override { destroyed.push_back(h); }
    uint32_t GetTextureSRVIndex(RHITextureHandle h) const override { return h.IsValid() ? kSrvBase + h.index : UINT32_MAX; }
    uint32_t GetTextureUAVIndex(RHITextureHandle h) const override { return h.IsValid() ? kUavBase + h.index : UINT32_MAX; }
    RHITextureDesc GetTextureDesc(RHITextureHandle h) const override {
        for (const auto& t : created) if (t.handle.index == h.index) return t.desc;
        return {};
    }

    // Buffers: one persistently mapped scratch region (the CB ring maps its upload buffer once).
    RHIBufferHandle CreateBuffer(const RHIBufferDesc&) override { return {++bufferIndex_, 1u}; }
    void DestroyBuffer(RHIBufferHandle) override {}
    void* MapBuffer(RHIBufferHandle) override { return scratch_; }
    void UnmapBuffer(RHIBufferHandle) override {}
    uint32_t GetBufferSRVIndex(RHIBufferHandle) const override { return UINT32_MAX; }
    uint32_t GetBufferUAVIndex(RHIBufferHandle) const override { return UINT32_MAX; }
    uint64_t GetBufferGPUAddress(RHIBufferHandle) const override { return 0x10000; }

    RHIShaderHandle CreateShader(const RHIShaderDesc&) override { return {0, 0}; }
    void DestroyShader(RHIShaderHandle) override {}
    RHIPipelineHandle CreateGraphicsPipeline(const RHIGraphicsPipelineDesc&) override { return {0, 0}; }
    RHIPipelineHandle CreateComputePipeline(const RHIComputePipelineDesc&) override { return {0, 0}; }
    void DestroyPipeline(RHIPipelineHandle) override {}
    std::vector<uint8_t> GetPipelineCachedBlob(RHIPipelineHandle) override { return {}; }
    RHISamplerHandle CreateSampler(const RHISamplerDesc&) override { return {0, 0}; }
    void DestroySampler(RHISamplerHandle) override {}
    uint32_t GetSamplerIndex(RHISamplerHandle) const override { return UINT32_MAX; }
    IRHISwapChain* CreateSwapChain(const RHISwapChainDesc&) override { return nullptr; }
    void DestroySwapChain(IRHISwapChain*) override {}
    IRHIFence* CreateFence() override { return nullptr; }
    void DestroyFence(IRHIFence*) override {}
    IRHICommandList* CreateCommandList(RHIQueueType) override { return nullptr; }
    void DestroyCommandList(IRHICommandList*) override {}
    void WaitIdle() override {}
    const char* GetAdapterName() const override { return "FogMockDevice"; }
    const char* GetFeatureLevel() const override { return "Mock"; }
    RHIMemoryBudget GetMemoryBudget() const override { return {}; }
    ShaderModel GetShaderModel() const override { return static_cast<ShaderModel>(0); }
    RHIBackend GetBackend() const override { return static_cast<RHIBackend>(0); }

private:
    uint32_t bufferIndex_ = 0;
    static constexpr size_t kScratchSize = 64 * 1024;
    alignas(256) uint8_t scratch_[kScratchSize] = {};
};

/// Mock device + render graph + CB ring + uninitialised pipeline library (every getter returns an
/// invalid handle, so RegisterPasses resolves its state and uploads its CB without adding passes).
/// The pass imports its volumes as tracked textures, which needs the state registry on the builder.
class VolumetricFogPassFixture : public ::testing::Test {
protected:
    void SetUp() override {
        pool = std::make_unique<rg::RgResourcePool>(&device);
        states = std::make_unique<rg::RgStateRegistry>(&device);
        ASSERT_TRUE(ring.Init(&device, 1, 16 * 1024));
        ring.BeginFrame(0);
        ASSERT_TRUE(pass.Init(&device));
    }
    void TearDown() override {
        pass.Shutdown(&device);
        ring.Shutdown();
    }

    VolumetricFogFrameInputs Inputs(bool temporal, bool reusable = true) {
        VolumetricFogFrameInputs in{};
        in.bindless = true;
        in.vpWidth = 1920.f;
        in.vpHeight = 1080.f;
        in.temporalWanted = temporal;
        in.historyReusable = reusable;
        in.lightingAlloc.gpuAddr = 0x2000;
        return in;
    }

    /// Registers the passes and returns the CB the pass uploaded. The ring is a bump allocator:
    /// a probe allocation taken just before fixes where the pass's 512-byte block lands.
    VolumetricFogCB Register(rg::RgBuilder& rgb, const VolumetricFogFrameInputs& in, const VolumetricFogCB& params) {
        const CBAllocation probe = ring.Allocate(sizeof(VolumetricFogCB));
        pass.RegisterPasses(rgb, in, params, ring, &pipelines);
        VolumetricFogCB uploaded{};
        std::memcpy(&uploaded, static_cast<const uint8_t*>(probe.cpuPtr) + sizeof(VolumetricFogCB), sizeof(VolumetricFogCB));
        return uploaded;
    }

    FogMockDevice device;
    std::unique_ptr<rg::RgResourcePool> pool;
    std::unique_ptr<rg::RgStateRegistry> states;
    CBRingAllocator ring;
    PipelineLibrary pipelines;
    VolumetricFogPass pass;
};

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogComponent — defaults
// ─────────────────────────────────────────────────────────────────────────────

TEST(VolumetricFogComponent, Defaults) {
    const VolumetricFogComponent c{};
    EXPECT_FLOAT_EQ(c.density, 0.0005f);
    EXPECT_FLOAT_EQ(c.globalDensity, 1.0f);
    EXPECT_FLOAT_EQ(c.albedo.x, 1.0f);
    EXPECT_FLOAT_EQ(c.albedo.y, 1.0f);
    EXPECT_FLOAT_EQ(c.albedo.z, 1.0f);
    EXPECT_FLOAT_EQ(c.phaseG, 0.5f);
    EXPECT_FLOAT_EQ(c.heightFalloff, 0.1f);
    EXPECT_FLOAT_EQ(c.heightOffset, 0.0f);
    EXPECT_FLOAT_EQ(c.extinctionScale, 1.0f);
    EXPECT_FLOAT_EQ(c.emissive.x, 0.0f);
    EXPECT_FLOAT_EQ(c.emissive.y, 0.0f);
    EXPECT_FLOAT_EQ(c.emissive.z, 0.0f);
    EXPECT_FLOAT_EQ(c.distance, 6400.0f);
    EXPECT_FLOAT_EQ(c.startDistance, 0.0f);
    EXPECT_FLOAT_EQ(c.nearFadeInDistance, 300.0f);
    EXPECT_FLOAT_EQ(c.localLightScattering, 1.0f);
    EXPECT_FLOAT_EQ(c.ambientIntensity, 1.0f);
    EXPECT_TRUE(c.enabled);
}

/// The header documents the default medium as a 20 m mean free path with transmittance 0.04
/// across the default 64 m grid: density is per cm, distance in cm.
TEST(VolumetricFogComponent, DefaultMediumIsTheDocumentedFoggyAtrium) {
    const VolumetricFogComponent c{};
    const float sigma = c.density * c.globalDensity * c.extinctionScale;   // per cm
    EXPECT_NEAR(1.0f / sigma, 2000.0f, 1e-3f);                              // 20 m mean free path
    EXPECT_NEAR(std::exp(-sigma * c.distance), 0.0408f, 5e-4f);             // exp(-3.2)
}

// ─────────────────────────────────────────────────────────────────────────────
// volumetric_fog serializer
// ─────────────────────────────────────────────────────────────────────────────

TEST(VolumetricFogSerializer, RegisteredUnderTheVolumetricFogKey) {
    EXPECT_TRUE(ComponentSerializerRegistry::Get().Has(kSerializerKey));
}

TEST(VolumetricFogSerializer, DeserializeReadsEveryKey) {
    World world;
    const Entity e = world.CreateEntity();
    const nlohmann::json j = {
        {"density", 0.002}, {"global_density", 0.5}, {"albedo", {0.9, 0.8, 0.7}}, {"phase_g", 0.2},
        {"height_falloff", 0.3}, {"height_offset", 150.0}, {"extinction_scale", 1.5},
        {"emissive", {0.01, 0.02, 0.03}}, {"distance", 12000.0}, {"start_distance", 50.0},
        {"near_fade_in_distance", 600.0}, {"ambient_intensity", 0.25}, {"local_light_scattering", 12.0},
        {"enabled", false},
    };
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, e, &j));
    const auto* c = world.GetRegistry().try_get<VolumetricFogComponent>(ToEntt(e));
    ASSERT_NE(c, nullptr);
    ExpectSame(*c, Authored());
}

TEST(VolumetricFogSerializer, MissingKeysKeepTheDefaults) {
    World world;
    const Entity empty = world.CreateEntity();
    const nlohmann::json none = nlohmann::json::object();
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, empty, &none));
    const auto* c0 = world.GetRegistry().try_get<VolumetricFogComponent>(ToEntt(empty));
    ASSERT_NE(c0, nullptr);
    ExpectSame(*c0, VolumetricFogComponent{});

    // One key set: everything else stays default.
    const Entity partial = world.CreateEntity();
    const nlohmann::json one = {{"density", 0.01}};
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, partial, &one));
    const auto* c1 = world.GetRegistry().try_get<VolumetricFogComponent>(ToEntt(partial));
    ASSERT_NE(c1, nullptr);
    VolumetricFogComponent expected{};
    expected.density = 0.01f;
    ExpectSame(*c1, expected);
}

TEST(VolumetricFogSerializer, SerializeWritesEveryKey) {
    World world;
    const Entity e = world.CreateEntity();
    world.AddComponent<VolumetricFogComponent>(e) = Authored();
    nlohmann::json j;
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Serialize(kSerializerKey, world, e, &j));
    for (const char* key : {"density", "global_density", "albedo", "phase_g", "height_falloff", "height_offset",
                            "extinction_scale", "emissive", "distance", "start_distance", "near_fade_in_distance",
                            "ambient_intensity", "local_light_scattering", "enabled"}) {
        EXPECT_TRUE(j.contains(key)) << key;
    }
    EXPECT_EQ(j.size(), 14u);
    EXPECT_FLOAT_EQ(j["density"].get<float>(), 0.002f);
    EXPECT_FLOAT_EQ(j["distance"].get<float>(), 12000.0f);
    ASSERT_TRUE(j["albedo"].is_array());
    ASSERT_EQ(j["albedo"].size(), 3u);
    EXPECT_FLOAT_EQ(j["albedo"][2].get<float>(), 0.7f);
    EXPECT_FALSE(j["enabled"].get<bool>());
}

TEST(VolumetricFogSerializer, RoundTripPreservesEveryField) {
    World world;
    const Entity src = world.CreateEntity();
    world.AddComponent<VolumetricFogComponent>(src) = Authored();
    nlohmann::json j;
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Serialize(kSerializerKey, world, src, &j));

    const Entity dst = world.CreateEntity();
    ASSERT_TRUE(ComponentSerializerRegistry::Get().Deserialize(kSerializerKey, world, dst, &j));
    const auto* c = world.GetRegistry().try_get<VolumetricFogComponent>(ToEntt(dst));
    ASSERT_NE(c, nullptr);
    ExpectSame(*c, Authored());
}

TEST(VolumetricFogSerializer, SerializeWithoutTheComponentReturnsFalse) {
    World world;
    const Entity e = world.CreateEntity();
    nlohmann::json j;
    EXPECT_FALSE(ComponentSerializerRegistry::Get().Serialize(kSerializerKey, world, e, &j));
    EXPECT_TRUE(j.is_null());
}

/// The scene loader reaches the component through the registry (no hardcoded parser).
TEST(VolumetricFogSerializer, SceneLoaderCreatesTheComponentFromItsKey) {
    World world;
    const std::string scene = R"({"entities":[{"name":"Fog","components":{"volumetric_fog":{
        "density":0.0007,"phase_g":0.3,"distance":8000,"local_light_scattering":4}}}]})";
    const auto result = LoadSceneEx(world, ToBytes(scene), "volumetric_fog_test.uzscene");
    ASSERT_TRUE(result.ok()) << result.message;
    EXPECT_EQ(result.entityCount, 1u);
    auto view = world.GetRegistry().view<VolumetricFogComponent>();
    ASSERT_EQ(view.size(), 1u);
    const auto& c = view.get<VolumetricFogComponent>(*view.begin());
    EXPECT_FLOAT_EQ(c.density, 0.0007f);
    EXPECT_FLOAT_EQ(c.phaseG, 0.3f);
    EXPECT_FLOAT_EQ(c.distance, 8000.0f);
    EXPECT_FLOAT_EQ(c.localLightScattering, 4.0f);
    EXPECT_FLOAT_EQ(c.heightFalloff, 0.1f);   // untouched default
    EXPECT_TRUE(c.enabled);
}

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogCB — size, field offsets (the HLSL copies), defaults, memcpy
// ─────────────────────────────────────────────────────────────────────────────

TEST(VolumetricFogCB, SizeIs512Bytes) {
    EXPECT_EQ(sizeof(VolumetricFogCB), 512u);
    EXPECT_EQ(sizeof(VolumetricFogCB) % 256u, 0u);
}

TEST(VolumetricFogCB, FieldOffsetsMatchTheShaderBlock) {
    EXPECT_EQ(offsetof(VolumetricFogCB, invViewProj),          0u);
    EXPECT_EQ(offsetof(VolumetricFogCB, cameraPosX),          64u);
    EXPECT_EQ(offsetof(VolumetricFogCB, cameraPosZ),          72u);
    EXPECT_EQ(offsetof(VolumetricFogCB, nearZ),               76u);
    EXPECT_EQ(offsetof(VolumetricFogCB, farZ),                80u);
    EXPECT_EQ(offsetof(VolumetricFogCB, globalDensity),       84u);
    EXPECT_EQ(offsetof(VolumetricFogCB, heightFalloff),       88u);
    EXPECT_EQ(offsetof(VolumetricFogCB, heightOffset),        92u);
    EXPECT_EQ(offsetof(VolumetricFogCB, albedoR),             96u);
    EXPECT_EQ(offsetof(VolumetricFogCB, phaseG),             108u);
    EXPECT_EQ(offsetof(VolumetricFogCB, sunDirX),            112u);
    EXPECT_EQ(offsetof(VolumetricFogCB, sunIntensity),       124u);
    EXPECT_EQ(offsetof(VolumetricFogCB, sunColorR),          128u);
    EXPECT_EQ(offsetof(VolumetricFogCB, temporalAlpha),      140u);
    EXPECT_EQ(offsetof(VolumetricFogCB, inscatteringUavIdx), 144u);
    EXPECT_EQ(offsetof(VolumetricFogCB, inscatteringSrvIdx), 148u);
    EXPECT_EQ(offsetof(VolumetricFogCB, integratedUavIdx),   152u);
    EXPECT_EQ(offsetof(VolumetricFogCB, integratedSrvIdx),   156u);
    EXPECT_EQ(offsetof(VolumetricFogCB, depthSrvIdx),        160u);
    EXPECT_EQ(offsetof(VolumetricFogCB, frameIndex),         164u);
    EXPECT_EQ(offsetof(VolumetricFogCB, gridX),              168u);
    EXPECT_EQ(offsetof(VolumetricFogCB, gridY),              172u);
    EXPECT_EQ(offsetof(VolumetricFogCB, gridZ),              176u);
    EXPECT_EQ(offsetof(VolumetricFogCB, extinctionScale),    180u);
    EXPECT_EQ(offsetof(VolumetricFogCB, emissiveR),          184u);
    EXPECT_EQ(offsetof(VolumetricFogCB, emissiveB),          192u);
    EXPECT_EQ(offsetof(VolumetricFogCB, fogStartDistance),   196u);
    EXPECT_EQ(offsetof(VolumetricFogCB, nearFadeInDistance), 200u);
    EXPECT_EQ(offsetof(VolumetricFogCB, cameraFarZ),         204u);
    EXPECT_EQ(offsetof(VolumetricFogCB, sunShadowEnabled),   208u);
    EXPECT_EQ(offsetof(VolumetricFogCB, debugMode),          212u);
    // phase2_froxel-volumetric-fog additions
    EXPECT_EQ(offsetof(VolumetricFogCB, historySrvIdx),      216u);
    EXPECT_EQ(offsetof(VolumetricFogCB, historyUavIdx),      220u);
    EXPECT_EQ(offsetof(VolumetricFogCB, temporalEnabled),    224u);
    EXPECT_EQ(offsetof(VolumetricFogCB, localLightsEnabled), 228u);
    EXPECT_EQ(offsetof(VolumetricFogCB, localShadowEnabled), 232u);
    EXPECT_EQ(offsetof(VolumetricFogCB, jitterEnabled),      236u);
    EXPECT_EQ(offsetof(VolumetricFogCB, debugStage),         240u);
    EXPECT_EQ(offsetof(VolumetricFogCB, temporalClamp),      244u);
    EXPECT_EQ(offsetof(VolumetricFogCB, ambientR),           248u);
    EXPECT_EQ(offsetof(VolumetricFogCB, ambientG),           252u);
    EXPECT_EQ(offsetof(VolumetricFogCB, ambientB),           256u);   // scalars: an HLSL float3 at 248 would move to 256
    EXPECT_EQ(offsetof(VolumetricFogCB, localLightScale),    260u);
    EXPECT_EQ(offsetof(VolumetricFogCB, prevViewProj),       272u);
    EXPECT_EQ(offsetof(VolumetricFogCB, prevViewProj) % 16u, 0u);    // a float4x4 row must start a 16-byte row
    EXPECT_EQ(offsetof(VolumetricFogCB, prevViewProj) + 64u, 336u);
}

TEST(VolumetricFogCB, DefaultsMatchTheCVarDefaults) {
    const VolumetricFogCB cb{};
    EXPECT_EQ(cb.historySrvIdx, UINT32_MAX);
    EXPECT_EQ(cb.historyUavIdx, UINT32_MAX);
    EXPECT_EQ(cb.temporalEnabled, 0u);
    EXPECT_EQ(cb.localLightsEnabled, 0u);
    EXPECT_EQ(cb.localShadowEnabled, 0u);
    EXPECT_EQ(cb.jitterEnabled, 0u);
    EXPECT_EQ(cb.debugStage, 0u);
    EXPECT_EQ(cb.debugMode, 0u);
    EXPECT_EQ(cb.sunShadowEnabled, 1u);
    EXPECT_FLOAT_EQ(cb.temporalClamp, 1.5f);      // r.VolumetricFog.TemporalClamp
    EXPECT_FLOAT_EQ(cb.localLightScale, 1.0f);
    EXPECT_FLOAT_EQ(cb.ambientR, 0.0f);
    EXPECT_FLOAT_EQ(cb.cameraFarZ, 0.0f);
}

TEST(VolumetricFogCB, MemcpyRoundTrip) {
    VolumetricFogCB src{};
    src.farZ = 6400.0f;
    src.globalDensity = 0.0005f;
    src.phaseG = 0.5f;
    src.temporalAlpha = 0.05f;
    src.historySrvIdx = 7u;
    src.historyUavIdx = 8u;
    src.temporalEnabled = 1u;
    src.localLightsEnabled = 1u;
    src.localShadowEnabled = 1u;
    src.jitterEnabled = 1u;
    src.debugStage = 4u;
    src.ambientR = 0.1f; src.ambientG = 0.2f; src.ambientB = 0.3f;
    src.localLightScale = 12.0f;
    uint8_t bytes[sizeof(VolumetricFogCB)];
    std::memcpy(bytes, &src, sizeof(src));
    VolumetricFogCB dst{};
    std::memcpy(&dst, bytes, sizeof(dst));
    EXPECT_EQ(std::memcmp(&src, &dst, sizeof(src)), 0);
    EXPECT_FLOAT_EQ(dst.farZ, 6400.0f);
    EXPECT_EQ(dst.historySrvIdx, 7u);
    EXPECT_EQ(dst.debugStage, 4u);
    EXPECT_FLOAT_EQ(dst.localLightScale, 12.0f);
}

// ─────────────────────────────────────────────────────────────────────────────
// Froxel grid constants
// ─────────────────────────────────────────────────────────────────────────────

TEST(FroxelGrid, FrostbiteResolution) {
    EXPECT_EQ(kFroxelGridX, 160u);
    EXPECT_EQ(kFroxelGridY, 90u);
    EXPECT_EQ(kFroxelGridZ, 64u);
    EXPECT_EQ(kFroxelGridX * kFroxelGridY * kFroxelGridZ, 921600u);
}

/// The dispatch counts RegisterPasses derives: inject [8,8,4] one thread per froxel, scatter [8,8,1]
/// one thread per column.
TEST(FroxelGrid, DispatchGroupsCoverTheGridWithoutGaps) {
    const uint32_t gx = (kFroxelGridX + 7) / 8, gy = (kFroxelGridY + 7) / 8, gz = (kFroxelGridZ + 3) / 4;
    EXPECT_EQ(gx, 20u);
    EXPECT_EQ(gy, 12u);
    EXPECT_EQ(gz, 16u);
    EXPECT_GE(gx * 8, kFroxelGridX);
    EXPECT_GE(gy * 8, kFroxelGridY);
    EXPECT_GE(gz * 4, kFroxelGridZ);
    EXPECT_LT((gy - 1) * 8, kFroxelGridY);   // 90 is not a multiple of 8: the last row group is partial, not missing
}

/// RGBA16F volumes: 8 bytes per froxel. The two temporal history volumes cost 14.7 MB (decimal).
TEST(FroxelGrid, VolumeMemory) {
    const uint64_t bytesPerVolume = uint64_t(kFroxelGridX) * kFroxelGridY * kFroxelGridZ * 8u;
    EXPECT_EQ(bytesPerVolume, 7372800u);
    EXPECT_EQ(2u * bytesPerVolume, 14745600u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Exponential depth slicing (CPU mirror)
// ─────────────────────────────────────────────────────────────────────────────

/// tasks.md 1.1.2: sliceDepth = nearZ * pow(farZ/nearZ, (slice+0.5)/numSlices) when no start distance.
TEST(FroxelDepthSlicing, MatchesTheTaskFormula) {
    const SliceParams p{};
    for (uint32_t s : {0u, 1u, 31u, 63u}) {
        const float expected = p.nearZ * std::pow(p.farZ / p.nearZ, (s + 0.5f) / 64.0f);
        EXPECT_NEAR(SliceToDepth(p, s), expected, expected * 1e-6f) << "slice " << s;
    }
}

TEST(FroxelDepthSlicing, GridEdgesAreStartAndFar) {
    const SliceParams p{};
    EXPECT_NEAR(SliceCoordToDepth(p, 0.0f), 5.0f, 1e-4f);
    EXPECT_NEAR(SliceCoordToDepth(p, 1.0f), 6400.0f, 6400.0f * 1e-6f);
    // Slice centres lie strictly inside the grid.
    EXPECT_GT(SliceToDepth(p, 0), StartZ(p));
    EXPECT_LT(SliceToDepth(p, kFroxelGridZ - 1), p.farZ);
}

TEST(FroxelDepthSlicing, StartDistancePushesTheGridPastTheNearPlane) {
    SliceParams p{};
    p.fogStartDistance = 100.0f;
    EXPECT_FLOAT_EQ(StartZ(p), 100.0f);
    EXPECT_NEAR(SliceCoordToDepth(p, 0.0f), 100.0f, 1e-3f);
    EXPECT_NEAR(SliceCoordToDepth(p, 1.0f), 6400.0f, 6400.0f * 1e-6f);
    EXPECT_GT(SliceThickness(p, 0), 0.0f);
    // Below the near plane the near plane wins.
    p.fogStartDistance = 1.0f;
    EXPECT_FLOAT_EQ(StartZ(p), 5.0f);
}

TEST(FroxelDepthSlicing, StrictlyIncreasingWithPositiveThickness) {
    const SliceParams p{};
    float prev = StartZ(p);
    for (uint32_t s = 0; s < kFroxelGridZ; ++s) {
        const float d = SliceToDepth(p, s);
        EXPECT_GT(d, prev) << "slice " << s;
        EXPECT_GT(SliceThickness(p, s), 0.0f) << "slice " << s;
        EXPECT_NEAR(SliceThickness(p, s), d - prev, 1e-3f) << "slice " << s;
        prev = d;
    }
}

/// Exponential: every slice is the same factor deeper than the previous one — more resolution
/// near the camera (slice 0 is 0.3 cm thick, slice 63 is 6.4 m).
TEST(FroxelDepthSlicing, ConstantDepthRatioBetweenSlices) {
    const SliceParams p{};
    const float ratio = std::pow(p.farZ / StartZ(p), 1.0f / 64.0f);
    EXPECT_NEAR(ratio, 1.1183f, 1e-3f);
    for (uint32_t s = 1; s < kFroxelGridZ; ++s) {
        EXPECT_NEAR(SliceToDepth(p, s) / SliceToDepth(p, s - 1), ratio, 1e-4f) << "slice " << s;
    }
    EXPECT_NEAR(SliceThickness(p, 0), 0.29f, 0.01f);
    EXPECT_NEAR(SliceThickness(p, 63), 640.0f, 2.0f);
}

/// The scatter's accumDepth is the telescoping sum of the thicknesses: it reaches the LAST SLICE
/// CENTRE, not the far edge — the final half slice (farZ * (1 - (startZ/farZ)^(1/128)), 5.4 % of
/// the default grid) is not integrated and the composite clamps to the last centre beyond it.
TEST(FroxelDepthSlicing, ThicknessesTelescopeToTheLastSliceCentre) {
    const SliceParams p{};
    float accum = 0.0f;
    for (uint32_t s = 0; s < kFroxelGridZ; ++s) accum += SliceThickness(p, s);
    const float lastCentre = SliceToDepth(p, kFroxelGridZ - 1);
    EXPECT_NEAR(accum, lastCentre - StartZ(p), 0.05f);
    EXPECT_NEAR(lastCentre / p.farZ, std::pow(StartZ(p) / p.farZ, 0.5f / 64.0f), 1e-5f);
    EXPECT_NEAR(lastCentre, 6052.0f, 1.0f);
}

TEST(FroxelDepthSlicing, SliceCoordinateInvertsTheSliceDepth) {
    const SliceParams p{};
    for (uint32_t s = 0; s < kFroxelGridZ; ++s) {
        const float t = DepthToSliceCoord(p, SliceToDepth(p, s));
        EXPECT_NEAR(t, (s + 0.5f) / 64.0f, 1e-5f) << "slice " << s;
        // The composite samples texel coordinate t * 64 - 0.5 == s: a surface at a slice centre
        // reads that slice's integral without blending.
        EXPECT_NEAR(t * 64.0f - 0.5f, static_cast<float>(s), 1e-3f) << "slice " << s;
    }
}

/// The composite saturates the W coordinate: sky (view depth = farZ) and geometry beyond the grid
/// read the far edge, geometry in front of the start reads the first slice.
TEST(FroxelDepthSlicing, CompositeSaturatesBeyondTheGrid) {
    const SliceParams p{};
    EXPECT_FLOAT_EQ(CompositeSliceW(p, p.farZ), 1.0f);
    EXPECT_FLOAT_EQ(CompositeSliceW(p, 1.0e9f), 1.0f);        // geometry past the grid = sky = far edge
    EXPECT_FLOAT_EQ(CompositeSliceW(p, StartZ(p)), 0.0f);
    EXPECT_FLOAT_EQ(CompositeSliceW(p, 0.5f * StartZ(p)), 0.0f);
    EXPECT_NEAR(CompositeSliceW(p, 2000.0f), DepthToSliceCoord(p, 2000.0f), 1e-6f);
    // Inside the grid the coordinate is strictly monotonic in depth.
    float prev = -1.0f;
    for (float d = 10.0f; d < p.farZ; d *= 1.5f) {
        const float w = CompositeSliceW(p, d);
        EXPECT_GT(w, prev);
        prev = w;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// VolumetricFogPass on the recording mock device
// ─────────────────────────────────────────────────────────────────────────────

TEST(VolumetricFogPass, InitRejectsNullDevice) {
    VolumetricFogPass pass;
    EXPECT_FALSE(pass.Init(nullptr));
    EXPECT_FALSE(pass.IsInitialized());
    pass.Shutdown(nullptr);   // no-op
}

TEST(VolumetricFogPass, InitCreatesTheTwoGridVolumesOnly) {
    FogMockDevice device;
    VolumetricFogPass pass;
    ASSERT_TRUE(pass.Init(&device));
    EXPECT_TRUE(pass.IsInitialized());
    ASSERT_EQ(device.created.size(), 2u);
    for (const auto& t : device.created) {
        EXPECT_EQ(t.desc.width, kFroxelGridX) << t.name;
        EXPECT_EQ(t.desc.height, kFroxelGridY) << t.name;
        EXPECT_EQ(t.desc.depthOrArraySize, kFroxelGridZ) << t.name;
        EXPECT_TRUE(t.desc.is3D) << t.name;
        EXPECT_EQ(t.desc.mipLevels, 1u) << t.name;
        EXPECT_EQ(t.desc.format, RHIFormat::RGBA16Float) << t.name;
        EXPECT_EQ(t.desc.usage, RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess) << t.name;
    }
    const auto* inscatter = device.Find("FroxelFog_Inscattering");
    const auto* integrated = device.Find("FroxelFog_Integrated");
    ASSERT_NE(inscatter, nullptr);
    ASSERT_NE(integrated, nullptr);
    EXPECT_EQ(pass.GetInscatteringTexture().index, inscatter->handle.index);
    EXPECT_EQ(pass.GetIntegratedTexture().index, integrated->handle.index);
    EXPECT_EQ(pass.GetInscatteringSrv(), FogMockDevice::kSrvBase + inscatter->handle.index);
    EXPECT_EQ(pass.GetInscatteringUav(), FogMockDevice::kUavBase + inscatter->handle.index);
    EXPECT_EQ(pass.GetIntegratedSrv(), FogMockDevice::kSrvBase + integrated->handle.index);
    EXPECT_EQ(pass.GetIntegratedUav(), FogMockDevice::kUavBase + integrated->handle.index);
    // OFF costs no history memory: nothing beyond the two grid volumes.
    EXPECT_FALSE(pass.HistoryValid());

    // Idempotent.
    EXPECT_TRUE(pass.Init(&device));
    EXPECT_EQ(device.created.size(), 2u);
    pass.Shutdown(&device);
}

TEST(VolumetricFogPass, InitFailureReleasesTheFirstVolume) {
    FogMockDevice device;
    device.failCreateCall = 2;   // the integrated volume
    VolumetricFogPass pass;
    EXPECT_FALSE(pass.Init(&device));
    EXPECT_FALSE(pass.IsInitialized());
    ASSERT_EQ(device.created.size(), 1u);
    ASSERT_EQ(device.destroyed.size(), 1u);
    EXPECT_TRUE(device.Destroyed(device.created[0].handle));

    FogMockDevice device2;
    device2.failCreateCall = 1;   // the inscattering volume
    VolumetricFogPass pass2;
    EXPECT_FALSE(pass2.Init(&device2));
    EXPECT_TRUE(device2.created.empty());
    EXPECT_TRUE(device2.destroyed.empty());
}

TEST_F(VolumetricFogPassFixture, RegisterPassesRequiresBindless) {
    rg::RgBuilder rgb(&device, pool.get(), states.get());
    VolumetricFogFrameInputs in = Inputs(true);
    in.bindless = false;
    const CBAllocation before = ring.Allocate(256);
    pass.RegisterPasses(rgb, in, VolumetricFogCB{}, ring, &pipelines);
    const CBAllocation after = ring.Allocate(256);
    // Nothing happened: no history, no CB upload, no pass.
    EXPECT_EQ(device.created.size(), 2u);
    EXPECT_EQ(static_cast<const uint8_t*>(after.cpuPtr) - static_cast<const uint8_t*>(before.cpuPtr), 256);
    EXPECT_EQ(rgb.GetPassCount(), 0u);
    EXPECT_FALSE(pass.HistoryValid());
}

TEST_F(VolumetricFogPassFixture, UploadedCBCarriesTheGridAndDescriptorIndices) {
    rg::RgBuilder rgb(&device, pool.get(), states.get());
    VolumetricFogCB params{};
    params.farZ = 6400.0f;
    params.globalDensity = 0.0005f;
    params.temporalAlpha = 0.05f;
    params.frameIndex = 42u;
    params.depthSrvIdx = 77u;
    const VolumetricFogCB cb = Register(rgb, Inputs(false), params);
    EXPECT_EQ(cb.gridX, kFroxelGridX);
    EXPECT_EQ(cb.gridY, kFroxelGridY);
    EXPECT_EQ(cb.gridZ, kFroxelGridZ);
    EXPECT_EQ(cb.inscatteringUavIdx, pass.GetInscatteringUav());
    EXPECT_EQ(cb.inscatteringSrvIdx, pass.GetInscatteringSrv());
    EXPECT_EQ(cb.integratedUavIdx, pass.GetIntegratedUav());
    EXPECT_EQ(cb.integratedSrvIdx, pass.GetIntegratedSrv());
    // The caller's fields pass through untouched.
    EXPECT_FLOAT_EQ(cb.farZ, 6400.0f);
    EXPECT_FLOAT_EQ(cb.globalDensity, 0.0005f);
    EXPECT_FLOAT_EQ(cb.temporalAlpha, 0.05f);
    EXPECT_EQ(cb.frameIndex, 42u);
    EXPECT_EQ(cb.depthSrvIdx, 77u);
    // No temporal filter wanted: no history volumes, sentinel indices, no jitter.
    EXPECT_EQ(device.created.size(), 2u);
    EXPECT_EQ(cb.historySrvIdx, UINT32_MAX);
    EXPECT_EQ(cb.historyUavIdx, UINT32_MAX);
    EXPECT_EQ(cb.temporalEnabled, 0u);
    EXPECT_EQ(cb.jitterEnabled, 0u);
    EXPECT_FALSE(pass.HistoryValid());
}

TEST_F(VolumetricFogPassFixture, HistoryIsAllocatedOnTheFirstTemporalFrameOnly) {
    rg::RgBuilder rgb(&device, pool.get(), states.get());
    VolumetricFogCB params{};
    params.jitterEnabled = 1u;
    const VolumetricFogCB first = Register(rgb, Inputs(true), params);

    ASSERT_EQ(device.created.size(), 4u);
    const auto* h0 = device.Find("FroxelFog_History0");
    const auto* h1 = device.Find("FroxelFog_History1");
    ASSERT_NE(h0, nullptr);
    ASSERT_NE(h1, nullptr);
    EXPECT_TRUE(h0->desc.is3D);
    EXPECT_EQ(h0->desc.format, RHIFormat::RGBA16Float);
    EXPECT_EQ(h1->desc.depthOrArraySize, kFroxelGridZ);
    // First temporal frame: write history 0, read history 1; the blend is off (no history yet)
    // even though the camera matrix is continuous; the jitter follows the temporal filter.
    EXPECT_EQ(first.historyUavIdx, FogMockDevice::kUavBase + h0->handle.index);
    EXPECT_EQ(first.historySrvIdx, FogMockDevice::kSrvBase + h1->handle.index);
    EXPECT_EQ(first.temporalEnabled, 0u);
    EXPECT_EQ(first.jitterEnabled, 1u);
    // Without the scatter pipeline nothing wrote the history: it stays invalid.
    EXPECT_FALSE(pass.HistoryValid());
    EXPECT_EQ(rgb.GetPassCount(), 0u);

    // A second temporal frame reuses the volumes.
    rg::RgBuilder rgb2(&device, pool.get(), states.get());
    const VolumetricFogCB second = Register(rgb2, Inputs(true), params);
    EXPECT_EQ(device.created.size(), 4u);
    EXPECT_EQ(second.temporalEnabled, 0u);
    EXPECT_EQ(second.jitterEnabled, 1u);

    // Shutdown releases all four.
    pass.Shutdown(&device);
    EXPECT_EQ(device.destroyed.size(), 4u);
    EXPECT_FALSE(pass.IsInitialized());
    EXPECT_FALSE(pass.HistoryValid());
}

TEST_F(VolumetricFogPassFixture, JitterNeedsTheTemporalFilter) {
    rg::RgBuilder rgb(&device, pool.get(), states.get());
    VolumetricFogCB params{};
    params.jitterEnabled = 1u;
    const VolumetricFogCB cb = Register(rgb, Inputs(false), params);
    EXPECT_EQ(cb.jitterEnabled, 0u);
    EXPECT_EQ(device.created.size(), 2u);
}

TEST_F(VolumetricFogPassFixture, LocalLightsNeedTheLightingCB) {
    VolumetricFogCB params{};
    params.localLightsEnabled = 1u;
    params.localShadowEnabled = 1u;
    {
        rg::RgBuilder rgb(&device, pool.get(), states.get());
        VolumetricFogFrameInputs in = Inputs(false);
        in.lightingAlloc.gpuAddr = 0;   // no LightingCB this frame
        const VolumetricFogCB cb = Register(rgb, in, params);
        EXPECT_EQ(cb.localLightsEnabled, 0u);
        EXPECT_EQ(cb.localShadowEnabled, 0u);
    }
    {
        rg::RgBuilder rgb(&device, pool.get(), states.get());
        const VolumetricFogCB cb = Register(rgb, Inputs(false), params);
        EXPECT_EQ(cb.localLightsEnabled, 1u);
        EXPECT_EQ(cb.localShadowEnabled, 1u);
    }
    {
        rg::RgBuilder rgb(&device, pool.get(), states.get());
        params.localShadowEnabled = 0u;
        const VolumetricFogCB cb = Register(rgb, Inputs(false), params);
        EXPECT_EQ(cb.localLightsEnabled, 1u);
        EXPECT_EQ(cb.localShadowEnabled, 0u);
    }
    {
        rg::RgBuilder rgb(&device, pool.get(), states.get());
        params.localLightsEnabled = 0u;
        params.localShadowEnabled = 1u;   // shadows without lights are meaningless: off
        const VolumetricFogCB cb = Register(rgb, Inputs(false), params);
        EXPECT_EQ(cb.localLightsEnabled, 0u);
        EXPECT_EQ(cb.localShadowEnabled, 0u);
    }
}

TEST_F(VolumetricFogPassFixture, HistoryAllocationFailureDisablesTheTemporalFilter) {
    device.failCreateCall = 4;   // the second history volume
    rg::RgBuilder rgb(&device, pool.get(), states.get());
    VolumetricFogCB params{};
    params.jitterEnabled = 1u;
    const VolumetricFogCB cb = Register(rgb, Inputs(true), params);
    // History0 was created and released again; the frame runs without the filter.
    ASSERT_EQ(device.created.size(), 3u);
    ASSERT_EQ(device.destroyed.size(), 1u);
    EXPECT_TRUE(device.Destroyed(device.created[2].handle));
    EXPECT_EQ(cb.historySrvIdx, UINT32_MAX);
    EXPECT_EQ(cb.historyUavIdx, UINT32_MAX);
    EXPECT_EQ(cb.temporalEnabled, 0u);
    EXPECT_EQ(cb.jitterEnabled, 0u);
    EXPECT_FALSE(pass.HistoryValid());

    // The next temporal frame retries and succeeds.
    rg::RgBuilder rgb2(&device, pool.get(), states.get());
    const VolumetricFogCB retry = Register(rgb2, Inputs(true), params);
    EXPECT_EQ(device.created.size(), 5u);
    EXPECT_NE(retry.historySrvIdx, UINT32_MAX);
    EXPECT_NE(retry.historyUavIdx, UINT32_MAX);
    EXPECT_EQ(retry.jitterEnabled, 1u);
}

TEST_F(VolumetricFogPassFixture, InvalidateHistoryClearsValidity) {
    pass.InvalidateHistory();
    EXPECT_FALSE(pass.HistoryValid());
    EXPECT_TRUE(pass.IsInitialized());
}

// ─────────────────────────────────────────────────────────────────────────────
// Height fog handoff: the analytic fog starts at the froxel grid's far edge
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Registers the height fog pass for a HeightFogComponent with `authoredStart` and the froxel grid
/// edge `volumetricFogDistance`, and returns the HeightFogCB it uploaded.
HeightFogCB HeightFogCBFor(float authoredStart, float volumetricFogDistance) {
    FogMockDevice device;
    rg::RgResourcePool pool(&device);
    rg::RgBuilder rgb(&device, &pool);
    CBRingAllocator ring;
    EXPECT_TRUE(ring.Init(&device, 1, 16 * 1024));
    ring.BeginFrame(0);
    PipelineLibrary pipelines;
    World world;
    const Entity e = world.CreateEntity();
    auto& fog = world.AddComponent<HeightFogComponent>(e);
    fog.enabled = true;
    fog.density = 0.02f;
    fog.startDistance = authoredStart;
    Camera camera;
    camera.SetPosition({0.0f, 0.0f, 150.0f});
    const rg::RgResourceId hdr = rgb.ImportTexture("HDR", RHITextureHandle{1, 1}, RHIResourceState::RenderTarget);
    const rg::RgResourceId depth = rgb.ImportTexture("Depth", RHITextureHandle{2, 1}, RHIResourceState::ShaderResource);
    const CBAllocation probe = ring.Allocate(sizeof(HeightFogCB));
    AddHeightFogFromECS(rgb, hdr, depth, CBAllocation{}, &world, camera, &pipelines, ring,
                        5u, RHITextureHandle{2, 1}, true, 1920.f, 1080.f,
                        0.1f, 0.2f, 0.3f, volumetricFogDistance);
    EXPECT_EQ(rgb.GetPassCount(), 1u);
    HeightFogCB cb{};
    std::memcpy(&cb, static_cast<const uint8_t*>(probe.cpuPtr) + sizeof(HeightFogCB), sizeof(HeightFogCB));
    ring.Shutdown();
    return cb;
}

} // namespace

TEST(HeightFogHandoff, StartsAtTheFroxelGridFarEdge) {
    const HeightFogCB cb = HeightFogCBFor(0.0f, 6400.0f);
    EXPECT_FLOAT_EQ(cb.fogStartDistance, 6400.0f);
    EXPECT_FLOAT_EQ(cb.skyLightColorR, 0.1f);
    EXPECT_FLOAT_EQ(cb.skyLightColorB, 0.3f);
}

TEST(HeightFogHandoff, KeepsAFartherAuthoredStart) {
    const HeightFogCB cb = HeightFogCBFor(10000.0f, 6400.0f);
    EXPECT_FLOAT_EQ(cb.fogStartDistance, 10000.0f);
}

TEST(HeightFogHandoff, WithoutFroxelFogTheAuthoredStartIsUntouched) {
    EXPECT_FLOAT_EQ(HeightFogCBFor(0.0f, 0.0f).fogStartDistance, 0.0f);
    EXPECT_FLOAT_EQ(HeightFogCBFor(250.0f, 0.0f).fogStartDistance, 250.0f);
}
