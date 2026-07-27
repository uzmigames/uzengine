# Lumen-Lite GI Correction Plan — Sponza Realism

**Date**: 2026-07-26
**Goal (user brief)**: "Iluminação realista o mais próxima do Lumen possível, sem
copiar código da Unreal." Architecture and ideas from published material only;
all code original, cited via `@ref` to papers/talks (per
`.claude/rules/ue5-component-parity.md` IP rules).
**Reference image**: Intel Sponza base scene render
(`.screenshots/gi-plan/ref-intel-sponza.jpg`) — bright interior filled by
bounced skylight, neutral stone, soft contact AO, readable shadows.
**Baseline captured**: `.screenshots/gi-plan/00-baseline.png` (2026-07-26,
`--frames 140`, default scene CVars).

Supersedes nothing — this is the execution plan that sequences the designs in
[gap-analysis.md](./gap-analysis.md) and
[aaa-default-pipeline.md](./aaa-default-pipeline.md) against the **verified
current state** below.

---

## 1 · Verified current state (evidence, 2026-07-26)

### 1.1 What actually runs on Sponza today — measured, not guessed

`uzengine.exe --project sample/projects/sponza.uzproject --diag --frames 120`
returned **21 targets** and the list contains **no `ssgi.*`, no `gtao.*`, no
`ddgi.*`, no `rc.*`, no `ssr.*` entries at all**:

| Finding | Evidence |
|---|---|
| SSGI **not running** | `r.SSGI` CVar default `false` (`ssgi_renderer.cpp:22`); scene only sets `r.SSGI.Intensity=1.5`, never `r.SSGI=1`; no `ssgi.*` diag target. |
| GTAO **not running** | `r.GTAO` off unless a quality preset ≥2 is applied; `quality` CVar defaults to `-1` (custom → no preset applied); no `gtao.*` diag target. |
| RadianceCache (SDF GI) **off** | `r.RadianceCache` default `false` (`radiance_cache.cpp:24`); presets enable it only at Ultra/Cinematic. |
| DDGI (DXR probes) **off** | `r.RayTracing.GI` default `false` (`frame_gi_passes.cpp:46`). |
| Brixelizer GI **off** | `r.BrixelizerGI` default `false`; explicitly disabled in merge `b7dafaf8`. |
| LightProbeVolume **absent** | `sponza.uzscene` has no `light_probe_volume` entity (the `InteriorProbeVolume` added in phase0_indoor-lighting-system task 2.8 is no longer in the scene). |
| Indirect that DOES run | Sky-SH ambient × `skyVisibility = lerp(0.10, 1.0, pow(sunShadow, 0.55))` hack + `skyLightMinAmbient` floor (`deferred_lighting_ps.hlsl:676-758`). |
| `gbuffer.velocity` all zero | Static camera → expected zero, but must be re-verified under motion before any temporal GI denoise work. |
| `taa.history` WARN_ZERO | Temporal accumulation path not active in this capture — verify TAA/DLSS state before Phase 4 relies on reprojection. |
| `gbuffer.ao` diag stats identical to `gbuffer.albedo` | Diag target aliasing bug — readback maps the wrong resource/channel. |
| D3D12MA assertion on exit | "Some allocations were not freed before destruction" — shutdown leak, pollutes every headless capture. |

**Conclusion**: Sponza's "GI" today is a spatially-constant tinted ambient with
a shadow-correlated dimming hack. There is **zero bounce light, zero contact
occlusion, zero spatial variation**. That is exactly what the baseline
screenshot shows vs the Intel reference: dead uniform gray shadows, a pasted-on
sun strip that spills no light onto neighboring surfaces, and vaults lit flat.

### 1.2 The physics bug inside the world-space GI (when it IS enabled)

Both probe tracers return, at ray hit points
(`ddgi_probe_update_cs.hlsl:184-195`, `radiance_cache_trace_cs.hlsl:268-282`):

```hlsl
float3 N   = -dir;                         // fake normal = negated ray dir
float NdL  = max(dot(N, L), 0.0);
radiance   = lightColor * intensity * NdL  // sun with NO shadow test
           * groundAlbedo;                 // one constant for the whole scene
```

Four defects, ranked:

1. **No shadow test at the hit** — every hit surface "sees" the sun at full
   intensity. Probes inside enclosed spaces inject sunlight that cannot
   physically reach them → light leaks and wrong overall level.
2. **Fake normal** (`-dir`) — radiance depends on ray direction, not surface
   orientation; energy is directionally wrong everywhere.
3. **Constant `groundAlbedo`** — no red-curtain red bounce, no warm-stone warm
   bounce. Bounce is achromatic by construction. The Intel reference look is
   IMPOSSIBLE with a constant albedo.
