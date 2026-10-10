/// @ref Microsoft DXR 1.0 Spec — Minimal ray gen shader for RT infrastructure validation.
///
/// Outputs a UV gradient (DispatchRaysIndex / DispatchRaysDimensions) to a UAV texture.
/// No actual ray tracing — purely validates the RT pipeline, shader table, and DispatchRays.
///
/// # Exports
/// - `RayGen` — ray generation shader (writes UV gradient to output UAV)
/// - `Miss`   — miss shader (no-op, required for valid DispatchRays)
/// - `ClosestHit` — closest-hit shader (no-op, required for valid hit group)

// Output UAV — receives the UV gradient
RWTexture2D<float4> g_Output : register(u0);

// Acceleration structure (not actually used, but required for valid RTPSO)
RaytracingAccelerationStructure g_SceneTLAS : register(t0);

struct [raypayload] RayPayload {
    float4 color : write(closesthit, miss) : read(caller);   // the hit and miss shaders only write it
};

[shader("raygeneration")]
void RayGen() {
    uint2 launchIndex = DispatchRaysIndex().xy;
    uint2 launchDim   = DispatchRaysDimensions().xy;

    float2 uv = float2(launchIndex) / float2(launchDim);

    // Output UV gradient: R = u, G = v, B = 0, A = 1
    g_Output[launchIndex] = float4(uv.x, uv.y, 0.0, 1.0);
}

[shader("miss")]
void Miss(inout RayPayload payload) {
    payload.color = float4(0, 0, 0, 1);
}

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in BuiltInTriangleIntersectionAttributes attr) {
    payload.color = float4(attr.barycentrics.x, attr.barycentrics.y, 0, 1);
}
