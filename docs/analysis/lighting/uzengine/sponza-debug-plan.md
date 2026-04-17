# Sponza Debug Plan — Pass-by-Pass

Concrete, sequential diagnostic plan to find out why Sponza looks flat
in UzEngine today, using only our built-in diagnostic tools. No
shader edits until diagnosis is complete.

**Precondition**: run from `e:/UzEngine/` with a build of `uzengine.exe`
and Sponza loaded as the active scene.

---

## 0 · Setup

```bash
# Make sure we start from a clean build
cd e:/UzEngine/runtime && zig build

# Create a working directory for this session
mkdir -p e:/UzEngine/.screenshots/sponza-debug
```

---

## 1 · Baseline reference capture

Capture the current state first so we have a before/after:

```bash
# Run with default settings, 60-frame warmup, capture frame 60
uzengine.exe --project path/to/sponza.uzproj --editor \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/00-baseline.png \
  --screenshot-frame 60 --frames 120
```

View with the Read tool. Note the overall darkness / brightness
distribution, the color of shadowed areas (should be warm, if we have
GI), and any obvious banding or discontinuities.

---

## 2 · Run --diag to identify broken passes

```bash
uzengine.exe --diag --frames 120 --project path/to/sponza.uzproj
```

Expected output:
```
DIAG_JSON:{
  "frame": 120,
  "targetCount": ~30,
  "hasFailure": false|true,
  "hasWarning": false|true,
  "targets": [
    {"name": "gbuffer.albedo",    "status": "OK",        "min":..., "max":..., "avg":...},
    {"name": "gbuffer.normal",    "status": "OK", ...},
    {"name": "gbuffer.depth",     "status": "OK", ...},
    {"name": "gbuffer.emissive",  "status": "OK", ...},
    {"name": "gbuffer.material",  "status": "OK", ...},
    {"name": "shadow.csm",        "status": "OK", ...},
    {"name": "shadow.atlas",      "status": "OK|WARN_ZERO", ...},   ← check this
    {"name": "ddgi.irradiance",   "status": "OK|WARN_ZERO", ...},   ← **critical**
    {"name": "ddgi.visibility",   "status": "OK|WARN_ZERO", ...},
    {"name": "ssgi.color",        "status": "OK", ...},
    {"name": "ssgi.temporal",     "status": "OK", ...},
    {"name": "ssr.color",         "status": "OK", ...},
    {"name": "gtao.mask",         "status": "OK", ...},
    {"name": "ibl.prefiltered",   "status": "OK", ...},
    {"name": "ibl.irradiance",    "status": "OK", ...},
    {"name": "hdr",                "status": "OK", ...}
  ]
}
```

### What each status means

- `OK` — target has valid data.
- `WARN_ZERO` — target is all zeros (pass not writing or writing zeros).
- `FAIL_NAN` / `FAIL_INF` — shader math bug.

### Expected first finding

Based on the gap analysis ([gap-analysis.md](./gap-analysis.md)), the
most likely failure is **`ddgi.irradiance` = WARN_ZERO**. This confirms
DDGI isn't populating.

