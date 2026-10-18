#ifndef UZ_LIGHT_BVH
#define UZ_LIGHT_BVH
#include "light_bvh_math.hlsli"

struct LightBVHNode {
    float3 aabbMin; float totalEmissionPower;
    float3 aabbMax; uint leftChild;
    float3 coneAxis; float coneCosHalfAngle;
    uint rightChild; uint lightOffset; uint lightCount; uint parent;
};
struct LightBVHLight {
    float3 v0; float area;
    float3 v1; uint type;
    float3 v2; uint sourceIndex;
    float3 normal; uint emissiveSrv;
    float3 emission; float angularRadius;
    float2 uv0; float2 uv1; float2 uv2;
    uint leaf; uint padding;
};
struct LightBVHConfig {
    uint nodesSrv; uint lightsSrv; uint lightCount; uint uniformSampling;
};
struct LightBVHSample {
    uint lightId;
    float selectionPdf;
    float3 samplePos;
    float3 sampleNormal;
    float2 uv;
    float pdf; // joint area density = P(select light) / triangle area
};

float LightBVHImportance(LightBVHNode node, float3 P, float3 N) {
    if (node.coneCosHalfAngle == -2.0) return node.totalEmissionPower;
    float3 delta = .5 * (node.aabbMin + node.aabbMax) - P;
    float d2 = max(dot(delta,delta), 1.0);
    // Keep support when a triangle spans the receiver's hemisphere boundary.
    return node.totalEmissionPower * max(dot(N,delta) * rsqrt(d2), .01) / d2;
}
float LightBVHLeftProbability(LightBVHNode node, float3 P, float3 N,
                              StructuredBuffer<LightBVHNode> bvh) {
    float a = LightBVHImportance(bvh[node.leftChild],P,N);
    float b = LightBVHImportance(bvh[node.rightChild],P,N);
    return clamp(a/(a+b), 1e-6, 1.0-1e-6);
}
// Stack-free single-path descent: rescale the variate after every branch.
// Balanced Morton construction bounds depth by 32 for uint32 light counts.
LightBVHSample SampleLightBVH(float3 P, float3 N, float3 u,
    LightBVHConfig config, StructuredBuffer<LightBVHNode> bvh,
    StructuredBuffer<LightBVHLight> lights) {
    LightBVHSample result = (LightBVHSample)0;
    result.selectionPdf = 1.0;
    uint id = 0;
    if (config.uniformSampling != 0u) {
        result.lightId = min(uint(u.x * config.lightCount), config.lightCount-1u);
        result.selectionPdf = 1.0 / config.lightCount;
    } else {
        for (uint depth = 0; depth < 32 && bvh[id].lightCount == 0u; ++depth) {
            LightBVHNode node = bvh[id];
            float q = LightBVHLeftProbability(node,P,N,bvh);
            if (u.x < q) {
                result.selectionPdf *= q; u.x /= q; id = node.leftChild;
            } else {
                result.selectionPdf *= 1.0-q; u.x = (u.x-q)/(1.0-q); id = node.rightChild;
            }
        }
        result.lightId = bvh[id].lightOffset;
    }
    LightBVHLight light = lights[result.lightId];
    float root = sqrt(u.y);
    float3 weights = float3(1.0-root, root*(1.0-u.z), root*u.z);
    result.samplePos = weights.x*light.v0 + weights.y*light.v1 + weights.z*light.v2;
    result.sampleNormal = light.normal;
    result.uv = weights.x*light.uv0 + weights.y*light.uv1 + weights.z*light.uv2;
    result.pdf = result.selectionPdf / max(light.area,1e-20);
    return result;
}
float EvaluateLightBVHPDF(float3 P, float3 N, uint lightId,
    LightBVHConfig config, StructuredBuffer<LightBVHNode> bvh,
    StructuredBuffer<LightBVHLight> lights) {
    if (config.uniformSampling != 0u) return 1.0/config.lightCount;
    float pdf = 1.0;
    uint id = lights[lightId].leaf;
    for (uint depth = 0; depth < 32 && bvh[id].parent != 0xffffffffu; ++depth) {
        uint parent = bvh[id].parent;
        LightBVHNode node = bvh[parent];
        float q = LightBVHLeftProbability(node,P,N,bvh);
        pdf *= id == node.leftChild ? q : 1.0-q;
        id = parent;
    }
    return pdf;
}
#endif
