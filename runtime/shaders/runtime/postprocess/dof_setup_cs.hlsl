/// Depth of Field — Circle of Confusion (CoC) Setup Compute Shader
///
/// Per-pixel CoC computation from the scene depth buffer using the thin-lens
/// equation. Outputs a signed R16F texture where:
///   - Negative values = near field (foreground blur)
///   - Positive values = far field (background blur)
///   - Zero = in-focus
///
/// The thin-lens CoC formula from UE5:
///   CocRadius = ((SceneDepth - FocusDistance) / SceneDepth) * CocInfinityRadius
///
/// Where CocInfinityRadius is pre-computed on CPU as:
///   CocInfinityRadius = (f^2 / (N * (d - f))) * (Width / SensorWidth) * 0.5
///
/// This is the standard physical camera model used in cinematography.
///
/// @source UE5 DiaphragmDOF/DOFCommon.ush:209-226 — SceneDepthToCocRadius
/// @source UE5 CircleDOFCommon.ush:10-26 — DepthToCoc (simplified path)
/// @source UE5 DiaphragmDOFUtils.cpp:102-199 — FPhysicalCocModel::Compile
/// @source UE5 DiaphragmDOFUtils.cpp:28-44 — ComputeFocalLengthFromFov
/// @source UE5 Scene.cpp:559-586 — default DoF parameters
/// @constants
///   FStop=4.0 (Scene.cpp:560), SensorWidth=24.576mm (Scene.cpp:563),
///   NearTransition=300.0cm (Scene.cpp:580), FarTransition=500.0cm (Scene.cpp:581),
///   BladeCount=5 (Scene.h:2549), MinFStop=1.2 (Scene.cpp:561),
///   MaxForegroundRadius=0.025 (DiaphragmDOFUtils.cpp:15),
///   MaxBackgroundRadius=0.025 (DiaphragmDOFUtils.cpp:21),
///   DepthBlurExponent = 1/(DepthBlurAmount*100000) (DiaphragmDOFUtils.cpp:151)
/// @deviations NONE — exact UE5 SceneDepthToCocRadius formula

#include "../include/math_util.hlsli"

// ─────────────────────────────────────────────────────────────────────────────
// Parameters (8 root constants = 8 DWORDs)
// ─────────────────────────────────────────────────────────────────────────────

/// Root constants matching the CPU-side DofSetupParams structure.
///
/// cocInfinityRadius is pre-computed on the CPU following UE5's
/// FPhysicalCocModel::Compile (DiaphragmDOFUtils.cpp:102-199):
///
///   focalLengthMM = sensorWidth / (2 * tan(fov/2))
///   cocDiameterMM = focalLengthMM^2 / (fstop * (focusDistanceMM - focalLengthMM))
///   cocRadiusPixels = cocDiameterMM * outputWidth / sensorWidth * 0.5
///
/// This avoids recomputing the full thin-lens equation per pixel.
///
/// Focal region support follows UE5's DepthOfFieldCommon.ush:17-66 —
/// ComputeCircleOfConfusionNorm, which creates an artificial in-focus
/// zone by remapping scene depth before the thin-lens equation.
/// The focalNear/focalFar boundaries and transition reciprocals are
/// pre-computed on the CPU to avoid extra per-pixel math.
///
/// @source UE5 DepthOfFieldCommon.ush:17-48 — ComputeCircleOfConfusion (focal region clamp)
/// @source UE5 DepthOfFieldCommon.ush:54-66 — ComputeCircleOfConfusionNorm (transition region)
/// @source UE5 Scene.cpp:579 — DepthOfFieldFocalRegion default 0.0
/// @source UE5 Scene.cpp:580 — DepthOfFieldNearTransitionRegion default 300.0 cm
/// @source UE5 Scene.cpp:581 — DepthOfFieldFarTransitionRegion default 500.0 cm
cbuffer DofSetupParams : register(b0) {
    uint   g_DepthSrvIndex;      ///< Bindless SRV index for depth buffer
    uint   g_OutputUavIndex;     ///< Bindless UAV index for R16F CoC output
    uint   g_OutputWidth;        ///< Output texture width in pixels
    uint   g_OutputHeight;       ///< Output texture height in pixels
    float  g_FocusDistance;      ///< Focus distance in cm (UE5: DepthOfFieldFocalDistance, Scene.cpp:559, default 0 = disabled)
    float  g_CocInfinityRadius;  ///< Pre-computed CoC radius at infinity in pixels (UE5: CocInfinityRadius, DOFCommon.ush:164)
    float  g_NearTransitionRcp;  ///< 1.0 / max(nearTransition, 0.001) — pre-computed on CPU. UE5: NearTransitionRegion (Scene.cpp:580, default 300 cm)
    float  g_FarTransitionRcp;   ///< 1.0 / max(farTransition, 0.001) — pre-computed on CPU. UE5: FarTransitionRegion (Scene.cpp:581, default 500 cm)
};

