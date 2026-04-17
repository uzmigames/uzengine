# Validation Methodology — Proving the Pipeline Works

You cannot know if a lighting pipeline is AAA-quality by eyeballing
screenshots. You need **quantitative validation** against a
physically-correct reference. This document describes the methodology
UzEngine should use to prove (and iterate on) the new pipeline's
correctness.

---

## 1 · The reference target

### 1.1 Ground truth: UzEngine's offline path tracer

We have a reference path tracer in
`runtime/engine/renderer/passes/pathtracing/`. Offline, converged at
4096+ spp per pixel, it produces the **physically-correct** image for
the given scene and lighting configuration.

This is our ground truth. All real-time pipeline outputs are compared
against it.

### 1.2 Why a path tracer, not Falcor or UE5

- **Falcor**: different material conventions, different sky model,
  different tonemap. Comparing is unreliable.
- **UE5**: different BRDF, different sky, different exposure.
- **Our own path tracer**: same scene data, same materials, same sky,
  same tonemap. Any difference is the real-time approximation error,
  not a cross-engine difference.

### 1.3 Scenes

Three canonical scenes for validation:

1. **Sponza** — interior with directional sun, rich GI bounce.
2. **Village (outdoor)** — mixed interior/exterior with local lights (lamps, torches).
3. **Night dungeon** — dominated by point lights, strong SSS on characters.

Each scene has a preset camera path (8–12 viewpoints) covering lit
areas, shadowed areas, reflective surfaces, and character close-ups.

---

## 2 · Primary metric: PSNR

Peak Signal-to-Noise Ratio, computed per channel (R/G/B) and averaged.

```
PSNR = 20 × log10(PEAK / sqrt(MSE))
```

where `PEAK = 1.0` for tonemapped LDR images, or some reference max
for HDR (typically 100 × the mean, clamped).

### 2.1 Thresholds (from project rule: "PSNR >= 35 dB = pass")

| PSNR | Quality | Action |
|------|---------|--------|
| ≥ 45 dB | Excellent | Production-ready. |
| 40–45 dB | Very good | Minor artifacts, acceptable. |
| 35–40 dB | **Pass** | Meets project bar; may have localized issues. |
| 30–35 dB | Borderline | Investigate before shipping. |
| 25–30 dB | Noticeable artifacts | Fix required. |
| < 25 dB | Broken | Pipeline fails validation. |

### 2.2 Per-viewpoint reporting

Don't just report the average. Report the **minimum** PSNR across all
camera viewpoints and the **per-channel** breakdown. A 38 dB average
with a 22 dB blue channel at viewpoint 5 means something is broken in
blue light.

---

## 3 · Secondary metrics

### 3.1 SSIM (Structural Similarity)

Complementary to PSNR. Pixel-wise error (PSNR) catches overall
brightness mismatch. SSIM catches **structural** mismatch — edges,
shadows, highlights in wrong places.

Threshold: SSIM ≥ 0.95 = pass.

### 3.2 LPIPS (Learned Perceptual Image Patch Similarity)

Deep-learning-based perceptual metric. Slower but matches human
perception better than PSNR/SSIM. Optional for critical scenes.

Threshold: LPIPS ≤ 0.05 = pass.

### 3.3 Temporal stability

For moving-camera captures, compute the **delta between consecutive
frames**. Large deltas = temporal instability (flickering, ghosting).

Metric: mean frame-to-frame luminance variance, averaged across the
camera path. Lower = more stable.

Threshold: variance ≤ 0.002 = stable.

### 3.4 Energy balance (HDR)

Sum of all outgoing radiance from a scene should equal incoming light
minus absorbed. A 10% discrepancy points to a light-leak or
energy-overflow.

---

## 4 · Reference capture procedure

### 4.1 Offline reference

```bash
# Capture 4096-spp path-traced references for all 8 viewpoints
for viewpoint in {1..8}; do
    uzengine.exe --project scenes/sponza.uzproj \
        --mode pathtracing \
        --spp 4096 \
        --camera-preset viewpoint_$viewpoint \
        --screenshot validation/sponza_ref_${viewpoint}.png \
        --frames 1
done
```

Takes ~30 min per viewpoint. One-time cost per scene.

### 4.2 Real-time capture

```bash
# Capture real-time at same viewpoints after 120 warmup frames
for viewpoint in {1..8}; do
    uzengine.exe --project scenes/sponza.uzproj \
        --camera-preset viewpoint_$viewpoint \
        --screenshot validation/sponza_rt_${viewpoint}.png \
        --screenshot-frame 120 --frames 180
done
```

---

## 5 · Metric computation

### 5.1 Tool: `scripts/validate_psnr.py`

Create a validation script:

```python
# scripts/validate_psnr.py
import sys
from PIL import Image
import numpy as np
from skimage.metrics import structural_similarity as ssim

ref = np.array(Image.open(sys.argv[1])) / 255.0
rt  = np.array(Image.open(sys.argv[2])) / 255.0

# Compute PSNR per channel
mse_per_ch = np.mean((ref - rt) ** 2, axis=(0, 1))
psnr_per_ch = 20 * np.log10(1.0 / np.sqrt(mse_per_ch + 1e-10))

# Compute SSIM per channel
ssim_per_ch = [ssim(ref[:,:,c], rt[:,:,c], data_range=1.0) for c in range(3)]

print(f"PSNR  R={psnr_per_ch[0]:.1f} G={psnr_per_ch[1]:.1f} B={psnr_per_ch[2]:.1f} dB (avg {psnr_per_ch.mean():.1f})")
print(f"SSIM  R={ssim_per_ch[0]:.3f} G={ssim_per_ch[1]:.3f} B={ssim_per_ch[2]:.3f}")
```

