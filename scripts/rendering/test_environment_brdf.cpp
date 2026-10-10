#include <algorithm>
#include <cmath>
#include <cstdio>

struct float2 {
    float x, y;
    float2(float a = 0, float b = 0) : x(a), y(b) {}
};
struct float4 {
    float x, y, z, w;
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
    float2 zw() const { return {z, w}; }
};
float2 operator*(float2 a, float b) { return {a.x*b, a.y*b}; }
float2 operator*(float a, float2 b) { return b*a; }
float2 operator+(float2 a, float2 b) { return {a.x+b.x, a.y+b.y}; }
float4 operator*(float4 a, float b) { return {a.x*b, a.y*b, a.z*b, a.w*b}; }
float4 operator*(float a, float4 b) { return b*a; }
float4 operator+(float4 a, float4 b) { return {a.x+b.x, a.y+b.y, a.z+b.z, a.w+b.w}; }
float min(float a, float b) { return std::min(a, b); }
float max(float a, float b) { return std::max(a, b); }
float saturate(float a) { return std::clamp(a, 0.f, 1.f); }

#include "environment_brdf.generated.h"

// Independent oracle: Karis 2014, https://www.unrealengine.com/blog/physically-based-shading-on-mobile
float2 KarisReference(double roughness, double noV) {
    const double x = 1.0 - roughness;
    const double y = 0.0425 - 0.0275 * roughness;
    const double z = 1.04 - 0.572 * roughness;
    const double w = 0.022 * roughness - 0.04;
    const double a004 = std::min(x*x, std::exp2(-9.28 * noV)) * x + y;
    return {static_cast<float>(z - 1.04*a004), static_cast<float>(w + 1.04*a004)};
}

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    FILE* csv = std::fopen(argv[1], "w");
    if (!csv) return 2;
    std::fprintf(csv, "path,roughness,NoV,actual_A,actual_B,expected_A,expected_B,max_error,dielectric_actual,dielectric_expected\n");
    constexpr float values[] = {0.f, 0.001f, 0.01f, 0.05f, 0.1f, 0.25f, 0.5f, 0.75f, 0.9f, 1.f};
    int failures = 0;
    int count = 0;
    float worst = 0.f;
    for (int path = 0; path < 2; ++path) {
        for (float roughness : values) {
            for (float noV : values) {
                const float2 expected = KarisReference(roughness, path == 0 ? noV : 1.f);
                const float2 actual = path == 0 ? ProductionSpecular(roughness, noV) : ProductionDiffuse(roughness, noV);
                const float error = std::max(std::abs(actual.x-expected.x), std::abs(actual.y-expected.y));
                const bool valid = std::isfinite(actual.x) && std::isfinite(actual.y) && error < 1e-6f;
                failures += !valid;
                ++count;
                worst = std::max(worst, error);
                std::fprintf(csv, "%s,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", path ? "diffuse" : "specular", roughness, noV, actual.x, actual.y, expected.x, expected.y, error, 0.04f*actual.x+actual.y, 0.04f*expected.x+expected.y);
                if (path == 0 && ((roughness == 0.5f && noV == 0.1f) || (roughness == 0.f && noV == 0.f))) {
                    std::printf("roughness=%.3f NoV=%.3f actualAB=(%.9g,%.9g) expectedAB=(%.9g,%.9g) dielectric=%.9g expected=%.9g\n", roughness, noV, actual.x, actual.y, expected.x, expected.y, 0.04f*actual.x+actual.y, 0.04f*expected.x+expected.y);
                }
            }
        }
    }
    std::fclose(csv);
    std::printf("%d/%d cases passed; max component error %.9g\n", count-failures, count, worst);
    return failures ? 1 : 0;
}
