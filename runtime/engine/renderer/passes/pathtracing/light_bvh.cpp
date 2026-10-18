#include "light_bvh.h"
#include "draw_list.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "world.h"
#include "scene_component.h"
#include "light_component.h"
#include "color_utils.h"
#include "cvar.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>

static auto* cv_LightBVH = UZ_CVAR_BOOL("r.LightBVH", false,
    "Opt-in many-light hierarchy for path tracer NEE (enable before loading meshes)");
static auto* cv_LightBVHUniform = UZ_CVAR_BOOL("r.LightBVH.Uniform", false,
    "Uniform light-selection reference using the same LightBVH emitters");

namespace uz {
static float Luminance(Vec3 c) { return c.x * .2126f + c.y * .7152f + c.z * .0722f; }
static Vec3 Min(Vec3 a, Vec3 b) { return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)}; }
static Vec3 Max(Vec3 a, Vec3 b) { return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)}; }

std::shared_ptr<const LightBVHGeometry> CaptureLightBVHGeometry(const Mesh& mesh) {
    if (cv_LightBVH->value < .5f || mesh.gpuSkinned) return {};
    auto geometry = std::make_shared<LightBVHGeometry>();
    geometry->vertices.resize(mesh.GetVertexCount());
    const uint32_t uvOffset = mesh.header.vertexStride == sizeof(VertexPacked)
        ? offsetof(VertexPacked, texcoord) : offsetof(Vertex, texcoord);
    for (uint32_t i = 0; i < mesh.GetVertexCount(); ++i) {
        const auto* source = mesh.vertexData.data() + size_t(i) * mesh.header.vertexStride;
        std::memcpy(&geometry->vertices[i].position, source, sizeof(Vec3));
        std::memcpy(geometry->vertices[i].uv, source + uvOffset, sizeof(float) * 2);
    }
    geometry->indices.resize(mesh.header.indexCount ? mesh.header.indexCount : mesh.GetVertexCount());
    for (uint32_t i = 0; i < geometry->indices.size(); ++i) {
        uint32_t index = i;
        if (mesh.header.indexCount) {
            if (mesh.header.indexFormat == MeshIndexFormat::UInt16) {
                uint16_t value;
                std::memcpy(&value, mesh.indexData.data() + size_t(i) * 2, 2);
                index = value;
            } else std::memcpy(&index, mesh.indexData.data() + size_t(i) * 4, 4);
        }
        geometry->indices[i] = index;
    }
    return geometry;
}