### 5.2 CI integration

On every PR that touches the lighting pipeline:

1. Capture reference (cached; regenerated on scene change only).
2. Capture real-time.
3. Compute PSNR + SSIM.
4. Compare against thresholds.
5. **Fail the build** if PSNR drops below the baseline.

---

## 6 · Per-pass validation

Not just final image — validate each pass's output against the path
tracer's contribution.

### 6.1 Direct-only

Disable all indirect lighting. Capture. Compare to path-tracer's "direct-only" mode.

Tests: BRDF, shadows, attenuation, area-light integration.

### 6.2 Indirect diffuse only

Disable direct. Capture DDGI + SSGI + IBL output. Compare to path-tracer's
indirect-diffuse-only mode (1 bounce diffuse path).

Tests: DDGI correctness, SSGI correctness, IBL correctness.

### 6.3 Reflection only

Disable all non-specular. Compare.

Tests: SSR correctness, roughness-dependent spec lobe.

### 6.4 Volumetric only

Opaque scene black; volumetric fog only. Compare.

Tests: fog extinction, inscattering accuracy.

---

## 7 · Debug visualizations

Every pass has a visualization mode via `--vis <target>`. Cross-
reference with the path tracer's equivalent buffer.

Example:
```bash
# Real-time DDGI atlas
uzengine.exe --vis ddgi.irradiance --screenshot rt.png

# Path-traced diffuse irradiance (same probes, sample 4096)
uzengine.exe --mode pathtracing --vis ddgi.irradiance --spp 4096 --screenshot pt.png
```

The two should match closely. If they diverge, the DDGI probe update
is wrong.

---

## 8 · The validation matrix

Build a table of (scene × viewpoint × pass) with the PSNR/SSIM score
in each cell. This is the regression suite.

Example (Sponza):

| Viewpoint | Direct PSNR | Diffuse GI PSNR | Specular PSNR | Volumetric PSNR | Total PSNR |
|-----------|-------------|------------------|---------------|------------------|------------|
| 1 (atrium)      | 42.1 | 38.4 | 45.2 | 42.8 | 39.1 |
| 2 (side hall)   | 41.5 | 36.2 | 44.8 | 42.5 | 37.8 |
| 3 (close column)| 43.7 | 33.1 | 46.0 | N/A  | 34.2 ← investigate |
| ...

In the example above, viewpoint 3 has a **low diffuse GI** score — look
at viewpoint 3's DDGI output vs path-traced reference to find the
cause.

---

## 9 · Regression detection

Run the validation suite on:

- Every PR that touches `runtime/shaders/`, `runtime/engine/renderer/`, `runtime/engine/renderer/gi/`, `runtime/engine/renderer/lighting/`, `runtime/engine/renderer/shadows/`.
- Nightly on main branch.
- Before each release.

Automated in CI. Failures block merges.

---

## 10 · The "perceptual A/B" step

After the automated validation passes, do human A/B:

1. Take the real-time and path-traced captures at each viewpoint.
2. Mix them randomly (blind).
3. Review: is there a viewpoint where the real-time is visibly wrong
   even though PSNR is fine?

Cases where PSNR is fine but perception disagrees:
- Temporal flicker (doesn't show in a single frame).
- Tonal shift that PSNR averages out.
- High-frequency detail preservation differences.

These are caught only by human review. Budget ~2 hours per release.

---

## 11 · Specific Sponza acceptance criteria

From [aaa-default-pipeline.md](./aaa-default-pipeline.md) §11:

- Overall PSNR Sponza ≥ 35 dB across all viewpoints.
- Diffuse-GI PSNR Sponza ≥ 30 dB (harder to hit — indirect is noisy).
- Direct PSNR ≥ 40 dB.
- Temporal variance ≤ 0.002.

If all hit, Phase 0–3 is complete.

---

## 12 · Tools we need to build

- `scripts/validate_psnr.py` — the PSNR/SSIM/LPIPS computation.
- `scripts/capture_validation_suite.py` — orchestrates ref + RT captures.
- CI workflow that runs the suite.
- A webpage that shows per-viewpoint diff images (alongside PSNR) for
  easy human review.

~2 weeks total investment. Pays off forever after.

---

## 13 · References

- Wang et al., "Image Quality Assessment: From Error Visibility to Structural Similarity" (SSIM paper, IEEE Trans. on Image Processing 2004).
- Zhang et al., "The Unreasonable Effectiveness of Deep Features as a Perceptual Metric" (LPIPS, CVPR 2018).
- Jimenez, "Next Generation Post Processing in Call of Duty: Advanced Warfare" (SIGGRAPH 2014) — discusses PSNR/SSIM use in production shipping.
- "Falcor = ground truth, PSNR >= 35 dB = pass" — UzEngine project rule (AGENTS.override.md).
