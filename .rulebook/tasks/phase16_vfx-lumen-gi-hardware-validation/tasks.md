## 1. DDGI probe-update CS integration
- [ ] 1.1 Extend the DDGI probe-update CB (CPU mirror + HLSL cbuffer) with `vfxEmissiveSrvIndex` (uint) and `vfxEmissiveSampleCount` (uint); update the `static_assert(sizeof == ...)` to the new size
- [ ] 1.2 `SceneRenderer` populates both CB fields from `m_vfxDDGIEmissiveFeed.GetBufferSrvIndex()` / `GetSampleCount()` once per frame in the existing DDGI CB-fill site
- [ ] 1.3 Probe-update CS HLSL accumulation loop — for each sample, Karis attenuation `(probePos - sample.position)` × `sample.emissive`, add to probe SH band-0
- [ ] 1.4 Cross-check the shader math against `VfxDDGIAttenuation` / `VfxDDGIProbeContribution` so CPU helpers and shader stay in lock-step

## 2. Visual A/B + Falcor reference
- [ ] 2.1 Capture Sponza atrium with bright fireball emitter; save PNG with `r.VFX.DDGIInjection=0` and `=1`; view via Read tool to confirm warm bounce light on walls
- [ ] 2.2 Compare against Falcor reference render with equivalent emissive virtual lights; PSNR target 35 dB
- [ ] 2.3 If PSNR gap > 5 dB, implement approach 2 (per-probe cube map rasterisation) gated by new CVar `r.VFX.DDGIInjection.Method` (0 = virtual lights, 1 = cube map)

## 3. GPU integration tests
- [ ] 3.1 `test_vfx_ddgi_probe_accumulates_emissive_samples.cpp` — single-probe grid + one emissive sample 50 cm away; probe SH band-0 matches analytical Karis x emissive within 1e-3
- [ ] 3.2 `test_vfx_ddgi_disabled_path_zero_contribution.cpp` — `r.VFX.DDGIInjection=0` produces probe SH byte-identical to pure-real-light render

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