void LightBVH::Build(std::vector<LightBVHLight> input) {
    nodes.clear();
    lights = std::move(input);
    std::erase_if(lights, [](const auto& l) {
        return Luminance(l.emission) <= 0 || (l.type == 0 && l.area <= 0);
    });
    if (lights.empty()) return;
    Vec3 lo = lights[0].v0, hi = lo;
    auto center = [](const auto& l) { return l.type == 0 ? (l.v0 + l.v1 + l.v2) / 3.f : Vec3{}; };
    for (const auto& l : lights) { lo = Min(lo, center(l)); hi = Max(hi, center(l)); }
    auto morton = [&](const auto& l) {
        Vec3 p = center(l) - lo, extent = hi - lo;
        uint32_t xyz[3] = {
            uint32_t(std::clamp(p.x / std::max(extent.x, 1e-6f), 0.f, 1.f) * 1023),
            uint32_t(std::clamp(p.y / std::max(extent.y, 1e-6f), 0.f, 1.f) * 1023),
            uint32_t(std::clamp(p.z / std::max(extent.z, 1e-6f), 0.f, 1.f) * 1023)};
        uint32_t code = 0;
        for (uint32_t bit = 0; bit < 10; ++bit)
            for (uint32_t axis = 0; axis < 3; ++axis)
                code |= ((xyz[axis] >> bit) & 1u) << (3 * bit + axis);
        return code;
    };
    std::stable_sort(lights.begin(), lights.end(), [&](const auto& a, const auto& b) {
        return morton(a) < morton(b);
    });
    nodes.reserve(2 * lights.size() - 1);
    // Median splits of Morton order keep depth <= 32 for uint32 light counts.
    // Full SAH optimization is deliberately a later stage.
    auto build = [&](auto&& self, uint32_t begin, uint32_t end, uint32_t parent) -> uint32_t {
        uint32_t id = uint32_t(nodes.size());
        nodes.emplace_back();
        nodes[id].parent = parent;
        if (end - begin == 1) {
            auto& l = lights[begin];
            auto& n = nodes[id];
            n.lightOffset = begin; n.lightCount = 1;
            n.aabbMin = l.type == 0 ? Min(l.v0, Min(l.v1,l.v2)) : Vec3{};
            n.aabbMax = l.type == 0 ? Max(l.v0, Max(l.v1,l.v2)) : Vec3{};
            // Negative cone cosine is a conservative full-sphere bound for
            // two-sided emitters, directional sources, and mixed nodes.
            n.coneAxis = l.normal;
            if (l.type == 1) n.coneCosHalfAngle = -2;
            n.totalEmissionPower = Luminance(l.emission) * (l.type == 0 ? l.area : 1.f);
            l.leaf = id;
        } else {
            uint32_t mid = begin + (end - begin) / 2;
            uint32_t left = self(self, begin, mid, id), right = self(self, mid, end, id);
            auto& n = nodes[id];
            n.leftChild = left; n.rightChild = right;
            n.aabbMin = Min(nodes[left].aabbMin, nodes[right].aabbMin);
            n.aabbMax = Max(nodes[left].aabbMax, nodes[right].aabbMax);
            n.totalEmissionPower = nodes[left].totalEmissionPower + nodes[right].totalEmissionPower;
            if (nodes[left].coneCosHalfAngle == -2 || nodes[right].coneCosHalfAngle == -2)
                n.coneCosHalfAngle = -2;
        }
        return id;
    };
    build(build, 0, uint32_t(lights.size()), UINT32_MAX);
}