4. **No multi-bounce** — hit radiance never includes prior-frame indirect, so
   interiors that need 2+ bounces stay dark even with everything on.

Lumen's core insight (Narkowicz & Wright, GDC 2022) is precisely that the hit
must return **cached LIT surface radiance** — "the cost of GI is not the rays,
it is the material evaluation at the hit". Our tracers return a constant.
This is the #1 gap between UzEngine and a Lumen-class result.

### 1.3 What is already correct (do not touch)

- Calibration fixes from `sponza-lighting-rootcause.md` are **already in code**:
  sun `intensity=10`, `sky_light.light_color=[1,1,1]`, skyTint double-multiply
  fixed (`deferred_lighting_ps.hlsl:420-422` — diffuse gets color only,
  specular gets color×intensity), dead tonemap CVars removed from the scene,
  `r.Exposure=2.0`. **The rulebook task `phase1_fix-sponza-lighting-calibration`
  (0/11 checked) was implemented but never updated — housekeeping, not work.**
- GBuffer, BRDF (Frostbite diffuse + GGX + multi-scatter Fdez-Aguera 2019),
  CSM pipeline, atmosphere/sky LUT, path tracer (progressive accumulation,
  VNDF sampling, per-bounce sun shadow rays — usable as ground truth today).
- The composite priority rule ("pick ONE GI source, never sum" —
  `deferred_lighting_ps.hlsl:425-440`) is the right frame; the sources feeding
  it are what's broken/off.

---

## 2 · Lumen architecture → UzEngine mapping (original implementation)

Lumen's four cache layers (see [../lumen/00-architecture.md](../lumen/00-architecture.md))
mapped onto what UzEngine already owns:

| Lumen layer | Purpose | UzEngine equivalent | Status |
|---|---|---|---|
| Screen traces | Near-field rays vs the lit screen | **SSGI** (samples prev-frame lit HDR — already multi-bounce by construction) | exists, OFF |
| L1 Screen probes + final gather | 16×16 adaptive probes, 32 rays, octahedral, temporal | **NEW pass** (Phase 4) | missing |
| L2 Radiance cache (world probes) | Distant radiance at sparse world probes | `RadianceCache` (GDF sphere-trace, L2 SH, hysteresis, 1024/4096 probes/frame) + `DDGISystem` (DXR atlases, Chebyshev) | exists, OFF + hit-lighting broken |
| L3 Surface cache (lit hit material) | Ray hit → cached lit radiance tap | **Surface-cache-LITE** (Phase 2): per-instance material table + GDF-gradient normals + shadowed sun at hit + prev-frame probe feedback + screen-radiance reuse | missing (the core fix) |
| L4 Voxel lighting (multi-bounce reservoir) | Scene-wide feedback | Prev-frame probe grid AS the feedback source (Majercik 2019 "infinite bounces") | missing, cheap once L3-lite lands |
| SDF tracing backend | SW ray tracing | `GlobalDistanceField` clipmap + `mesh_sdf_baker` | exists |
| HWRT backend | RT cores | TLAS/BLAS + `RayQuery` (DXR 1.1) proven working | exists |
| Ground truth | Validation | In-engine path tracer + `--diff`/FLIP + Falcor (PSNR ≥ 35 dB project rule) | exists |

Full card-atlas surface cache (Lumen L3 proper) is **deliberately deferred**:
`01-surface-cache.md §7` estimates 3+ months; the LITE version above delivers
the visual behaviors that matter for Sponza-class scenes (colored, shadowed,
multi-bounce hits) for ~5% of the effort. Revisit cards only if Phase 4
quality plateaus below target.

References for original implementation (cite these in shaders, never UE code):
Majercik et al. 2019 "Dynamic Diffuse Global Illumination with Ray-Traced
Irradiance Fields" (+ 2021 update); McGuire et al. 2017 "Real-Time Global
Illumination using Precomputed Light Field Probes"; Wright & Narkowicz GDC 2022
(architecture concepts); Ramamoorthi & Hanrahan 2001 (SH irradiance); Heitz
2018 (VNDF); Fdez-Aguera 2019 (multi-scatter); Jimenez et al. 2016 (GTAO);
Schied 2017 SVGF / NVIDIA ReBLUR docs (denoise concepts).

---

## 3 · Execution phases

Every phase: CVar-gated, additive, fix-forward, and closed with the
verification block listed. No phase starts before the previous one's exit
criteria pass. Follow `.claude/rules/rendering-diagnostics.md` order
(diagnose → visualize → edit → 1 rebuild).

