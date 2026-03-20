# Proposal: phase0-3.3.0-ssgi-bleeding-fix

## Why

SSGI (Screen-Space Global Illumination) has two visible artifacts in the PBR demo:

1. **Sky bleeding onto ground plane** — White/bright patches appear below spheres where
   SSGI rays escape through geometry edges and hit the sky. The trace incorrectly samples
   sky luminance as indirect light contribution on the ground, creating bright halos that
   overlap shadows.

2. **Surface noise/grain on spheres** — The SSGI trace produces noisy/speckled patterns
   on sphere surfaces (most visible on smooth materials like quartzite and concrete). The
   spatial denoise pass doesn't fully remove this noise, causing visible flickering/shimmer
   at runtime.

Both issues are visible in pass dump screenshots (ssgi-trace, ssgi-denoised, ssgi-result).

## What Changes

### Fix 1: Sky rejection in SSGI trace
- Reject ray hits where depth == farPlane (sky pixels)
- Or clamp SSGI contribution from sky-hitting rays to ambient level
- Check if the ray march has a max-distance cutoff that's too large

### Fix 2: Improved spatial denoise
- Increase denoise kernel radius or iterations
- Add edge-aware filtering using normal/depth to preserve edges while smoothing noise
- Check temporal accumulation blend factor

## Impact
- Affected code: `runtime/shaders/runtime/ssgi/ssgi_trace_cs.hlsl`, `ssgi_spatial_denoise_cs.hlsl`
- Visual impact: Removes bright halos under objects, smoother indirect lighting
- No breaking changes