bool LightBVH::Gather(std::span<const DrawCommand> draws, std::span<const GPUMesh> meshes, World* world) {
    std::vector<LightBVHLight> input;
    // Hash transport inputs before extraction: unchanged meshes never rescan triangles.
    uint64_t signature = 14695981039346656037ull;
    auto hash = [&](const void* data, size_t bytes) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i) signature = (signature ^ p[i]) * 1099511628211ull;
    };
    for (const auto& d : draws) {
        hash(&d.meshId, sizeof(d.meshId));
        hash(&d.worldMatrix, sizeof(d.worldMatrix));
        hash(&d.emissiveColorR, 4 * sizeof(float));
        hash(&d.emissiveSrvIndex, sizeof(d.emissiveSrvIndex));
        hash(&d.uvScaleX, 4 * sizeof(float));
        if (d.meshId < meshes.size()) {
            auto* source = meshes[d.meshId].lightBVHGeometry.get();
            hash(&source, sizeof(source));
        }
    }
    if (world) {
        auto view = world->GetRegistry().view<DirectionalLightComponent>();
        for (auto entity : view) {
            const auto& light = view.get<DirectionalLightComponent>(entity);
            const auto* scene = world->GetRegistry().try_get<SceneComponent>(entity);
            Vec3 direction = scene ? (scene->GetRotation() * Vec3::Forward()).Normalized()
                                   : light.GetDirection().Normalized();
            Vec3 color = light.color;
            if (light.useTemperature) color = color * KelvinToRgb(light.temperature);
            LightBVHLight l{};
            l.type = 1; l.v0 = direction; l.emission = color * light.intensity;
            l.angularRadius = light.lightSourceAngle * (3.14159265359f / 360.f);
            input.push_back(l);
            hash(&l, sizeof(l));
        }
    }
    if (signature == m_signature) return false;
    m_signature = signature;
    for (const auto& d : draws) {
        Vec3 emission{d.emissiveColorR * d.emissiveIntensity,
                      d.emissiveColorG * d.emissiveIntensity,
                      d.emissiveColorB * d.emissiveIntensity};
        if (Luminance(emission) <= 0 || !IsGIOccluder(d) || d.meshId >= meshes.size()) continue;
        const auto& source = meshes[d.meshId].lightBVHGeometry;
        if (!source) continue;
        for (size_t i = 0; i + 2 < source->indices.size(); i += 3) {
            LightBVHLight l{};
            const auto& a = source->vertices[source->indices[i]];
            const auto& b = source->vertices[source->indices[i+1]];
            const auto& c = source->vertices[source->indices[i+2]];
            l.v0 = d.worldMatrix.TransformPoint(a.position);
            l.v1 = d.worldMatrix.TransformPoint(b.position);
            l.v2 = d.worldMatrix.TransformPoint(c.position);
            Vec3 cross = (l.v1-l.v0).Cross(l.v2-l.v0);
            l.area = .5f * cross.Length();
            if (l.area <= 0) continue;
            l.normal = cross.Normalized(); l.emission = emission;
            l.sourceIndex = uint32_t(&d - draws.data());
            l.padding = uint32_t(i / 3); // BLAS PrimitiveIndex, retained through Morton sorting
            l.emissiveSrv = d.emissiveSrvIndex ? d.emissiveSrvIndex : UINT32_MAX;
            const LightBVHGeometry::Vertex* v[] = {&a,&b,&c};
            float* uv[] = {l.uv0,l.uv1,l.uv2};
            for (uint32_t j = 0; j < 3; ++j) {
                uv[j][0] = v[j]->uv[0] * d.uvScaleX + d.uvOffsetX;
                uv[j][1] = v[j]->uv[1] * d.uvScaleY + d.uvOffsetY;
            }
            input.push_back(l);
        }
    }
    Build(std::move(input));
    return true;
}

float LightBVH::Importance(uint32_t id, const Vec3& p, const Vec3& normal) const {
    const auto& n = nodes[id];
    // Directionals have no finite spatial extent. Any mixed ancestor uses a
    // power-only conservative estimate; probabilities still telescope exactly.
    if (n.coneCosHalfAngle == -2) return n.totalEmissionPower;
    Vec3 delta = (n.aabbMin + n.aabbMax) * .5f - p;
    float d2 = std::max(delta.LengthSq(), 1.f);
    float cosine = std::max(normal.Dot(delta) / std::sqrt(d2), .01f);
    return n.totalEmissionPower * cosine / d2;
}
uint32_t LightBVH::Sample(const Vec3& p, const Vec3& normal, float u, float& pdf) const {
    uint32_t id = 0; pdf = 1;
    while (!nodes[id].lightCount) {
        const auto& n = nodes[id];
        float a = Importance(n.leftChild,p,normal), b = Importance(n.rightChild,p,normal);
        float q = std::clamp(a / (a+b), 1e-6f, 1.f-1e-6f);
        if (u < q) { pdf *= q; u /= q; id = n.leftChild; }
        else { pdf *= 1-q; u = (u-q)/(1-q); id = n.rightChild; }
    }
    return nodes[id].lightOffset;
}
float LightBVH::EvaluatePDF(const Vec3& p, const Vec3& normal, uint32_t light) const {
    float pdf = 1;
    for (uint32_t id = lights[light].leaf; nodes[id].parent != UINT32_MAX;) {
        uint32_t parent = nodes[id].parent;
        const auto& n = nodes[parent];
        float a = Importance(n.leftChild,p,normal), b = Importance(n.rightChild,p,normal);
        float q = std::clamp(a/(a+b), 1e-6f, 1.f-1e-6f);
        pdf *= id == n.leftChild ? q : 1-q;
        id = parent;
    }
    return pdf;
}
} // namespace uz
