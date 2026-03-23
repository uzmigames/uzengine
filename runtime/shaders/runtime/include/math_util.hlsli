/// Math constants and helpers.
#ifndef MATH_UTIL_HLSLI
#define MATH_UTIL_HLSLI

#ifndef UZ_PI_DEFINED
#define UZ_PI_DEFINED
static const float PI = 3.14159265358979f;
#endif

float Pow2(float x) { return x * x; }
float Pow4(float x) { float x2 = x * x; return x2 * x2; }
float Pow5(float x) { float x2 = x * x; return x2 * x2 * x; }

float3 LinearToSrgb(float3 c) {
    float3 lo = c * 12.92;
    float3 hi = 1.055 * pow(max(c, 0.0), 1.0 / 2.4) - 0.055;
    return float3(c.r <= 0.0031308 ? lo.r : hi.r,
                  c.g <= 0.0031308 ? lo.g : hi.g,
                  c.b <= 0.0031308 ? lo.b : hi.b);
}

float3 SrgbToLinear(float3 c) {
    float3 lo = c / 12.92;
    float3 hi = pow((c + 0.055) / 1.055, 2.4);
    return float3(c.r <= 0.04045 ? lo.r : hi.r,
                  c.g <= 0.04045 ? lo.g : hi.g,
                  c.b <= 0.04045 ? lo.b : hi.b);
}

#endif
