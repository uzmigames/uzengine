#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
using std::max;
struct float2 { float x, y; };
float saturate(float x) { return std::clamp(x, 0.0f, 1.0f); }
float lerp(float a, float b, float t) { return a + (b - a) * t; }
float ComputeF0(float color, float metallic) { return lerp(0.04f, color, metallic); }
float F_Schlick(float f0, float f90, float cosine) { return f0 + (f90 - f0) * std::pow(1.0f - cosine, 5.0f); }
enum class MaterialShadingModel { DefaultLit, Unlit, Subsurface };
enum class EShadingModel { DefaultLit, Unlit, Subsurface };
struct Graph {
    uint32_t blendMode = 1;
    bool twoSided = false;
    MaterialShadingModel shadingModel = MaterialShadingModel::DefaultLit;
    struct Params {
        float baseColorTint[3] = {1, 1, 1};
        float roughnessMultiplier = 1, metallicMultiplier = 1, normalStrength = 1, aoMultiplier = 1;
        float specularMultiplier = 1;
    } params;
};
uint32_t Pack(float multiplier) {
    Graph input;
    input.params.specularMultiplier = multiplier;
    const Graph* graphForDraw = &input;
    struct Command {
        uint32_t materialFlags = (128u << 8) | (128u << 16), twoSided = 0;
        EShadingModel shadingModel = EShadingModel::DefaultLit;
        float baseColorTintR, baseColorTintG, baseColorTintB;
        float roughnessMultiplier, metallicMultiplier, normalIntensity, aoIntensity;
    } cmd;
    PRODUCTION_GRAPH
    return cmd.materialFlags;
}
struct Result { float f0, direct, indirect; };
Result Evaluate(float packedAlpha, unsigned smID, float baseColor, float metallic, float VoH, float2 envBRDF) {
    PRODUCTION_MATERIAL
    PRODUCTION_DIRECT
    PRODUCTION_INDIRECT
    return {F0, F, FssEss_d + Fms_d * Ems_d};
}
int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } };
    for (float multiplier : {-1.0f, 0.0f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f}) {
        const auto flags = Pack(multiplier);
        const auto packed = (flags >> 8) & 255;
        const auto expected = static_cast<unsigned>(std::clamp(multiplier * 0.5f, 0.0f, 1.0f) * 255.0f);
        check(packed == expected, "graph authored specular survives flags");
        check((flags & (255u << 16)) == (128u << 16), "packing preserves environment multiplier");
    }
    for (unsigned smID : {0u, 1u, 2u, 9u}) {
        for (float specular : {0.0f, 0.125f, 0.25f, 0.5f, 1.0f}) {
            for (float cosine : {0.0f, 0.2f, 0.7f, 1.0f}) {
                for (float2 dfg : {float2{0.9f,0.05f}, float2{0.4f,0.2f}, float2{0.2f,0.01f}}) {
                    const auto result = Evaluate(smID * 16.0f + specular, smID, 0.7f, 0, cosine, dfg);
                    check(std::abs(result.f0 - 0.08f * specular) < 1e-6f, "dielectric F0 matches packed material");
                    if (specular == 0) {
                        check(result.direct == 0 && result.indirect == 0, "zero specular extinguishes all angles and split sum");
                    }
                    if (specular >= 0.25f) {
                        const float expected = F_Schlick(0.08f * specular, 1, cosine);
                        check(std::abs(result.direct - expected) < 1e-6f, "ordinary dielectric Fresnel preserved");
                    }
                    const auto metal = Evaluate(smID * 16.0f + specular, smID, 0.7f, 1, cosine, dfg);
                    check(std::abs(metal.f0 - 0.7f) < 1e-6f && std::abs(metal.direct - F_Schlick(0.7f, 1, cosine)) < 1e-6f, "metal Fresnel independent of dielectric slider");
                }
            }
        }
    }
    std::printf("Material specular: %d/%d passed\n", checks - failures, checks);
    return failures ? 1 : 0;
}
