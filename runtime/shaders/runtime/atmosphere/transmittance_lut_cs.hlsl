/// UzEngine transmittance LUT — precomputed atmosphere optical transmittance.
/// @ref Bruneton & Neyret 2017 "Precomputed Atmospheric Scattering"
/// 256x64 RGBA16F. 40-step midpoint quadrature: Rayleigh + Mie + Ozone.

cbuffer AtmosphereLutParams : register(b0) {
    float3 rayleighBeta;
    float  rayleighScaleH;
    float  mieBeta;
    float  mieScaleH;
    float  bottomRadiusKm;
    float  topRadiusKm;
};

cbuffer OzoneParams : register(b2) {
    float3 ozoneAbsorption;
    float  ozoneLayerWidth;
    float  ozoneLinTerm0;
    float  ozoneConstTerm0;
    float  ozoneLinTerm1;
    float  ozoneConstTerm1;
};

RWTexture2D<float4> transmittanceLut : register(u0);

static const uint LUT_W = 256;
static const uint LUT_H = 64;
static const int  STEPS = 40;

float2 RaySphere(float3 o, float3 d, float r) {
    float b = dot(o, d); float c = dot(o, o) - r * r;
    float disc = b * b - c;
    if (disc < 0.0) return float2(-1, -1);
    float sq = sqrt(disc);
    return float2(-b - sq, -b + sq);
}

float OzDens(float alt) {
    return saturate(alt < ozoneLayerWidth
        ? ozoneLinTerm0 * alt + ozoneConstTerm0
        : ozoneLinTerm1 * alt + ozoneConstTerm1);
}

void UvToParams(float2 uv, out float h, out float cosZ) {
    float H = sqrt(max(0.0, topRadiusKm * topRadiusKm - bottomRadiusKm * bottomRadiusKm));
    float rho = H * uv.y;
    h = sqrt(rho * rho + bottomRadiusKm * bottomRadiusKm);
    float dMin = topRadiusKm - h;
    float dMax = rho + H;
    float d = dMin + uv.x * (dMax - dMin);
    cosZ = (d == 0.0) ? 1.0 : clamp((H*H - rho*rho - d*d) / (2.0*h*d), -1.0, 1.0);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID) {
    if (DTid.x >= LUT_W || DTid.y >= LUT_H) return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(LUT_W, LUT_H);
    float h, cosZ;
    UvToParams(uv, h, cosZ);

    float3 pos = float3(0, 0, h);
    float sinZ = sqrt(max(0.0, 1.0 - cosZ * cosZ));
    float3 dir = float3(sinZ, 0, cosZ);

    float2 hit = RaySphere(pos, dir, topRadiusKm);
    float tMax = max(0.0, hit.y);
    float2 gnd = RaySphere(pos, dir, bottomRadiusKm);
    if (gnd.x > 0.0) tMax = min(tMax, gnd.x);

    float dt = tMax / float(STEPS);
    float3 od = float3(0, 0, 0);
    for (int i = 0; i < STEPS; ++i) {
        float t = (float(i) + 0.5) * dt;
        float3 p = pos + dir * t;
        float alt = max(0.0, length(p) - bottomRadiusKm);
        od += (rayleighBeta * exp(-alt / rayleighScaleH)
             + mieBeta * 1.11 * exp(-alt / mieScaleH)
             + ozoneAbsorption * OzDens(alt)) * dt;
    }

    transmittanceLut[DTid.xy] = float4(exp(-od), 1.0);
}
