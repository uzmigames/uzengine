// The runner extracts the production HLSL basis and packing functions verbatim.
// This checks their arithmetic against analytic diffuse convolution, without a GPU.
#include <cmath>
#include <cstdio>

struct float3 { float x, y, z; };
struct float4 {
    float x, y, z, w;
    float4() = default;
    float4(float a, float b, float c, float d) : x(a), y(b), z(c), w(d) {}
};

#include "radiance_cache_sh.generated.h"

static float Evaluate(float3 n, const float4 (&p)[7], int channel) {
    const auto& a = p[channel];
    const auto& b = p[channel + 3];
    const float last = channel == 0 ? p[6].x : channel == 1 ? p[6].y : p[6].z;
    return a.x*n.x + a.y*n.y + a.z*n.z + a.w
         + b.x*n.x*n.y + b.y*n.y*n.z + b.z*n.z*n.z + b.w*n.z*n.x
         + last*(n.x*n.x-n.y*n.y);
}

int main() {
    constexpr float pi = 3.14159265358979323846f;
    constexpr int samples = 65536;
    const float3 normals[] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
    int failures = 0;
    // Analytic convolution of L(w)=1+a*w.axis is E(n)/pi=1+2*a*n.axis/3.
    // The quadratic Legendre band has convolution weight 1/4.
    for (int mode = 0; mode < 5; ++mode) {
        float coeffs[9]{};
        for (int i = 0; i < samples; ++i) {
            const float z = 1.0f - 2.0f*(i+0.5f)/samples;
            const float phi = i * 2.39996322972865332f;
            const float radius = std::sqrt(1.0f-z*z);
            const float3 direction{radius*std::cos(phi), radius*std::sin(phi), z};
            const float axis = mode == 1 ? direction.x : mode == 2 ? direction.y : direction.z;
            const float radiance = mode == 0 ? 1.0f : mode == 4
                ? 1.0f+0.6f*(3.0f*z*z-1.0f) : 1.0f+0.6f*axis;
            float basis[9];
            SHEvaluateL2(direction, basis);
            for (int b = 0; b < 9; ++b) coeffs[b] += radiance*basis[b]*(4*pi/samples);
        }
        float4 packed[7];
        PackL2(coeffs, coeffs, coeffs, packed);
        for (const auto normal : normals) {
            const float axis = mode == 1 ? normal.x : mode == 2 ? normal.y : normal.z;
            const float expected = mode == 0 ? 1.0f : mode == 4
                ? 1.0f+0.15f*(3.0f*normal.z*normal.z-1.0f) : 1.0f+0.4f*axis;
            for (int channel = 0; channel < 3; ++channel) {
                const float actual = Evaluate(normal, packed, channel);
                if (!std::isfinite(actual) || std::abs(actual-expected) > 0.003f) {
                    std::printf("FAIL mode=%d normal=(%.0f,%.0f,%.0f) channel=%d expected=%.4f actual=%.4f\n",
                        mode, normal.x, normal.y, normal.z, channel, expected, actual);
                    ++failures;
                }
            }
        }
    }
    std::printf("SH production arithmetic: %d/90 checks passed\n", 90-failures);
    return failures == 0 ? 0 : 1;
}
