#pragma once

#include "constant_buffers.h"
#include "render_graph_types.h"
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
};
static_assert(sizeof(PipelinePathMaterial) == 96);
static_assert(offsetof(PipelinePathMaterial, metallicSrv) == 48);
static_assert(offsetof(PipelinePathMaterial, emission) == 80);

struct PipelinePathRoot {
    uint32_t materialTableSrv;
    uint32_t envMapSrv;
    uint32_t sampleIndex;
    uint32_t hasGeometry;
    uint32_t directionalLightsSrv;
    uint32_t directionalLightCount;
};
static_assert(sizeof(PipelinePathRoot) == 24);

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
    static constexpr uint32_t kPayloadBytes = 32;
    static constexpr uint32_t kRecursionDepth = 2; // primary + nested shadow, not two surface bounces
    PipelinePathTracerPass();
    ~PipelinePathTracerPass();
    bool Init(IRHIDevice* device);
    bool AddPass(rg::RgBuilder& builder, rg::RgResourceId hdr, CBAllocation perFrame,
                 RTManager& rt, IRHICommandList* buildCmd, std::span<const DrawCommand> draws,
                 std::span<const GPUMesh> meshes, uint32_t envSrv,
                 uint32_t frameIndex, uint32_t width, uint32_t height, World* world);
private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
} // namespace uz
