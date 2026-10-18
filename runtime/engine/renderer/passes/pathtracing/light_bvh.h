#pragma once

#include "vector3.h"
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace uz {
struct Mesh;
struct DrawCommand;
struct GPUMesh;
class World;

// Source positions/UVs are retained only when r.LightBVH is enabled before
// mesh upload. They share the uploaded mesh's lifetime and LOD0 topology.
struct LightBVHGeometry {
    struct Vertex { Vec3 position; float uv[2]; };
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
};
std::shared_ptr<const LightBVHGeometry> CaptureLightBVHGeometry(const Mesh& mesh);

// Shared structured-buffer contracts; no shared CB or root signature changes.
struct LightBVHNode {
    Vec3 aabbMin; float totalEmissionPower = 0;
    Vec3 aabbMax; uint32_t leftChild = 0;
    Vec3 coneAxis; float coneCosHalfAngle = -1;
    uint32_t rightChild = 0, lightOffset = 0, lightCount = 0, parent = UINT32_MAX;
};
struct LightBVHLight {
    Vec3 v0; float area = 0;
    Vec3 v1; uint32_t type = 0; // 0: two-sided triangle, 1: directional
    Vec3 v2; uint32_t sourceIndex = 0;
    Vec3 normal; uint32_t emissiveSrv = UINT32_MAX;
    Vec3 emission; float angularRadius = 0;
    float uv0[2]{}, uv1[2]{}, uv2[2]{};
    uint32_t leaf = 0, padding = 0;
};
struct LightBVHConfig {
    uint32_t nodesSrv, lightsSrv, lightCount, uniformSampling;
};
static_assert(sizeof(LightBVHNode) == 64);
static_assert(sizeof(LightBVHLight) == 112);
static_assert(sizeof(LightBVHConfig) == 16);

class LightBVH {
public:
    std::vector<LightBVHNode> nodes;
    std::vector<LightBVHLight> lights;
    void Build(std::vector<LightBVHLight> input);
    bool Gather(std::span<const DrawCommand> draws, std::span<const GPUMesh> meshes,
                World* world);
    float Importance(uint32_t node, const Vec3& position, const Vec3& normal) const;
    uint32_t Sample(const Vec3& position, const Vec3& normal, float u, float& pdf) const;
    float EvaluatePDF(const Vec3& position, const Vec3& normal, uint32_t light) const;
private:
    uint64_t m_signature = 0;
};
} // namespace uz
