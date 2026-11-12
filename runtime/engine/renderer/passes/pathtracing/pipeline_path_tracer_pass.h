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
class IVFS;
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
    uint32_t stackDepth = 0;
    float stackRoughness[3] = {0.2f,0.3f,0.4f};
    float stackIOR[3] = {1.33f,1.5f,1.33f};
    float stackThickness[3] = {1,1,1};
    float stackSigmaAR[3] = {}, stackSigmaAG[3] = {}, stackSigmaAB[3] = {};
};
static_assert(sizeof(PipelinePathMaterial) == 204);
static_assert(offsetof(PipelinePathMaterial, stackDepth) == 128);
static_assert(offsetof(PipelinePathMaterial, stackRoughness) == 132);
static_assert(offsetof(PipelinePathMaterial, stackIOR) == 144);
static_assert(offsetof(PipelinePathMaterial, stackThickness) == 156);
static_assert(offsetof(PipelinePathMaterial, stackSigmaAR) == 168);
static_assert(offsetof(PipelinePathMaterial, stackSigmaAG) == 180);
static_assert(offsetof(PipelinePathMaterial, stackSigmaAB) == 192);
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

// The two former padding DWORDs carry the grid media: the structured buffer of
// PathVolumeGridDesc and its count. The private root stays 12 DWORDs.
struct PathVolumeRoot {
    float volumeSigmaA = 0, volumeSigmaS = 0, volumeG = 0, volumeRadius = 0;
    float volumeCenterX = 0, volumeCenterY = 0, volumeCenterZ = 0;
    uint32_t volumeEnabled = 0;
    float volumeVariation = 0;
    uint32_t volumeGridSrv = UINT32_MAX, volumeGridCount = 0;
    float volumePadding2 = 0;
};
static_assert(sizeof(PathVolumeRoot) == 48);
static_assert(offsetof(PathVolumeRoot, volumeGridSrv) == 36);

// Mirrors PathVolumeGrid in pathtracer_volume.hlsli: one VolumeComponent's
// oriented box and medium. worldToLocal maps world points into the box's
// [-1,1]^3 frame (rows are affine: local_i = dot(row_i.xyz, P) + row_i.w).
struct PathVolumeGridDesc {
    float worldToLocal[3][4] = {};
    float sigmaA = 0, sigmaS = 0, phaseG = 0, maxDensity = 0;
    float emission[3] = {}; uint32_t densitySrv = UINT32_MAX;
    float densityScale = 1, halfTexel[3] = {};
};
static_assert(sizeof(PathVolumeGridDesc) == 96);
static_assert(offsetof(PathVolumeGridDesc, sigmaA) == 48);
static_assert(offsetof(PathVolumeGridDesc, emission) == 64);
static_assert(offsetof(PathVolumeGridDesc, densityScale) == 80);

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
    float visibility;
};
struct RestirSurface {
    float position[3]; uint32_t valid;
    float normal[3], roughness;
    float view[3], metallic;
    float albedo[3], padding;
};
static_assert(sizeof(RestirLightSample) == 44);
static_assert(sizeof(RestirReservoir) == 116);
static_assert(offsetof(RestirReservoir, visibility) == 112);
static_assert(sizeof(RestirSurface) == 64);

// Mirrors RestirCameraHistory in include/restir_di.hlsli: the previous frame's
// PerFrameCB view-projection rows and camera position. Temporal reuse
// reprojects this frame's primary hit through them when the camera moved, so
// reservoir history survives camera motion instead of resetting with the
// accumulation. An unchanged camera keeps the exact same-pixel lookup.
struct RestirCameraHistory {
    float viewProjRows[4][4] = {};
    float cameraPos[4] = {};
    uint32_t cameraMoved = 0;
    uint32_t padding[3] = {};
};
static_assert(sizeof(RestirCameraHistory) == 96);
static_assert(offsetof(RestirCameraHistory, cameraMoved) == 80);

// Reservoir history survives camera motion (it is reprojected). It is dropped
// by a transport change (scene, materials, lights, path-tracer CVars), a frame
// gap, a manual reset, scene edits, or when ReSTIR did not run last frame at
// this resolution. Pure, so the rule is unit-testable without a device.
struct RestirHistoryInputs {
    bool enabled = false;
    bool previousEnabled = false;
    uint32_t frameIndex = 0;
    uint32_t previousFrame = UINT32_MAX;
    uint32_t resetGeneration = 0;
    uint32_t previousResetGeneration = 0;
    uint32_t sceneDirtyFlags = 0;
    uint64_t transportSignature = 0;
    uint64_t previousTransportSignature = 0;
};
inline bool RestirHistoryValid(const RestirHistoryInputs& in) {
    return in.enabled && in.previousEnabled
        && in.previousFrame != UINT32_MAX && in.frameIndex == in.previousFrame + 1u
        && in.sceneDirtyFlags == 0u && in.resetGeneration == in.previousResetGeneration
        && in.transportSignature == in.previousTransportSignature;
}

// Mirrors GISample / GIReservoir / RestirGIRoot in include/restir_gi.hlsli:
// the one-bounce secondary vertex with its own material and single NEE light
// sample, so its exitant radiance is re-evaluated toward any receiver.
struct RestirGISample {
    float position[3]; uint32_t lightIndex;
    float normal[3], roughness;
    float albedo[3], metallic;
    float emission[3], lightPdf;
    float lightDirection[3];
    float lightValue[3];
};
struct RestirGIReservoir {
    RestirGISample sample;
    float weightSum;
    uint32_t M;
    float W;
    float surfacePosition[3], surfaceNormal[3];
    float surfaceView[3], surfaceRoughness;
    float surfaceAlbedo[3], surfaceMetallic;
    float visibility;
};
struct RestirGIRoot {
    uint32_t giRestirEnabled = 0, giRestirInitialUav = 0, giRestirCurrentUav = 0, giRestirPreviousUav = 0;
    uint32_t giRestirPartialUav = 0, giRestirRadianceUav = 0, giRestirTemporal = 0, giRestirSpatial = 0;
};
static_assert(sizeof(RestirGISample) == 88);
static_assert(sizeof(RestirGIReservoir) == 160);
static_assert(offsetof(RestirGIReservoir, weightSum) == 88);
static_assert(offsetof(RestirGIReservoir, visibility) == 156);
static_assert(sizeof(RestirGIRoot) == 32);

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
                 const NrdInputs& denoiserGuides, IVFS* vfs);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace uz