### Phase 0 — Reactivate the stack + honest baseline (1–2 days)

1. Scene/config: enable `r.SSGI=1` (intensity back to ~1.0), `r.GTAO=1` for
   Sponza; decide default-on story (scene CVars now; engine defaults after
   Phase 3 proves stable).
2. Diag hygiene — make "off" visible, not invisible: register `ssgi.*`,
   `gtao.*`, `rc.*`, `ddgi.*` diag targets unconditionally with status
   `SKIPPED` when their pass is disabled. (A missing target hid the entire
   problem for months.)
3. Fix the `gbuffer.ao` diag aliasing (stats currently duplicate albedo).
4. Fix the D3D12MA shutdown assertion (allocations not freed) so headless
   captures exit clean.
5. Verify motion vectors + TAA history under camera motion (prerequisite for
   all temporal GI work; `taa.history` was WARN_ZERO in baseline).
6. Housekeeping: check off + archive `phase1_fix-sponza-lighting-calibration`
   via rulebook tools (items verified in code, §1.3).

**Verify**: `--diag` shows ssgi/gtao OK + others SKIPPED; A/B screenshot vs
`00-baseline.png` shows SSGI bounce near lit strip + GTAO contact darkening;
no shutdown assertion; screenshots at 3 camera positions.

### Phase 1 — Ground-truth harness (1 day)

1. Path-traced goldens: `r.PathTracer.Enable=1`, static camera, ≥1024 accum
   frames, 3 poses (atrium ground, arcade shadow, upper gallery) →
   `.screenshots/gi-plan/golden-*.png`.
2. Document the loop: raster capture same poses → `--diff` / FLIP → PSNR
   table appended to this file per phase. (Falcor cross-check per project rule
   when the number matters.)
3. Record Phase-0 PSNR as the tracked baseline number.

**Verify**: goldens visually sane (no fireflies/NaNs), PSNR harness produces a
number, table started.

### Phase 2 — Surface-cache-lite: fix hit lighting (the core, 3–5 days)

Applies to BOTH `radiance_cache_trace_cs.hlsl` (GDF path) and
`ddgi_probe_update_cs.hlsl` (DXR path); keep the two shaders' schemes
identical (they already share packing/spreading conventions).

1. **Real normals**: GDF central-difference gradient (6 taps) at hit for the
   SDF path; geometric triangle normal via `RayQuery` candidate data for DXR.
2. **Shadowed sun at hit**: occlusion ray toward the sun — GDF sphere-trace
   (SDF path) / second `RayQuery` (DXR path). Kills the interior sun-injection
   leak.
3. **Per-instance albedo/emissive**: average material color table
   (StructuredBuffer indexed by instance/mesh id) built at load from material
   constants + albedo texture mip-tail average. DXR: `InstanceID()` → row.
   GDF: nearest-mesh id if available from the composite; otherwise start with
   a scene-average constant and log the gap — DXR path is the quality path.
4. **Multi-bounce feedback**: at hit, sample the **previous frame's** probe
   grid (same SH fetch the deferred PS uses) × hit albedo and add to hit
   radiance (Majercik 2019 §"infinite bounces"; Lumen radiosity concept).
5. **Screen-radiance reuse (stretch)**: if hit reprojects into prev-frame
   screen with depth agreement, take prev lit HDR instead of 1–4 (Lumen
   screen-trace shortcut; SSGI already proves the sampling pattern).
6. Turn `r.RadianceCache=1` (SDF) on for Sponza; `r.RayTracing.GI=1` where DXR
   available (RTX 4090 dev box: yes).

**Verify**: `--vis` probe atlas shows colored, shadow-aware radiance (warm
floor bounce, dark under-arch probes); `--pixel-debug` on an enclosed pixel
shows no direct-sun-tinted energy; PSNR step-change recorded; leak A/B vs
Phase 0.

### Phase 3 — Composite correctness (2–3 days)

1. Fallback cascade per pixel (Lumen order): SSGI (screen validity mask) →
   world probes (RC/DDGI) → sky SH — **blend by validity, never sum** the
   diffuse sources (SSGI is currently ADDED on top of the probe/sky result —
   double count, `deferred_lighting_ps.hlsl:913-927`).
2. Stop tinting SSGI/probe radiance by `skyTintDiffuse` (scene radiance is not
   sky radiance; white skylight hides this today).
3. Retire the `skyVisibility` sun-shadow hack to Low tier only once RC is
   default-on (it already self-disables when a GI source fires).
4. GTAO modulates the **unified** indirect diffuse (single application point).
5. Units audit pass across SSGI/RC/DDGI/sky irradiance (the rootcause history
   shows scale bugs love this seam); document the single convention in
   `cb-layouts.md`.