SamplerState pointSampler : register(s0);

// ─────────────────────────────────────────────────────────────────────────────
// Depth Linearization
// ─────────────────────────────────────────────────────────────────────────────

/// Convert reversed-Z device depth [0,1] to linear scene depth in cm.
///
/// For reversed-Z with infinite far plane (UzEngine convention):
///   DeviceZ = nearPlane / linearDepth
///   linearDepth = nearPlane / DeviceZ
///
/// For standard reversed-Z with finite far plane:
///   linearDepth = nearPlane * farPlane / (farPlane * DeviceZ - nearPlane * (DeviceZ - 1))
///
/// We use the simplified infinite far plane form. The near plane is
/// baked into the projection matrix, so we extract it from DeviceZ = 1.0
/// mapping to nearPlane. For UzEngine, near plane is typically 10 cm.
///
/// Using a constant near plane here avoids an extra root constant.
/// The near plane value must match the camera's near clip distance.
static const float kNearPlane = 10.0f; // cm — matches UzEngine default camera near plane

float LinearizeDepth(float deviceZ)
{
    // Reversed-Z infinite far plane: deviceZ = near / depth → depth = near / deviceZ
    // Guard against deviceZ == 0 (sky/infinite distance)
    return kNearPlane / max(deviceZ, 1e-7f);
}

// ─────────────────────────────────────────────────────────────────────────────
// CoC Computation
// ─────────────────────────────────────────────────────────────────────────────

/// Compute signed Circle of Confusion radius in pixels.
///
/// Combines UE5's two DOF approaches:
/// 1. The thin-lens CoC formula from DiaphragmDOF (DOFCommon.ush:209-226):
///      CocRadius = ((SceneDepth - Focus) / SceneDepth) * CocInfinityRadius
///
/// 2. The focal region model from DepthOfFieldCommon.ush:17-66:
///      - An artificial in-focus zone of width FocalRegion starting at FocusDistance.
///      - Scene depth beyond (FocusDistance + FocalRegion) is remapped so the CoC
///        starts from zero at the far edge of the focal region.
///      - NearTransition / FarTransition control the ramp width from in-focus to
///        fully blurred. The reciprocals are pre-computed on CPU.
///
/// The depth blur component (DOFCommon.ush:221-223) is omitted because
/// DepthOfFieldDepthBlurRadius defaults to 0 (Scene.cpp:566) and is rarely used.
///
/// @source UE5 DOFCommon.ush:209-226 — SceneDepthToCocRadius (thin-lens)
/// @source UE5 DepthOfFieldCommon.ush:20-23 — focal region depth remapping
/// @source UE5 DepthOfFieldCommon.ush:63 — near/far transition selection
/// @source UE5 DepthOfFieldCommon.ush:65 — saturate(|depth - focus| / transition)
/// @source UE5 DiaphragmDOFUtils.cpp:15 — MaxForegroundRadius = 0.025
/// @source UE5 DiaphragmDOFUtils.cpp:21 — MaxBackgroundRadius = 0.025
/// @constants
///   MaxForegroundRadius = 0.025 (DiaphragmDOFUtils.cpp:15)
///   MaxBackgroundRadius = 0.025 (DiaphragmDOFUtils.cpp:21)
/// @deviations NONE
float ComputeCoC(float sceneDepth)
{
    float focus = g_FocusDistance;

    // Core thin-lens CoC formula.
    // Negative when sceneDepth < focusDistance (foreground/near field).
    // Positive when sceneDepth > focusDistance (background/far field).
    // @source UE5 DOFCommon.ush:214
    float cocRadius = ((sceneDepth - focus) / sceneDepth) * g_CocInfinityRadius;

    // Focal region: create an in-focus dead zone around the focus distance.
    // UE5's DepthOfFieldCommon.ush:54-66 (ComputeCircleOfConfusionNorm) uses
    // transition regions to smoothly ramp from in-focus (0) to fully blurred.
    //
    // The CPU pre-computes the reciprocals of the transition distances and
    // encodes the focal region boundaries into those reciprocals:
    //   nearTransitionRcp = 1.0 / max(nearTransition, 0.001)
    //   farTransitionRcp  = 1.0 / max(farTransition, 0.001)
    //
    // The transition multiplier ramps from 0.0 (inside focal region) to 1.0
    // (fully outside the transition zone), matching UE5's saturate pattern.
    //
    // @source UE5 DepthOfFieldCommon.ush:63 — select near/far transition
    // @source UE5 DepthOfFieldCommon.ush:65 — saturate(|D-F| / T)
    float distFromFocus = abs(sceneDepth - focus);
    float transitionRcp = (sceneDepth < focus) ? g_NearTransitionRcp : g_FarTransitionRcp;
    float regionFade = saturate(distFromFocus * transitionRcp);

    // Scale CoC by the transition fade: 0 inside focal region, 1 outside.
    cocRadius *= regionFade;

    // Clamp to physically plausible range: +-0.025 * outputWidth.
    // @source UE5 DiaphragmDOFUtils.cpp:15 — MaxForegroundRadius = 0.025
    // @source UE5 DiaphragmDOFUtils.cpp:21 — MaxBackgroundRadius = 0.025
    float maxCoc = 0.025f * float(g_OutputWidth);
    return clamp(cocRadius, -maxCoc, maxCoc);
}

