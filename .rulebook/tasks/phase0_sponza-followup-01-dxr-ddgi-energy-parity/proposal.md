# Proposal: phase0_sponza-followup-01-dxr-ddgi-energy-parity

## Why

`run_sponza_dxr.bat` (`r.RayTracing=1 r.RayTracing.GI=1`) now runs cleanly on the RTX 5080
(Tier 1.2): `PipelineLibrary: created DDGI_ProbeUpdate pipeline`, `DDGI atlas ready`,
`TLASManager: Initialized`, exit 0, no Error lines (2026-09-08, task 06 item 3.3). SponzaTest never
validated this path (0x80070057 on a Tier 1.0 GPU), and the image shows why it matters: Camera A at
frame 300 is **77.1/255 mean, 1.0 % pixels below 16** with DXR versus **52.0 / 36.5 %** with the SDF
radiance cache (`.screenshots/sponza-06-dxr-f300.png` vs `sponza-06-cameraA-f640.png`,
`sponza-06-paridade.jpg`). Shadows flatten, the ceiling turns warm, the whole atrium is lifted —
the DDGI probe path carries roughly 1.5× the indirect energy of the SDF path that was just
calibrated against the reference branch.

Known facts to start from:
- `frame_gi_passes.cpp` (`RadianceCache_Trace` pass): "the SDF path always runs" and the DDGI atlas
  writer runs in addition when `r.RayTracing.GI` is on and a TLAS is ready
  (`m_ddgiSystem.DispatchUpdate(...)`, gated by `HasCompletedFirstUpdate()` for sampling).
- Both writers share `RadianceCacheTraceConstants` (192 bytes: 176 + the VFX emissive tail).
- The SH packing constants embed Lambert 1/π (task 03); the octahedral atlas stores irradiance —
  if the consumer applies the diffuse BRDF to both, or the deferred shader **adds** the DDGI sample
  on top of the SH sample instead of replacing it, the energy doubles.
- The DXR update has no first-frame hysteresis (`m_firstUpdateComplete` only gates sampling).

## What Changes

Diagnose, then fix, the DDGI irradiance scale so `r.RayTracing.GI=1` lands within ±10 % of the SDF
path's mean luminance at Camera A / Camera C with the same scene CVars. Instrumentation
(`gi.ddgi_irradiance` diag target), a GPU harness that compares probe irradiance in the enclosed-box
fixture under both writers, and the shader/consumer fix. No change to the SDF path's calibration.

## Impact
- Affected specs: none
- Affected code: `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`,
  `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` (DDGI sample combination),
  `runtime/engine/renderer/frame/frame_gi_passes.cpp` / `frame_lighting.cpp` (diag target, gating),
  `scripts/test_ddgi_energy.ps1` (new)
- Breaking change: NO (DXR GI is opt-in)
- User benefit: the ray-traced GI path becomes usable instead of a 1.5× brighter alternative
