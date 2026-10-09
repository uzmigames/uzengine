# Design: the sky capture's sun illuminance

## Before
`RegisterAtmospherePasses` computes `skySunColor = skyComp.sunIlluminance × lightTint` and hands the
sky pass `skyCB.sunIlluminance = skySunColor` (per channel, `L += throughput · Sint · sunIlluminance.xyz`
in `sky_atmosphere_ps.hlsl`). The two capture paths got `sunIntensity = skySunColor.Length()`, a scalar
the capture shader multiplies into the per-channel scattering (`L += T · sunT · Sint · sunIntensity`,
`gRad = albedo / π · NdotL · sunIntensity · gSunT`). For (10, 10, 10) the length is 17.32: the capture is
√3 × the sky pass; for a tinted sun the capture is grey.

## After
- `sky_cubemap_cs.hlsl`: `cbuffer SkyCubemapParams` carries `sunIllumR, sunIllumG, sunIllumB` in place
  of `sunIntensity` (12 DWORDs); `float3 sunIllum` multiplies the in-scatter and the ground term per
  channel. The pixel-shader variant the same (10 DWORDs).
- `SkyCubemapCaptureRootConstants` / `SkyCubemapCapturePSRootConstants`: the three floats, the
  static_asserts and the `SetRootConstants` counts follow.
- `atmosphere_renderer.cpp`: `cubeRC.sunIllumX/Y/Z = skySunColor.x/y/z` (both paths). Nothing else
  changes: the re-bake signature (block A.5) already keys on `skySunColor`.
- Consumers unchanged: the IBL bake, the SH projection, the sampler and the GI probes read the cubemap
  as before and see the corrected level.

## Harness
`scripts/test_sky_capture.py`: S1 the dumped environment (block B.5b's diagnostics, cubemap space)
against the mirrored view's sky pixels along the view rays; S2 block B.5b's E1 on the chrome floor; S3
the warm-sun variant's sky pixels per channel; S4 Sponza Camera C at frame 120 against block B.5b's
capture of the same binary before this change (direct identical, indirect ratio, colour statistics,
montage); S5 the PMREM floor bit-identical. `--before` measures S1-S3 on the binary before the item
(the warm scene captured, the rest reused from block B.5b).
