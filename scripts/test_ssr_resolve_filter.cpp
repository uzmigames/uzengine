#include <algorithm>
#include <cmath>
#include <cstdio>

struct float3 {
    float x, y, z;
    float3(float a = 0, float b = 0, float c = 0) : x(a), y(b), z(c) {}
    float3& operator+=(float3 b) { x += b.x; y += b.y; z += b.z; return *this; }
    float3& operator*=(float b) { x *= b; y *= b; z *= b; return *this; }
};
struct float4 {
    float3 rgb;
    float a;
    float4(float3 color, float alpha) : rgb(color), a(alpha) {}
};
float3 operator*(float3 a, float b) { return {a.x*b, a.y*b, a.z*b}; }
float3 operator/(float3 a, float b) { return {a.x/b, a.y/b, a.z/b}; }
float dot(float3 a, float3 b) { return a.x*b.x + a.y*b.y + a.z*b.z; }
float max(float a, float b) { return std::max(a, b); }
float rcp(float a) { return 1.f/a; }
float saturate(float a) { return std::clamp(a, 0.f, 1.f); }

#include "ssr_resolve_filter.generated.h"

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    FILE* csv = std::fopen(argv[1], "w");
    if (!csv) return 2;
    std::fprintf(csv, "input_R,input_G,input_B,input_alpha,output_R,output_G,output_B,output_alpha,max_RGB_error,alpha_error\n");
    const float3 colors[] = {{.1f,.1f,.1f}, {.5f,.5f,.5f}, {1,1,1}, {2,2,2}, {8,8,8}, {8,2,.1f}, {.1f,8,.5f}};
    constexpr float alphas[] = {.25f, .5f, 1.f};
    int count = 0;
    int failures = 0;
    float maxRgbError = 0;
    float maxAlphaError = 0;
    for (float3 color : colors) {
        for (float alpha : alphas) {
            const auto result = ProductionResolve(float4(color, alpha));
            const float rgbError = std::max({std::abs(result.rgb.x-color.x), std::abs(result.rgb.y-color.y), std::abs(result.rgb.z-color.z)});
            const float alphaError = std::abs(result.a-alpha);
            const bool valid = std::isfinite(result.rgb.x) && std::isfinite(result.rgb.y) && std::isfinite(result.rgb.z) && std::isfinite(result.a)
                && rgbError <= 1e-5f && alphaError <= 1e-6f;
            failures += !valid;
            ++count;
            maxRgbError = std::max(maxRgbError, rgbError);
            maxAlphaError = std::max(maxAlphaError, alphaError);
            std::fprintf(csv, "%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g\n", color.x, color.y, color.z, alpha, result.rgb.x, result.rgb.y, result.rgb.z, result.a, rgbError, alphaError);
            if (alpha == .5f) std::printf("RGB=(%.3g,%.3g,%.3g) alpha=.5 -> RGB=(%.9g,%.9g,%.9g) alpha=%.9g\n", color.x, color.y, color.z, result.rgb.x, result.rgb.y, result.rgb.z, result.a);
        }
    }
    std::fclose(csv);
    std::printf("%d/%d cases passed; max RGB error %.9g; max alpha error %.9g\n", count-failures, count, maxRgbError, maxAlphaError);
    return failures ? 1 : 0;
}
