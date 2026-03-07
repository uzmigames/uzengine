// dof_coc.ps.hlsl -- Circle of Confusion computation
//
// Reads depth buffer, computes signed CoC diameter for each pixel.
// Positive = far field (behind focus), negative = near field.
// Output: R16_FLOAT with CoC in pixels.

cbuffer DofParams : register(b0) {
    float  focusDistance;  // world-space focus distance
    float  aperture;       // f-stop (lower = shallower DoF)
    float  focalLength;    // focal length in mm
    float  sensorHeight;   // sensor height (for pixel scale)
    float  nearPlane;
    float  farPlane;
    float  screenHeight;
    float  _dofPad;
};

Texture2D    depthTex    : register(t0);
SamplerState linearClamp : register(s1);

struct PSInput {
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

float LinearizeDepth(float d) {
    return nearPlane * farPlane / (farPlane - d * (farPlane - nearPlane));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float depth = depthTex.Load(int3(input.position.xy, 0)).r;
    float linearDepth = LinearizeDepth(depth);

    // Thin lens CoC diameter:
    // CoC = abs(A * f * (S1 - d) / (d * (S1 - f)))
    // where A = focalLength/aperture, f = focalLength, S1 = focusDistance, d = linearDepth
    float A = (focalLength * 0.001) / aperture;  // convert mm to meters
    float S1 = focusDistance;
    float num = A * (focalLength * 0.001) * (S1 - linearDepth);
    float den = linearDepth * (S1 - focalLength * 0.001);

    float coc = num / max(abs(den), 0.001);
    // Convert to pixels and clamp
    float cocPixels = coc * screenHeight * 0.5;
    cocPixels = clamp(cocPixels, -20.0, 20.0);

    return float4(cocPixels, 0.0, 0.0, 0.0);
}
