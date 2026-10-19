#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
#include "nrd_denoiser.h"
#include <cstddef>
#include <memory>
#include <span>

namespace uz {
class IRHIDevice;
class IRHICommandList;
class RTManager;
class World;
struct DrawCommand;
struct GPUMesh;
namespace rg { class RgBuilder; }

// Pass-private StructuredBuffer layout, not a constant buffer. Each TLAS
// instance has its own entry even when multiple draws share a mesh.
struct PipelinePathMaterial {
    float tint[3] = {1, 1, 1}; uint32_t albedoSrv = UINT32_MAX;
    uint32_t vbSrv = UINT32_MAX, ibSrv = UINT32_MAX, vertexStride = 0, uvOffset = 0;
    uint32_t normalOffset = 0, indexFormat = 4, normalFormat = 0, roughnessSrv = UINT32_MAX;
    uint32_t metallicSrv = UINT32_MAX, materialFlags = 0;
    float roughnessMultiplier = 1, metallicMultiplier = 1;
    float uvScale[2] = {1, 1}, uvOffsetValue[2] = {};
    float emission[3] = {}; uint32_t emissiveSrv = UINT32_MAX;
    uint32_t lightSourceIndex = UINT32_MAX;
    float coatWeight = 0, coatRoughness = 0.1f, coatIOR = 1.5f;
    float coatThickness = 1;
    float coatSigmaA[3] = {};
};
static_assert(sizeof(PipelinePathMaterial) == 128);
static_assert(offsetof(PipelinePathMaterial, coatWeight) == 100);
static_assert(offsetof(PipelinePathMaterial, coatThickness) == 112);
static_assert(offsetof(PipelinePathMaterial, coatSigmaA) == 116);
static_assert(offsetof(PipelinePathMaterial, metallicSrv) == 48);
static_assert(offsetof(PipelinePathMaterial, emission) == 80);

struct PipelinePathRoot {
    uint32_t materialTableSrv;
    uint32_t envMapSrv;
    uint32_t sampleIndex;
    uint32_t hasGeometry;
    uint32_t directionalLightsSrv;
    uint32_t directionalLightCount;
    uint32_t accumulationUav;
    uint32_t compensationUav;
    uint32_t accumulatedFrames;
    uint32_t maxBounces;
    uint32_t rrEnabled;
    uint32_t rrMinBounce;
    uint32_t restirEnabled, restirMode;
    uint32_t restirSurfaceUav, restirInitialUav, restirCurrentUav, restirPreviousUav;
    uint32_t restirWidth, restirHeight, restirHistory, neeSamples, restirTemporal, restirLightCount;
};
static_assert(sizeof(PipelinePathRoot) == 96);

struct PathVolumeRoot {
    float volumeSigmaA = 0, volumeSigmaS = 0, volumeG = 0, volumeRadius = 0;
    float volumeCenterX = 0, volumeCenterY = 0, volumeCenterZ = 0;
    uint32_t volumeEnabled = 0;
    float volumeVariation = 0, volumePadding0 = 0, volumePadding1 = 0, volumePadding2 = 0;
};
static_assert(sizeof(PathVolumeRoot) == 48);

struct RestirLightSample {
    uint32_t lightIndex;
    float position[3], normal[3], emission[3], pdf;
};
struct RestirReservoir {
    RestirLightSample sample;
    float weightSum;
    uint32_t M;
    float W;
    float surfacePosition[3], surfaceNormal[3];
    float surfaceView[3], surfaceRoughness;
    float surfaceAlbedo[3], surfaceMetallic;
};
struct RestirSurface {
    float position[3]; uint32_t valid;
    float normal[3], roughness;
    float view[3], metallic;
    float albedo[3], padding;
};
static_assert(sizeof(RestirLightSample) == 44);
static_assert(sizeof(RestirReservoir) == 112);
static_assert(sizeof(RestirSurface) == 64);

struct PipelinePathDirectionalLight {
    float direction[3];
    float angularRadius;
    float radiance[3];
    uint32_t padding = 0;
};
static_assert(sizeof(PipelinePathDirectionalLight) == 32);

class PipelinePathTracerPass {
public:
    static constexpr uint32_t kRootDwords = sizeof(PipelinePathRoot) / sizeof(uint32_t);
    static constexpr uint32_t kPayloadBytes = 88;
    static constexpr uint32_t kRecursionDepth = 2; // iterative surface loop + nested shadow
    PipelinePathTracerPass();
    ~PipelinePathTracerPass();
    bool Init(IRHIDevice* device);
    bool AddPass(rg::RgBuilder& builder, rg::RgResourceId hdr, CBAllocation perFrame,
                 RTManager& rt, IRHICommandList* buildCmd, std::span<const DrawCommand> draws,
                 std::span<const GPUMesh> meshes, uint32_t envSrv,
                 uint32_t frameIndex, uint32_t width, uint32_t height, World* world,
                 const Mat4& unjitteredViewProj, uint32_t sceneDirtyFlags,
                 const NrdInputs& denoiserGuides);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace uz