**Verify**: `--pass-dump` INDEX review of every stage; "indirect only" debug
view compare vs PT indirect-only; PSNR table update; 3-pose screenshots.

### Phase 4 — Screen-probe final gather, Lumen L1 (2–3 weeks)

Per [../lumen/03-screen-probe-gather.md](../lumen/03-screen-probe-gather.md)
§10 sketch — original implementation:

1. Uniform 16×16-pixel probe grid (skip adaptive subdivision v1).
2. 16–32 rays/probe, cosine + blue-noise rotation; octahedral 8×8 storage.
3. Trace cascade per ray: screen-space march (reuse SSGI marcher) → GDF/DXR
   world continuation → radiance-cache tap on far miss → sky.
4. Bilateral (depth/normal-aware) per-pixel gather from 4 neighbor probes.
5. Temporal accumulation with motion-vector reprojection + disocclusion
   rejection (velocity verified in Phase 0).
6. Feeds the same "indirect diffuse" slot via the Phase-3 cascade (screen
   probes become the preferred source above RC).

**Verify**: stability (no boiling) static cam 500 frames; disocclusion pan
test; PSNR ≥ 30 dB vs PT on Sponza sun-only pose; frame budget ≤ 3 ms for the
gather stack at 1440p on the 4090 (log `--gpu-profile`).

### Phase 5 — Specular + polish (1–2 weeks)

1. Stochastic SSR (GGX VNDF, Heitz 2018) + hit-distance output; RC/sky
   fallback on miss — replaces the deterministic march.
2. Specular occlusion coherent with the new indirect (bent-normal GTAO
   optional).
3. Re-tune Sponza scene values (exposure, skylight) against the Intel
   reference and PT goldens; lock as the calibration scene.

### Phase 6 — Later (explicitly out of current scope)

- DDGI default-on policy per HW tier; à-trous/SVGF-class denoiser upgrade for
  SSGI + probe gather; ReSTIR DI; VSM; full card-atlas surface cache ONLY if
  Phase 4 plateaus; Brixelizer stays off (redundant with RC+DDGI once fixed —
  decide keep/delete after Phase 4).

---

## 4 · Verification tooling (the feedback loop for every phase)

| Tool | Use |
|---|---|
| `--diag --frames 120` | Pass health: OK / WARN_ZERO / FAIL_NAN / SKIPPED per target, JSON on stdout. |
| `--vis <target>` + `--screenshot` | Fullscreen view of any registered target (probe atlases, ssgi.trace, hdr...). |
| `--pass-dump` | Every target as PNG + INDEX.html for stage-by-stage review. |
| `--pass-toggle <name>` | A/B a pass's visual contribution. |
| `--pixel-debug <x> <y>` | GPU printf of GBuffer + HDR at one pixel (leak forensics). |
| `--view-mode overview` / `--all-modes` | GBuffer sanity grids. |
| `r.PathTracer.Enable=1` | In-engine ground truth (progressive, VNDF, shadowed bounces). |
| `--diff a.png b.png` / FLIP | PSNR/SSIM gate per phase (target trajectory: baseline → ≥30 dB Phase 4 → ≥35 dB long-term vs Falcor per project rule). |
| `--gpu-profile` | Per-pass GPU ms for the budget gates. |
| CAVEATS (measured today) | `uzengine.exe --headless` is a device smoke-test that EXITS — do not combine with captures; use `--frames N --screenshot <abs path>`. Screenshot paths must be absolute. `--screenshot-stdout`/`DIAG_JSON` go to stdout — capture with shell redirection. |

## 5 · PSNR tracking table (fill per phase)

| Phase | Pose A (atrium) | Pose B (arcade shadow) | Pose C (gallery) | Notes |
|---|---|---|---|---|
| Baseline (0 GI) | — | — | — | capture pending Phase 1 harness |

## 6 · Risks

| Risk | Mitigation |
|---|---|
| GDF too coarse for per-instance albedo attribution | DXR path is the quality path on capable HW; SDF path may stay scene-average albedo (still shadowed + normal-correct). |
| Temporal foundation weak (velocity/TAA gaps) | Phase 0 gate 5 blocks Phase 4 until verified under motion. |
| Probe update budget on big scenes | Spreading already exists (1024/4096 per frame); keep; screen probes are fixed-cost by design. |
| Double-count regressions during composite refactor | "Indirect only" debug view + PT indirect-only A/B at every step. |
| Two probe shaders drift (SDF vs DXR) | Shared constants/packing already; add a shared .hlsli for the hit-lighting model in Phase 2. |
