/// Editor grid pixel shader — infinite XY plane with procedural lines.
///
/// Reconstructs world position by unprojecting the pixel's NDC through
/// the inverse ViewProj, then intersects the camera ray with Z=0 plane.
/// Draws anti-aliased minor and major grid lines with distance-based fade.
/// Coordinate system: X-forward, Y-right, Z-up.

cbuffer GridCB : register(b1) {
    row_major float4x4 invViewProj;
    row_major float4x4 viewProj;
    float4 cameraPos;       // xyz = world position
    float4 gridParams;      // x = minorSpacing, y = majorInterval, z = fadeDistance
    uint   sceneDepthSrv;   // Bindless SRV index for scene depth (0xFFFFFFFF = use hardware depth test)
    float  displayWidth;    // Display-resolution width (for pixel-to-UV mapping)
    float  displayHeight;   // Display-resolution height
    float  _pad0;
};

struct PSInput {
    float4 position : SV_Position;
    float2 ndc      : TEXCOORD0;
};

/// Computes anti-aliased grid line intensity for one axis.
///
/// Uses screen-space derivatives (fwidth) for clean lines at any distance.
/// Returns 0.0 for no line, up to 1.0 for full line intensity.
float GridLine(float coord, float spacing) {
    float wrapped = frac(coord / spacing + 0.5) - 0.5;
    float lineDist = abs(wrapped) * spacing;
    float derivative = fwidth(coord);
    return 1.0 - smoothstep(0.0, derivative * 1.5, lineDist);
}

struct PSOutput {
    float4 color : SV_Target;
    float  depth : SV_Depth;
};

PSOutput main(PSInput input) {
    float minorSpacing = gridParams.x;
    float majorInterval = gridParams.y;
    float fadeDistance = gridParams.z;
    float majorSpacing = minorSpacing * majorInterval;

    // ── Reconstruct world ray from NDC ───────────────────────────────────
    // Reversed-Z: near plane = 1.0, far plane = 0.0
    float4 nearNDC = float4(input.ndc, 1.0, 1.0);
    float4 farNDC  = float4(input.ndc, 0.0, 1.0);

    float4 nearWorld = mul(nearNDC, invViewProj);
    float4 farWorld  = mul(farNDC,  invViewProj);
    nearWorld /= nearWorld.w;
    farWorld  /= farWorld.w;

    // ── Intersect ray with Z=0 plane (ground in Z-up coordinate system) ──
    float3 rayOrigin = nearWorld.xyz;
    float3 rayDir = farWorld.xyz - nearWorld.xyz;

    // t where ray.z = 0: origin.z + t * dir.z = 0
    float t = -rayOrigin.z / rayDir.z;

    // Discard pixels where ray doesn't hit the plane (behind camera or parallel)
    if (t < 0.0) {
        discard;
    }

    float3 worldPos = rayOrigin + rayDir * t;

    // ── Hard distance clamp — discard BEFORE any grid computation ─────────
    // This is the primary horizon-line fix: any pixel whose Z=0 hit point
    // is farther than fadeDistance from the camera is killed immediately.
    float dist = length(worldPos - cameraPos.xyz);
    if (dist > fadeDistance) {
        discard;
    }

    // ── Grid lines on XY plane (Z-up ground) ───────────────────────────────
    float minorX = GridLine(worldPos.x, minorSpacing);
    float minorY = GridLine(worldPos.y, minorSpacing);
    float minor = max(minorX, minorY);

    float majorX = GridLine(worldPos.x, majorSpacing);
    float majorY = GridLine(worldPos.y, majorSpacing);
    float major = max(majorX, majorY);

    // ── Axis highlights ──────────────────────────────────────────────────
    // X-axis (forward) = red line at Y=0
    float onXAxis = (abs(worldPos.y) < fwidth(worldPos.y) * 2.0) ? 1.0 : 0.0;
    // Y-axis (right) = green line at X=0
    float onYAxis = (abs(worldPos.x) < fwidth(worldPos.x) * 2.0) ? 1.0 : 0.0;

    // ── Distance fade — exponential falloff ──────────────────────────────
    // dist already computed above (before grid lines)
    float ratio = dist / fadeDistance;
    float fade = exp(-3.0 * ratio * ratio);

    // Base grid color: dark gray for minor, lighter for major
    float3 color = float3(0.3, 0.3, 0.3);
    float alpha = minor * 0.3;

    // Major lines: brighter
    color = lerp(color, float3(0.5, 0.5, 0.5), major);
    alpha = max(alpha, major * 0.5);

    // X-axis (forward) = red, Y-axis (right) = green
    color = lerp(color, float3(0.8, 0.2, 0.2), onXAxis);
    color = lerp(color, float3(0.2, 0.8, 0.2), onYAxis);
    alpha = max(alpha, max(onXAxis, onYAxis) * 0.8);

    alpha *= fade;

    if (alpha < 0.01) {
        discard;
    }

    // Project Z=0 hit point back to clip space for correct depth
    float4 clipPos = mul(float4(worldPos, 1.0), viewProj);
    float gridDepth = clipPos.z / clipPos.w;

    // ── Manual depth occlusion when no hardware depth buffer is attached ──
    // When screen percentage != 100%, the scene depth buffer is at render
    // resolution which doesn't match the display-resolution backbuffer.
    // Instead of hardware depth test, we sample the scene depth via bindless
    // and discard pixels where geometry is closer than the grid plane.
    // Reversed-Z: larger depth values are closer to the camera.
#ifdef UZ_BINDLESS
    if (sceneDepthSrv != 0xFFFFFFFF) {
        // Map display-resolution pixel coords to [0,1] UVs for depth sampling
        float2 uv = input.position.xy / float2(displayWidth, displayHeight);
        uv = saturate(uv);
        Texture2D<float> depthTex = ResourceDescriptorHeap[sceneDepthSrv];
        SamplerState pointSampler = SamplerDescriptorHeap[0];
        float sceneDepth = depthTex.SampleLevel(pointSampler, uv, 0);
        // Reversed-Z: scene geometry with depth > gridDepth is closer
        if (sceneDepth > gridDepth) {
            discard;
        }
    }
#endif
    // SM5 path: depth occlusion relies on hardware depth test (DepthStencilState).
    // When no bindless descriptor is available, the editor grid uses standard
    // depth testing against the scene depth buffer bound to the output merger.

    PSOutput output;
    output.color = float4(color, alpha);
    output.depth = gridDepth;
    return output;
}