If it's OK, the DDGI atlas has data — then the problem is downstream
(it's not being sampled).

---

## 3 · Visualize DDGI irradiance directly

```bash
uzengine.exe --project path/to/sponza.uzproj --editor \
  --vis ddgi.irradiance \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/01-ddgi-irrad.png \
  --screenshot-frame 60 --frames 120
```

View. Expected if DDGI is working: a colorful texture showing
hemispherical radiance samples. Expected if broken: black or
near-black.

---

## 4 · Dump all passes as PNGs

```bash
uzengine.exe --project path/to/sponza.uzproj --editor \
  --pass-dump --frames 120
```

This generates `.screenshots/pass-dump-f120/INDEX.html` with PNGs of
every render target. Open the INDEX in browser (or read the PNGs
directly).

Review:
- `gbuffer.*` — all should be meaningful (albedo = textures, normal =
  normal map, depth = depth, emissive = blinking lanterns if any).
- `shadow.csm` — 4 cascade slices; cascade 0 near cam, cascade 3 far.
- `ddgi.irradiance` / `ddgi.visibility` — probe atlas (should have
  colored gradients).
- `ssgi.color` — per-pixel indirect diffuse from screen space.
- `ssr.color` — reflections.
- `ibl.prefiltered` — cubemap mip chain.
- `hdr` — the final lit HDR before tonemap.

For each image, note:
- Is it all black? → pass didn't run or wrote zeros.
- Is it the wrong resolution? → dispatch size bug.
- Does it match the other related passes (same depth, normal)? → correctness OK.

---

## 5 · Pixel-debug a specific Sponza pixel

Pick a pixel in a shadowed area (e.g., under a column where GI should
be warm):

```bash
uzengine.exe --project path/to/sponza.uzproj --editor \
  --pixel-debug --pixel-debug-x 960 --pixel-debug-y 700 \
  --frames 60
```

Expected output:
```
PIXEL_DEBUG_JSON:{"pixel":[960,700],"frame":59,"recordCount":13,"records":[
  {"id":0, "type":"float",  "value":0.00234},        // depth (reversed-Z)
  {"id":1, "type":"float",  "value":0.87},           // ao (GTAO)
  {"id":2, "type":"float",  "value":0.92},           // albedo R
  {"id":3, "type":"float",  "value":0.81},           // albedo G
  {"id":4, "type":"float",  "value":0.68},           // albedo B
  {"id":5, "type":"float",  "value":0.42},           // HDR color R
  {"id":6, "type":"float",  "value":0.35},           // HDR color G
  {"id":7, "type":"float",  "value":0.23},           // HDR color B
  {"id":8, "type":"float",  "value":0.5},            // roughness
  {"id":9, "type":"float",  "value":0.0},            // metallic
  {"id":10,"type":"float",  "value":0.0},            // emissive R
  {"id":11,"type":"float",  "value":0.0},            // emissive G
  {"id":12,"type":"float",  "value":0.0}             // emissive B
]}
```

Key diagnostic: compare HDR color to albedo.

- HDR ≈ albedo × (IBL constant) → indirect is IBL-only, DDGI not contributing.
- HDR ≈ albedo × (warm tinted light) → indirect has warm bounce, DDGI working.
- HDR << albedo → shadows might be oversuppressing.

Record the ratio HDR/albedo per channel. If R/R is similar to G/G is
similar to B/B, the indirect is achromatic (wrong). If R/R > G/G > B/B,
it's warm (correct for a sun-lit stone environment).

---

## 6 · Toggle individual passes

### 6.1 Disable SSGI to isolate DDGI

```bash
uzengine.exe --project path/to/sponza.uzproj --editor \
  --cvar r.SSGI.Enabled=0 \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/02-no-ssgi.png \
  --screenshot-frame 60 --frames 120
```

Compare to baseline. If image looks the same: SSGI was contributing
nothing (it's broken or disabled already). If image is darker: SSGI was
the main indirect source — meaning DDGI isn't active.

### 6.2 Disable DDGI

```bash
uzengine.exe --cvar r.DDGI.Enabled=0 \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/03-no-ddgi.png \
  --screenshot-frame 60 --frames 120
```

Same idea. If identical to baseline → DDGI wasn't doing anything.

### 6.3 Only IBL (both GI sources off)

```bash
uzengine.exe --cvar r.SSGI.Enabled=0 --cvar r.DDGI.Enabled=0 \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/04-ibl-only.png \
  --screenshot-frame 60 --frames 120
```

This is the "worst case" image: only IBL indirect, no bounce. Compare
to baseline — the difference between baseline and this capture is the
**actual contribution of SSGI + DDGI combined** in the current build.

If baseline and `04-ibl-only` look similar, SSGI + DDGI are contributing
nothing. This is the suspected state today.

### 6.4 Full daylight with direct-only

```bash
uzengine.exe --cvar r.GI.Mode=0 \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/05-direct-only.png \
  --screenshot-frame 60 --frames 120
```

Shows only direct lighting + shadows. Everything else black.

---

## 7 · Map the findings to the gap analysis

After steps 2–6, you should have answers to:

| Question | Expected answer | Action |
|---|---|---|
| Is DDGI atlas populated? | No (based on gap-analysis) | Go to step 8. |
| Is DDGI sampled in deferred lighting? | Yes, but multiplied by 0 or ~0 | Go to step 9. |
| Is SSGI denoise stable? | Maybe flickery at edges | Go to step 10. |
| Is PCSS light size correct? | Probably too small | Go to step 11. |
| Is IBL the only indirect right now? | Yes | Fix DDGI first. |

---

## 8 · Fix DDGI atlas (if empty)

Open `runtime/engine/renderer/gi/ddgi_system.cpp`. Check:

1. Is the probe update compute dispatch actually being issued each
   frame?
2. Does the DDGI probe CS read the directional light correctly?
3. Does it sample a shadow map or skip shadowing?

Confirm via RenderDoc or by inspecting the CS itself
(`runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl`).

Common issues:
- **Light array empty** in the CS's constant buffer.
- **Probe positions inside geometry** — rays fail immediately.
- **Shadow sample wrong direction** for probe-to-light.
- **Radiance accumulation zeroed** at each dispatch instead of blended.

Fix → re-capture with `--vis ddgi.irradiance` → confirm colored atlas.

---

## 9 · Fix DDGI sampling in deferred_lighting_ps.hlsl

Open `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl`.

Search for the "DDGI" section (~line 445 per earlier reading). Check:

1. Is the atlas texture bound to the shader (check root signature / descriptor)?
2. Is the probe index lookup valid (world-space → grid coord)?
3. Is the bilinear / Chebyshev weight computation producing non-zero weights?
4. Is the result multiplied by a valid `indirectLightingIntensity`?
5. Is the result **added** to the final color (not multiplied or replaced)?

Common issue: the atlas is sampled but `indirectLightingIntensity` is 0
in the project config. Grep `frame_constants.cpp:269` for how this is
populated.

---

## 10 · Check GTAO application

In `deferred_lighting_ps.hlsl` (line ~849 per earlier reading), GTAO is
applied only to the IBL ambient. It should apply to all indirect:

```hlsl
// OLD (buggy):
ambient = iblAmbient * ao;

// NEW (correct):
float3 indirect = ssgi + ddgi + iblAmbient;
indirect *= pow(ao, indirectAOExponent);
```

This produces proper contact-darkening on all indirect sources.

---

## 11 · Calibrate PCSS

In the PCSS code path, find the `lightSize` parameter. Typical for
directional sun:
- `lightSize = 0.01` (world units; sun disc is ~0.5° angular radius).
- Default UE5 uses ~0.005–0.01.

Set to 0.01, capture again. Expected: visible penumbra softening on
column shadows (currently hard-edged).

---

## 12 · Final baseline

After the fixes:

```bash
uzengine.exe --project path/to/sponza.uzproj --editor \
  --screenshot e:/UzEngine/.screenshots/sponza-debug/FINAL.png \
  --screenshot-frame 120 --frames 180
```

Compare to `00-baseline.png`. Expected:
- Shadowed areas colored warm, not uniform gray.
- Soft shadow penumbra on long shadows.
- Contact shadows near column bases.
- Overall scene brighter in shadowed areas (~20% more luma).

If the result looks right, commit the fixes as Phase 0 per the
[aaa-default-pipeline.md](./aaa-default-pipeline.md).

---

## 13 · If step 12 doesn't match expectations

The gap is larger than Phase 0 can fix. Proceed to Phase 1 (Indirect
Integration pass unification), as detailed in the default-pipeline doc.

---

## 14 · Files referenced

- `runtime/engine/renderer/gi/ddgi_system.cpp` — probe update
- `runtime/shaders/runtime/gi/ddgi_probe_update_cs.hlsl` — the CS
- `runtime/shaders/runtime/lighting/deferred_lighting_ps.hlsl` — deferred PS
- `runtime/engine/renderer/frame/frame_constants.cpp` — CB population
- `runtime/shaders/runtime/lighting/shadow_pcss.hlsli` — PCSS
- `runtime/shaders/runtime/lighting/contact_shadows.hlsli` — contact shadows

All paths are relative to repo root.
