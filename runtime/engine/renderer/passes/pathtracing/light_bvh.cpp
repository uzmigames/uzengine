#include "light_bvh.h"
#include "draw_list.h"
#include "gpu_mesh.h"
#include "mesh.h"
#include "world.h"
#include "scene_component.h"
#include "light_component.h"
#include "color_utils.h"
#include "cvar.h"
#include "log.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <numeric>
#include <limits>

UZ_DECLARE_LOG_CATEGORY(Renderer);

static auto* cv_LightBVH = UZ_CVAR_BOOL("r.LightBVH", false,
    "Opt-in many-light hierarchy for path tracer NEE (enable before loading meshes)");
static auto* cv_LightBVHUniform = UZ_CVAR_BOOL("r.LightBVH.Uniform", false,
    "Uniform light-selection reference using the same LightBVH emitters");
static auto* cv_LightBVHMorton = UZ_CVAR_BOOL("r.LightBVH.Morton", false,
    "Stage-one Morton/full-sphere reference for light-BVH quality comparisons");

namespace uz {
Vec3 PathMaterialEmission(const DrawCommand& draw) {
    // Compiled graphs carry (1,1,1) x 1 as a shader pass-through marker,
    // not table emission. Match UpdateGIMaterialTable's graph detection.
    const float intensity = draw.materialGraphPipeline.IsValid() ? 0.f : draw.emissiveIntensity;
    return {draw.emissiveColorR * intensity,
            draw.emissiveColorG * intensity,
            draw.emissiveColorB * intensity};
}

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

LightBVHNode LightBVH::Union(const LightBVHNode& a, const LightBVHNode& b) {
    LightBVHNode n;
    n.aabbMin = Min(a.aabbMin,b.aabbMin);
    n.aabbMax = Max(a.aabbMax,b.aabbMax);
    n.totalEmissionPower = a.totalEmissionPower + b.totalEmissionPower;
    n.coneAxis = a.coneAxis;
    if (a.coneCosHalfAngle == -2 || b.coneCosHalfAngle == -2) {
        n.coneCosHalfAngle = -2;
        return n;
    }
    // Minimal enclosing spherical cap of two caps. Antipodal axes use a
    // deterministic perpendicular great circle, avoiding a singular slerp.
    constexpr float pi = 3.14159265359f;
    float ta = std::acos(std::clamp(a.coneCosHalfAngle,-1.f,1.f));
    float tb = std::acos(std::clamp(b.coneCosHalfAngle,-1.f,1.f));
    float d = std::acos(std::clamp(a.coneAxis.Dot(b.coneAxis),-1.f,1.f));
    if (ta >= std::min(pi,d+tb)) { n.coneCosHalfAngle=a.coneCosHalfAngle; return n; }
    if (tb >= std::min(pi,d+ta)) { n.coneAxis=b.coneAxis; n.coneCosHalfAngle=b.coneCosHalfAngle; return n; }
    float theta = std::min(pi,.5f*(d+ta+tb));
    Vec3 tangent = b.coneAxis-a.coneAxis*std::cos(d);
    if (tangent.LengthSq() < 1e-12f)
        tangent = a.coneAxis.Cross(std::abs(a.coneAxis.x)<.9f ? Vec3{1,0,0} : Vec3{0,1,0});
    tangent = tangent.Normalized();
    float rotation = theta-ta;
    n.coneAxis = (a.coneAxis*std::cos(rotation)+tangent*std::sin(rotation)).Normalized();
    // Round outward after the axis rotation.
    theta = std::max(ta+std::acos(std::clamp(n.coneAxis.Dot(a.coneAxis),-1.f,1.f)),
                     tb+std::acos(std::clamp(n.coneAxis.Dot(b.coneAxis),-1.f,1.f)));
    n.coneCosHalfAngle = std::cos(std::min(pi,theta+1e-6f));
    return n;
}

float LightBVH::SplitCost(const LightBVHNode& n) {
    // Conty Estevez & Kulla 2018, Eq. 1; Falcor LightBVHBuilder::evalSAOH.
    // Flat diffuse emission has theta_e=pi/2. Two-sided emission duplicates
    // the cap; the common factor of two cancels in split comparisons.
    constexpr float pi = 3.14159265359f;
    float theta = std::acos(std::clamp(n.coneCosHalfAngle,-1.f,1.f));
    float w = std::min(theta+pi*.5f,pi), s = std::sin(theta), c = std::cos(theta);
    float orientation = 2*pi*(1-c)+pi*.5f*(2*w*s-std::cos(theta-2*w)-2*theta*s+c);
    Vec3 e = n.aabbMax-n.aabbMin;
    return n.totalEmissionPower * (2*(e.x*e.y+e.y*e.z+e.z*e.x)) * orientation;
}

void LightBVH::Build(std::vector<LightBVHLight> input, bool mortonReference) {
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
    auto leaf = [&](const LightBVHLight& l) {
        LightBVHNode n;
        n.aabbMin = l.type == 0 ? Min(l.v0,Min(l.v1,l.v2)) : Vec3{};
        n.aabbMax = l.type == 0 ? Max(l.v0,Max(l.v1,l.v2)) : Vec3{};
        n.coneAxis = l.normal;
        n.coneCosHalfAngle = l.type == 1 ? -2.f : (mortonReference ? -1.f : 1.f);
        n.totalEmissionPower = Luminance(l.emission)*(l.type == 0 ? l.area : 1.f);
        return n;
    };
    auto build = [&](auto&& self, uint32_t begin, uint32_t end, uint32_t parent, uint32_t depth) -> uint32_t {
        uint32_t id = uint32_t(nodes.size());
        nodes.emplace_back();
        nodes[id].parent = parent;
        if (end - begin == 1) {
            auto& l = lights[begin];
            auto& n = nodes[id];
            n.lightOffset = begin; n.lightCount = 1;
            auto bounds = leaf(l);
            n.aabbMin=bounds.aabbMin; n.aabbMax=bounds.aabbMax;
            n.coneAxis=bounds.coneAxis; n.coneCosHalfAngle=bounds.coneCosHalfAngle;
            n.totalEmissionPower=bounds.totalEmissionPower;
            l.leaf = id;
        } else {
            uint32_t mid = begin + (end - begin) / 2;
            if (!mortonReference) {
                float best = std::numeric_limits<float>::infinity();
                uint32_t count = end-begin, bestSplit=count/2;
                std::vector<LightBVHLight> order(lights.begin()+begin,lights.begin()+end), bestOrder=order;
                std::vector<LightBVHNode> prefix(count), suffix(count);
                // Exact sweeps over spatial and normal axes also handle
                // co-located emitters with different orientations.
                for (uint32_t axis=0; axis<6; ++axis) {
                    auto key = [&](const auto& l) {
                        Vec3 p = axis<3 ? center(l) : l.normal;
                        return axis%3 == 0 ? p.x : (axis%3 == 1 ? p.y : p.z);
                    };
                    std::stable_sort(order.begin(),order.end(),[&](const auto& a,const auto& b) { return key(a)<key(b); });
                    prefix[0]=leaf(order[0]); suffix[count-1]=leaf(order[count-1]);
                    for (uint32_t i=1;i<count;++i) prefix[i]=Union(prefix[i-1],leaf(order[i]));
                    for (uint32_t i=count-1;i>0;--i) suffix[i-1]=Union(leaf(order[i-1]),suffix[i]);
                    for (uint32_t i=1;i<count;++i) {
                        // Each child must fit in the remaining 32-level shader budget.
                        uint64_t capacity=uint64_t(1)<<(31-depth);
                        if (i>capacity || count-i>capacity) continue;
                        float cost=SplitCost(prefix[i-1])+SplitCost(suffix[i]);
                        if (cost<best) { best=cost; bestSplit=i; bestOrder=order; }
                    }
                }
                std::copy(bestOrder.begin(),bestOrder.end(),lights.begin()+begin);
                mid=begin+bestSplit;
            }
            uint32_t left = self(self, begin, mid, id, depth+1), right = self(self, mid, end, id, depth+1);
            auto bounds=Union(nodes[left],nodes[right]);
            auto& n = nodes[id];
            n.leftChild = left; n.rightChild = right;
            n.aabbMin = Min(nodes[left].aabbMin, nodes[right].aabbMin);
            n.aabbMax = Max(nodes[left].aabbMax, nodes[right].aabbMax);
            n.totalEmissionPower = nodes[left].totalEmissionPower + nodes[right].totalEmissionPower;
            n.coneAxis=bounds.coneAxis; n.coneCosHalfAngle=bounds.coneCosHalfAngle;
        }
        return id;
    };
    build(build, 0, uint32_t(lights.size()), UINT32_MAX, 0);
}

bool LightBVH::Gather(std::span<const DrawCommand> draws, std::span<const GPUMesh> meshes, World* world) {
    std::vector<LightBVHLight> input;
    // Hash transport inputs before extraction: unchanged meshes never rescan triangles.
    uint64_t signature = 14695981039346656037ull;
    auto hash = [&](const void* data, size_t bytes) {
        const auto* p = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < bytes; ++i) signature = (signature ^ p[i]) * 1099511628211ull;
    };
    bool mortonReference=cv_LightBVHMorton->value >= .5f;
    hash(&mortonReference,sizeof(mortonReference));
    for (const auto& d : draws) {
        hash(&d.meshId, sizeof(d.meshId));
        hash(&d.worldMatrix, sizeof(d.worldMatrix));
        const bool graphDraw = d.materialGraphPipeline.IsValid();
        hash(&graphDraw, sizeof(graphDraw));
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
        const Vec3 emission = PathMaterialEmission(d);
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
    Build(std::move(input),mortonReference);
    UZ_LOG(Renderer, Info, "LightBVH: {} build {} lights, {} nodes, root power={}",
        mortonReference ? "Morton reference" : "SAOH/cone", lights.size(), nodes.size(),
        nodes.empty() ? 0.f : nodes[0].totalEmissionPower);
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
    float orientation=1.f;
    if (n.coneCosHalfAngle > 0) {
        float distance=std::sqrt(std::max(delta.LengthSq(),1e-20f));
        float radius=.5f*(n.aabbMax-n.aabbMin).Length();
        float subtended=std::asin(std::min(radius/distance,1.f));
        float theta=std::acos(std::clamp(std::abs(n.coneAxis.Dot(delta))/distance,0.f,1.f));
        orientation=std::max(std::cos(std::max(0.f,theta-std::acos(n.coneCosHalfAngle)-subtended)),.01f);
    }
    return n.totalEmissionPower * cosine * orientation / d2;
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