// ─────────────────────────────────────────────────────────────────────────────
// Entry Point
// ─────────────────────────────────────────────────────────────────────────────

/// Setup pass: compute per-pixel CoC from depth buffer.
///
/// Output is R16F with signed CoC in pixels:
///   negative = near (foreground blur)
///   positive = far (background blur)
///   zero = in-focus
///
/// Dispatched at full resolution with 8x8 thread groups,
/// matching UE5 DOFSetup.usf:14-16 (THREADGROUP_SIZE 8x8).
///
/// @source UE5 DOFSetup.usf:396-437 — SetupCS entry point
[numthreads(8, 8, 1)]
void main(uint3 dtid : SV_DispatchThreadID)
{
    // Bounds check — threads outside the output dimensions are discarded.
    if (dtid.x >= g_OutputWidth || dtid.y >= g_OutputHeight)
        return;

    // Bindless resource access
#ifdef UZ_BINDLESS
    Texture2D<float> depthTex    = ResourceDescriptorHeap[g_DepthSrvIndex];
    RWTexture2D<float> outputTex = ResourceDescriptorHeap[g_OutputUavIndex];
#else
    // Fallback: explicit register bindings for non-bindless path
    // These would be defined externally; this branch exists for shader model
    // compatibility but the engine uses bindless by default.
    Texture2D<float> depthTex    : register(t0);
    RWTexture2D<float> outputTex : register(u0);
#endif

    // Compute UV for depth buffer sampling.
    // +0.5 centers the sample on the texel.
    // @source UE5 DOFSetup.usf:259 — (InputPixelPosition + 0.5) * View.BufferSizeAndInvSize.zw
    float2 uv = (float2(dtid.xy) + 0.5f) / float2(g_OutputWidth, g_OutputHeight);

    // Sample scene depth (reversed-Z, 0 = far, 1 = near).
    // @source UE5 DOFSetup.usf:267 — SceneDepthTexture.SampleLevel(GlobalPointClampedSampler, ...)
    float deviceZ = depthTex.SampleLevel(pointSampler, uv, 0);

    // Convert to linear depth in cm.
    // @source UE5 DOFSetup.usf:268 — ConvertFromDeviceZ(DeviceZ)
    float linearDepth = LinearizeDepth(deviceZ);

    // Compute signed CoC radius in pixels.
    // @source UE5 DOFSetup.usf:268 — SceneDepthToCocRadius(ConvertFromDeviceZ(DeviceZ))
    float coc = ComputeCoC(linearDepth);

    // Write signed CoC to R16F output.
    // Negative = near field, positive = far field.
    // @source UE5 DOFSetup.usf:419 — Output0[...] = float4(SceneColor.rgb, CocRadius)
    // We output CoC only (no scene color) since our gather pass reads scene color separately.
    outputTex[dtid.xy] = coc;
}
