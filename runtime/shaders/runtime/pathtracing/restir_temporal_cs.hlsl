#define RESTIR_COMPUTE
#include "pathtracer_shared.hlsli"
#include "../include/restir_di.hlsli"

[numthreads(8,8,1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= restirWidth || id.y >= restirHeight) return;
    uint index = id.y * restirWidth + id.x;
    RWStructuredBuffer<RestirSurface> surfaces = ResourceDescriptorHeap[restirSurfaceUav];
    RWStructuredBuffer<Reservoir> initial = ResourceDescriptorHeap[restirInitialUav];
    RWStructuredBuffer<Reservoir> previous = ResourceDescriptorHeap[restirPreviousUav];
    RWStructuredBuffer<Reservoir> output = ResourceDescriptorHeap[restirCurrentUav];
    RestirSurface surface = surfaces[index];
    Reservoir r = initial[index];
    // Initial RIS represents 32 candidates. Temporal M measures independent
    // frame reservoirs, capped to 20 frames, not 20 of those 32 candidates.
    r.M = surface.valid != 0u && restirLightCount > 0u ? 1u : 0u;
    float selectedTarget = RestirVisibilityTarget(RestirTarget(RestirContribution(surface, r.sample)), r.visibility);
    r.weightSum = selectedTarget * r.W;
    if (restirHistory != 0u && restirTemporal != 0u && r.M > 0u) {
        // History survives camera motion: restirHistory is the SRV index + 1 of
        // last frame's camera. An unchanged camera keeps the same pixel exactly;
        // a moved camera reprojects this frame's primary hit through last frame's
        // view-projection (static geometry; a moved object fails the surface
        // gate below instead). Transport/scene/reset changes drop the history
        // on the CPU. Subpixel jitter moves hits along the surface, so validate
        // point-to-plane separation, not hit depth.
        StructuredBuffer<RestirCameraHistory> cameras = ResourceDescriptorHeap[restirHistory - 1u];
        RestirCameraHistory camera = cameras[0];
        int2 historyPixel = int2(id.xy);
        bool found = true;
        if (camera.cameraMoved != 0u) {
            float4 clip = surface.position.x * camera.viewProjRow0 + surface.position.y * camera.viewProjRow1
                        + surface.position.z * camera.viewProjRow2 + camera.viewProjRow3;
            found = clip.w > 0.0;
            if (found) {
                float2 ndc = clip.xy / clip.w;
                // Inverse of the raygen mapping: uv = (pixel + 0.5 + jitter) / size,
                // ndc = (2 uv.x - 1, 1 - 2 uv.y).
                float2 uv = float2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
                historyPixel = int2(floor(uv * float2(restirWidth, restirHeight)));
                found = all(historyPixel >= 0) && historyPixel.x < int(restirWidth)
                     && historyPixel.y < int(restirHeight);
            }
        }
        if (found) {
            Reservoir old = previous[uint(historyPixel.y) * restirWidth + uint(historyPixel.x)];
            float tolerance = max(0.01, length(surface.position - cameraPos.xyz) * 1e-4);
            if (old.M > 0u && dot(old.surfaceNormal, surface.normal) >= 0.9
                && abs(dot(old.surfacePosition - surface.position, surface.normal)) <= tolerance) {
                uint oldM = min(old.M, 19u);
                RestirSurface source = RestirSource(old);
                float currentHere = selectedTarget;
                float unusedVisibility, oldVisibilityHere;
                float currentThere = RestirVisibleTarget(source, r.sample, unusedVisibility);
                float oldHere = RestirVisibleTarget(surface, old.sample, oldVisibilityHere);
                float oldThere = RestirVisibilityTarget(RestirTarget(RestirContribution(source, old.sample)), old.visibility);
                // Both candidates see BOTH domains, even if one happened to
                // produce a null sample. M is a deterministic MIS preference,
                // not a normalization applied again after resampling.
                r.weightSum = RestirReuseWeight(currentHere, r.W,
                    RestirMIS(currentHere, 1u, currentHere + float(oldM) * currentThere));
                float weight = RestirReuseWeight(oldHere, old.W,
                    RestirMIS(oldThere, oldM, oldHere + float(oldM) * oldThere));
                if (RestirStream(r.weightSum, weight, RestirRandom(index, sampleIndex, 241u))) {
                    r.sample = old.sample;
                    r.visibility = oldVisibilityHere;
                    selectedTarget = oldHere;
                }
                r.M += oldM;
            }
        }
    }
    r.W = RestirReuseFinalize(r.weightSum, selectedTarget);
    output[index] = r;
}
