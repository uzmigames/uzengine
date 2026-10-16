#ifndef UZ_VELOCITY_COMMON_HLSLI
#define UZ_VELOCITY_COMMON_HLSLI

// Surface current - previous, unjittered, in the CURRENT render-pixel grid.
// +x right, +y down. TAA/TSR: historyUV = uv - velocity / renderSize.
// Output-pixel consumers scale by outputSize/renderSize. DLSS/FSR2 negate.
float2 NdcToRenderPixels(float2 ndc, float2 renderSize) {
    // Keep the component expression order of the established fullscreen pass:
    // factoring the size multiply out can change DXC's floating-point contraction.
    return float2((ndc.x * 0.5f + 0.5f) * renderSize.x,
                  (-ndc.y * 0.5f + 0.5f) * renderSize.y);
}

#endif
