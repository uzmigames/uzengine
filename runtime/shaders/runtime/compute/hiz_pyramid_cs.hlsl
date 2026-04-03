/// UzEngine Hi-Z Pyramid — min-depth downscale compute shader.
/// Each dispatch reads the previous mip and writes min(2x2) to the current mip.
/// Used for hierarchical occlusion culling and SSR acceleration.
/// @ref Wihlidal 2016 "Optimizing the Graphics Pipeline with Compute"
/// @constants thread group = 8x8, min of 2x2 block per thread

cbuffer HiZConstants : register(b0) {
    uint   outputWidth;     // 0
    uint   outputHeight;    // 1
    uint   inputWidth;      // 2
    uint   inputHeight;     // 3
    uint   inputSrvIndex;   // 4
    uint   _pad0;           // 5
    uint   _pad1;           // 6
    uint   _pad2;           // 7
};

RWTexture2D<float> outputMip : register(u0);
SamplerState pointClamp : register(s0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= outputWidth || DTid.y >= outputHeight) return;

    Texture2D<float> inputDepth = ResourceDescriptorHeap[inputSrvIndex];

    // Source coordinates: each output texel covers a 2x2 block in the input.
    // Clamp to input dimensions to handle odd-size mips correctly.
    uint2 srcBase = DTid.xy * 2;

    uint2 s00 = min(srcBase + uint2(0, 0), uint2(inputWidth - 1, inputHeight - 1));
    uint2 s10 = min(srcBase + uint2(1, 0), uint2(inputWidth - 1, inputHeight - 1));
    uint2 s01 = min(srcBase + uint2(0, 1), uint2(inputWidth - 1, inputHeight - 1));
    uint2 s11 = min(srcBase + uint2(1, 1), uint2(inputWidth - 1, inputHeight - 1));

    float d00 = inputDepth.Load(int3(s00, 0)).r;
    float d10 = inputDepth.Load(int3(s10, 0)).r;
    float d01 = inputDepth.Load(int3(s01, 0)).r;
    float d11 = inputDepth.Load(int3(s11, 0)).r;

    // Reversed-Z: near=1.0, far=0.0. For occlusion culling we want the
    // MINIMUM depth (closest to far plane = most conservative occluder).
    // For SSR we also want min to find the closest geometry.
    float minDepth = min(min(d00, d10), min(d01, d11));

    outputMip[DTid.xy] = minDepth;
}
