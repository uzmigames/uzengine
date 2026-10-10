# Proposal: phase16_vfx-lumen-gi-hardware-validation

## Why

`phase16_vfx-lumen-gi-contribution` shipped the CPU surface of the
emissive-particle DDGI feed: `VfxDDGIBuildSampleForEmitter` helper,
`SceneRenderer::GatherAndUploadVfxEmissiveSamples` (called inline
after every simulation tick), `m_vfxDDGIEmissiveFeed` member
initialisation alongside the DDGI atlases, two new CVars
(`r.VFX.DDGIInjection`, `r.VFX.DDGIInjection.MaxEmitters`), and 11
unit tests. The buffer is allocated, populated, and uploaded every
frame.

What is NOT yet wired — and what this task owns:

1. The DDGI probe-update CS (DXR variant) does NOT yet read
   `m_vfxDDGIEmissiveFeed.GetBufferSrvIndex()` / `GetSampleCount()`
   when it accumulates probe SH. Wiring needs the probe-update CB
   layout extension + the in-shader accumulation loop.
2. No visual A/B exists yet to confirm the bounce light is correct.
3. No reference comparison against Falcor / Lumen — the proposal's
   risk note flags that approach 1 (virtual lights) may be too coarse
   for tight localized glows.

## What Changes

### DDGI probe-update CS integration

- Extend the DDGI probe-update CB with `vfxEmissiveSrvIndex` (uint) +
  `vfxEmissiveSampleCount` (uint).
- `SceneRenderer` fills both fields from `m_vfxDDGIEmissiveFeed` once
  per frame.
- The probe-update CS adds a loop after the existing real-light
  contribution: for each sample, compute Karis attenuation with
  `(probePos - sample.position)` distance and `sample.influenceRadius`,
  multiply by `sample.emissive`, accumulate into the probe's SH
  irradiance.
- Mirror the math against `VfxDDGIAttenuation` /
  `VfxDDGIProbeContribution` so the shader and CPU helpers stay in
  lock-step.

### Visual A/B + Falcor reference

- Capture Sponza atrium with a fireball emitter (intensity=15,
  radius=200 cm, alive count = 5000):
  - Frame N with `r.VFX.DDGIInjection = 0` (off).
  - Frame N+10 with `r.VFX.DDGIInjection = 1` (on).
- Save both as PNG in `.screenshots/`. View via the Read tool to
  confirm the on-frame shows visible warm bounce light on the
  atrium walls.
- Compare the on-frame against a Falcor render of the same scene
  with equivalent emissive virtual lights. PSNR target 35 dB. If
  the gap is > 5 dB, implement approach 2 (per-probe cube map
  rasterisation) as a secondary CVar option
  `r.VFX.DDGIInjection.Method` (0 = virtual lights, 1 = cube map).

### Tests

- `test_vfx_ddgi_probe_accumulates_emissive_samples.cpp` — render a
  single-probe DDGI grid with one emissive sample 50 cm away;
  assert the probe's SH band-0 irradiance matches the analytical
  Karis attenuation × emissive within 1e-3.
- `test_vfx_ddgi_disabled_path_zero_contribution.cpp` — set
  `r.VFX.DDGIInjection = 0`, render two frames with a bright emitter,
  assert probe SH irradiance is byte-identical to a pure-real-light
  render.

## Impact

- Affected specs: DDGI probe-update CB (existing).
- Affected code:
  - DDGI probe-update CS HLSL — add accumulation loop.
  - DDGI per-frame CB struct (CPU + HLSL) — append two uint fields.
  - `SceneRenderer::FillDdgiPerFrameCB` (or equivalent) — populate
    the new fields from `m_vfxDDGIEmissiveFeed`.
  - New: `runtime/tests/engine/vfx/test_vfx_ddgi_probe_accumulates_emissive_samples.cpp`,
    `test_vfx_ddgi_disabled_path_zero_contribution.cpp`.
  - `docs/engine/25-vfx/40-ddgi-emissive-injection.md` — visual A/B
    section with before / after screenshots.
- Breaking change: NO. CB extensions append fields; defaults preserve
  current behaviour when the feed is uninitialised.
- User benefit: explosions and magic effects finally light the scene
  via DDGI bounce — a fireball under Sponza's roof warms the walls.

## Dependencies

- Builds on `phase16_vfx-lumen-gi-contribution` (already archived).
- Requires DX12-capable host with DXR probe-update CS support.
