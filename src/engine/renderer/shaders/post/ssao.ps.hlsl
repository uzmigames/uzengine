// ssao.ps.hlsl -- Screen-Space Ambient Occlusion (GTAO-inspired)
//
// Half-resolution pass: reads depth buffer, outputs R8 occlusion factor.
// Uses hemisphere sampling with 12 samples + 4x4 dithered rotation.
//
// References:
//   [Jimenez 2016] "Practical Real-Time Strategies for Accurate Indirect Occlusion"
//   [Bavoil/Sainz 2008] "HBAO: Horizon-Based Ambient Occlusion"
//   UE5 — Engine/Shaders/Private/ScreenSpaceAmbientOcclusion.usf

cbuffer ProjCB : register(b0) {
    float4x4 projMatrix;       // camera projection matrix (16 DWORDs)
};

cbuffer SSAOParams : register(b1) {
    float2   screenSize;       // full resolution (width, height)
    float    nearPlane;        // camera near plane
    float    farPlane;         // camera far plane
    float    aoRadius;         // world-space AO radius (default 0.5)
    float    aoIntensity;      // AO strength multiplier (default 1.5)
    float    aoBias;           // depth bias to prevent self-occlusion (default 0.025)
    float    _pad;
};

Texture2D<float> depthTex : register(t0);
SamplerState      pointClamp : register(s1);  // clamp sampler (reuse s1)

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

static const float PI = 3.14159265359;

// 12 hemisphere sample kernel (tangent-space, z >= 0)
// Distributed with cos-weighted density, scaled by index for distance variation
static const float3 g_kernel[12] = {
    float3( 0.0352,  0.0299,  0.0210),
    float3(-0.0484,  0.0377,  0.0491),
    float3( 0.0711, -0.0437,  0.0767),
    float3(-0.0252,  0.1082,  0.0397),
    float3( 0.1288, -0.0289,  0.0861),
    float3(-0.0836, -0.1176,  0.0610),
    float3( 0.1773, -0.0046,  0.0960),
    float3(-0.0503,  0.1812,  0.1254),
    float3( 0.2254,  0.0587,  0.0954),
    float3(-0.1890, -0.1476,  0.1486),
    float3( 0.0687,  0.2930,  0.0792),
    float3(-0.2930,  0.0987,  0.1781),
};

// 4x4 rotation vectors for dithered sampling (repeating tile)
static const float2 g_noise[16] = {
    float2(-0.7071,  0.7071), float2( 0.0000, -1.0000),
    float2( 0.7071,  0.7071), float2(-1.0000,  0.0000),
    float2( 0.7071, -0.7071), float2( 0.0000,  1.0000),
    float2(-0.7071, -0.7071), float2( 1.0000,  0.0000),
    float2( 0.3827,  0.9239), float2(-0.9239,  0.3827),
    float2( 0.9239, -0.3827), float2(-0.3827, -0.9239),
    float2(-0.9239, -0.3827), float2( 0.3827, -0.9239),
    float2( 0.9239,  0.3827), float2(-0.3827,  0.9239),
};

// Linearize depth from [0,1] NDC to view-space Z
float LinearizeDepth(float d) {
    // Reverse-Z not used here; standard DX projection: z = (far*(d-near)) / (far-near)
    // Solve for view-z: z_view = near * far / (far - d * (far - near))
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

// Reconstruct view-space position from UV + depth
float3 ReconstructViewPos(float2 uv, float depth) {
    float z = LinearizeDepth(depth);
    // NDC to view-space XY using projection matrix inverse
    float2 ndc = uv * 2.0 - 1.0;
    ndc.y = -ndc.y;  // DX Y flip
    float x = ndc.x * z / projMatrix[0][0];
    float y = ndc.y * z / projMatrix[1][1];
    return float3(x, y, z);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float depth = depthTex.SampleLevel(pointClamp, input.texcoord, 0).r;

    // Skip sky pixels (depth = 1.0 or very close)
    if (depth >= 0.9999) return float4(1.0, 1.0, 1.0, 1.0);

    float3 viewPos = ReconstructViewPos(input.texcoord, depth);

    // Reconstruct normal from depth via cross products of neighbor positions
    float2 texelSize = 1.0 / screenSize;
    float depthL = depthTex.SampleLevel(pointClamp, input.texcoord + float2(-texelSize.x, 0), 0).r;
    float depthR = depthTex.SampleLevel(pointClamp, input.texcoord + float2( texelSize.x, 0), 0).r;
    float depthU = depthTex.SampleLevel(pointClamp, input.texcoord + float2(0, -texelSize.y), 0).r;
    float depthD = depthTex.SampleLevel(pointClamp, input.texcoord + float2(0,  texelSize.y), 0).r;

    float3 posL = ReconstructViewPos(input.texcoord + float2(-texelSize.x, 0), depthL);
    float3 posR = ReconstructViewPos(input.texcoord + float2( texelSize.x, 0), depthR);
    float3 posU = ReconstructViewPos(input.texcoord + float2(0, -texelSize.y), depthU);
    float3 posD = ReconstructViewPos(input.texcoord + float2(0,  texelSize.y), depthD);

    float3 dPdx = (abs(posR.z - viewPos.z) < abs(posL.z - viewPos.z)) ? (posR - viewPos) : (viewPos - posL);
    float3 dPdy = (abs(posD.z - viewPos.z) < abs(posU.z - viewPos.z)) ? (posD - viewPos) : (viewPos - posU);
    float3 normal = normalize(cross(dPdx, dPdy));

    // Dithered rotation from 4x4 tile
    int2 pixelCoord = int2(input.position.xy);
    int noiseIdx = (pixelCoord.x % 4) + (pixelCoord.y % 4) * 4;
    float2 rotVec = g_noise[noiseIdx];

    float occlusion = 0.0;
    float radiusScaled = aoRadius / viewPos.z;  // Scale radius by distance

    [unroll]
    for (int i = 0; i < 12; ++i) {
        // Rotate sample XY by dither angle
        float3 sampleVec = g_kernel[i];
        float2 rotated = float2(
            sampleVec.x * rotVec.x - sampleVec.y * rotVec.y,
            sampleVec.x * rotVec.y + sampleVec.y * rotVec.x
        );
        float3 sampleOffset = float3(rotated, sampleVec.z);

        // Flip if below surface
        sampleOffset = sampleOffset * sign(dot(sampleOffset, normal));

        // Scale to world-space radius
        float3 samplePos = viewPos + sampleOffset * aoRadius;

        // Project back to screen UV
        float4 projected = mul(projMatrix, float4(samplePos, 1.0));
        float2 sampleUV = (projected.xy / projected.w) * 0.5 + 0.5;
        sampleUV.y = 1.0 - sampleUV.y;

        // Sample depth at that screen position
        float sampleDepth = depthTex.SampleLevel(pointClamp, sampleUV, 0).r;
        float sampleZ = LinearizeDepth(sampleDepth);

        // Range check: ignore samples too far from the surface
        float rangeCheck = smoothstep(0.0, 1.0, aoRadius / abs(viewPos.z - sampleZ));

        // Occlusion: sample is closer to camera than expected = occluded
        occlusion += (sampleZ <= samplePos.z - aoBias ? 1.0 : 0.0) * rangeCheck;
    }

    occlusion = 1.0 - (occlusion / 12.0) * aoIntensity;
    occlusion = saturate(occlusion);

    return float4(occlusion, occlusion, occlusion, 1.0);
}
