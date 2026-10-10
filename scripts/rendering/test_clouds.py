"""Volumetric clouds harness: baseline and comparison captures of the cloud reference scene (Rulebook task
phase0-3.1.0-volumetric-clouds-rewrite: Phase 0 item 0.12, the "before" of Phases 1-5 and of Testing 8.x).

Scene: scripts/rendering/cloud_reference.py (run it first): a 1 km floor, the sky atmosphere, the sun at 40 deg elevation and
azimuth 0 (+X), a sky light, one cloud layer (coverage 0.45, extinction 0.04, wind 0, 64 primary steps) and no fog;
one project per camera pose (sun, away, horizon, above; above with a 100 m near plane, which keeps the shaders' float32
ray reconstruction exact 15 km up). Per pose the harness captures the base scene ("clouds") and
the `noclouds` variant (the clouds entity removed); at the sun pose also `steps32` and `steps16` (only primary_steps
changes); and --repeats - 1 timing-only repeats of every "clouds" capture.

Capture (scripts/rendering/flicker/runner.run_capture, the projects' 1920x1080 window, TAA on as every scripts/*-reference
project sets it): FRAMES frames; screenshots of frames SHOT_FIRST..SHOT_LAST (32 consecutive frames, 8-bit sRGB PNG of
the tonemapped TAA output); a frame-wide region dump of DUMP_FRAME = SHOT_LAST with the `motion` fields (LIGHTING: the
geometry mask; OUTPUT: the TAA output, linear HDR, never pre-exposed); gpu_ms from the run manifest over the timing
window TIMING_FIRST..FRAMES-1, which holds no screenshot and no dump (a dump is ~100 MB, a screenshot a few MB).
Without TAA (a variant setting r.TAA 0) the dump's TAA stamp is missing and only its LIGHTING records are written: the
geometry still comes from them, and the single-frame metrics of that pose fall back, for all its captures, to the
screenshots of DUMP_FRAME, sRGB-decoded to display-linear (the report's `source` says which).

Geometry: per-pixel world ray directions from the dump's unjittered view-projection (two unprojected depths, as
regiondump.world_positions); elevation e = asin(ray.z), sun angle theta = acos(ray . SUN_DIR); Y = Rec.709 luminance.
Sky region Omega = no geometry in either capture of the pose (lighting_normal.w < 0), e > MIN_ELEVATION (except at the
'above' pose, whose clouds lie below the horizon) and theta > SUN_EXCLUDE_HDR (the sun disk).

Metrics (functions below, so later phases can gate on them; all reported for the baseline):
  mask      rel = (Y_clouds - Y_noclouds) / Y_noclouds over Omega: cloud alpha is not dumped, so a cloud is what changes
            the sky. cloud = |rel| > TAU_CLOUD, clear = |rel| < TAU_CLEAR, thin = the rest; cloud_fraction,
            clear_fraction, thin_fraction (shares of Omega), mean_abs_rel.
  cost      from the "clouds" manifest: cloud_ms = sum of gpu_ms over every key that starts with "cloud" (case-
            insensitive: CloudCompute, CloudComposite, and any Cloud_* pass later), each listed in cloud_passes;
            frame_gpu_ms = gpu_ms._frame (mean GPU span of a frame), frame_cpu_ms = frame_ms.mean; frame_delta_ms =
            frame_gpu_ms - the noclouds capture's. With --repeats >= 2, `repeats`: mean/std/min/max/range of cloud_ms
            and frame_gpu_ms (scripts/rendering/flicker/metrics.repeatability). At the dose pose, cloud_ms_by_steps.
  banding   vertical luminance profiles through the clouds: segments of SEG_LEN consecutive rows (every
            SEG_COL_STEP-th column) inside Omega with >= SEG_MIN_CLOUD of their pixels cloud. Per segment q = ln Y,
            linear detrend, Hann window w, periodogram P(f) = |FFT(q w)|^2 / sum(w^2), averaged over the segments.
            hf_power = mean P over HF_BAND = 0.125-0.25 cycles/px (periods 4-8 px): the step frequency of the uniform
            march, whose slices are (H_layer / 64) / h * sin e cos e = 3.8-5 px apart at e = 25-50 deg, up to the
            Nyquist of the half-resolution trace; mid_power over MID_BAND = 0.025-0.0625 (periods 16-40 px, the clouds'
            own structure); hf_over_mid. A CPU port of cloud_raymarch_cs.hlsl (HEAD) on a crop of the sun pose, analysed
            at the trace's half resolution, gives hf_power 1.5e-6 converged (256 steps), 3.0e-6 at 64, 9.4e-6 at 32 and
            1.5e-5 at 16 (the engine's values differ in scale: bilinear upsampling and TAA attenuate the band equally
            for every capture, so compare captures, not these numbers): at 64 steps half of the band is step artifact,
            which an adaptive, jittered and TAA-resolved march should remove. At the horizon pose the clouds'
            foreshortened structure dominates the band (+15 % at 64 steps). Negative control: the same segments of the
            noclouds capture (control_hf_power, the floor of everything that is not a cloud: the sky gradient, TAA,
            float16); hf_over_control. Positive control, the dose response at the sun pose: the same segments of the
            steps32 and steps16 captures, dose_monotone = hf(16) > hf(32) > hf(64). An 8-bit plateau count was
            rejected: at 64 steps the step error is ~0.1 % (CPU port), below one code, while the sky gradient alone
            makes plateaus.
  temporal  per-pixel coefficient of variation CoV = std_t(Y) / mean_t(Y) over the 32 consecutive static frames of the
            screenshot range (sRGB-decoded display-linear Y; pixels with mean < COV_MIN_MEAN skipped), over
            Omega_shot = Omega with the sun excluded to SUN_EXCLUDE_SHOT (bloom). sky.cov_p95 is the headline; also
            p50/p99/max, changed_fraction (CoV > 0), std_p95, and the same over the cloud and clear masks. Negative
            control: the noclouds capture over the same pixels (control.sky). Sensitivity: per mask, max_code_p50 =
            the median over the pixels of their brightest 8-bit channel code (max over the frames) and
            clipped_fraction = the share that reached 255. Clouds of linear Y 2-6 sit on the ACES shoulder (codes
            234-249), where a 1 % change in HDR moves 0.1-0.2 code against 0.7 code for the sky at Y 0.3, so the
            cloud CoV of the screenshots under-reads an HDR flicker 3-6 times over.
  silver    (sun and away poses) d = chessboard distance of a cloud pixel to the nearest non-cloud pixel of Omega
            (pixels outside Omega count as cloud: unknown); edge = cloud with d <= R_EDGE, interior = cloud with
            d >= R_INTERIOR; ratio = mean Y(edge) / mean Y(interior) of the clouds capture, over SUN_WINDOW = 3-30 deg
            from the sun at the sun pose, and over ANTI_WINDOW = 100-180 deg at the away pose (the control: no forward
            scattering there). A silver lining lifts the sun-pose ratio above the away-pose one. An edge pixel is
            partly transparent, so the sky behind it is part of its Y: sky_ratio = the same ratio over the same pixels
            of the noclouds capture says how much brighter that sky is behind the edges than behind the interiors
            (near the sun the sky's forward peak can lift the sun-pose ratio without any scattering in the clouds).
  aerial    (horizon pose) the contrast of the clouds against the sky per elevation band of AP_BANDS: contrast =
            mean |rel| over Omega in the band (how much the clouds still change the sky there), chroma_distance =
            mean |c_clouds - c_noclouds| of the rg chromaticity c = (R, G) / (R + G + B), cloud_fraction;
            falloff = contrast(0.5-2 deg) / contrast(8-16 deg), cloud bases 150-250 km away against 25-50 km.
            Aerial perspective drives the far bands toward the sky (contrast and chroma_distance toward 0).

Checks, the same for --baseline and the default run (pre-registered for Phase 0; later phases add their gates):
  C1 every capture completed without device removal: runner.run_capture raises on DXGI_ERROR_DEVICE_REMOVED, shader
     compile failures and closed windows; exit code 0; every frame rendered; >= MIN_SHOTS screenshots of the range;
     the dump of DUMP_FRAME present and valid; a cloud pass in the gpu_ms of every clouds capture (0.12) and none in
     the noclouds ones; output and render resolution W x H;
  C2 every metric is finite (None, NaN and inf fail; their paths are listed).
  Default run only: C3 the baseline report exists. The comparison with the baseline (every numeric metric, and per
  pose the image delta |dY| / Y over Omega of the dumps) is reported, not gated.
Reported only: whether the noise volumes loaded ('CloudPass: shape noise loaded' / 'detail noise loaded'), the cloud CB
the pass logged (its sunDir against SUN_DIR), the sky light's SH L0, the sun's pixel at the sun pose.
JPEG montage for the user's review (clouds_montage.jpg): per pose clouds | noclouds | mask (baseline) or baseline |
after | |dY| / Y (default run), and at the sun pose a 640x360 crop row 16 | 32 | 64 steps.

Usage: python scripts/rendering/test_clouds.py --baseline [--reuse] [--poses sun,away,horizon,above] [--repeats 2]
       python scripts/rendering/test_clouds.py [--reuse]          # into test-clouds/after, compared with the baseline
Outputs: .screenshots/flicker-elimination/test-clouds/{baseline,after}/: a directory per capture, clouds.json,
clouds_montage.jpg.
"""
from __future__ import annotations

import argparse
import glob
import json
import math
import os
import re
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import regiondump  # noqa: E402
import runner  # noqa: E402
import metrics as flicker_metrics  # noqa: E402
import cloud_reference as ref  # noqa: E402

REPO = runner.REPO
OUT = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-clouds')
TASK = 'phase0-3.1.0-volumetric-clouds-rewrite'
W, H = ref.W, ref.H
FRAMES = 300
SHOT_FIRST, SHOT_LAST = 150, 181               # 32 consecutive frames
DUMP_FRAME = SHOT_LAST                          # f_181.png and region_181.bin hold the same render
TIMING_FIRST = 200                              # 100 timed frames after the screenshots and the dump
MIN_SHOTS = 24
REGION_FIELDS = 'motion'                        # LIGHTING (geometry) + OUTPUT (TAA output, linear HDR): 48 B/px
BASE_CVARS = {'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0}
REC709 = np.array([0.2126, 0.7152, 0.0722])
SUN_DIR = np.asarray(ref.SUN_DIR, dtype=np.float64)

# Analysis constants, fixed before the first engine run.
MIN_ELEVATION = 0.5             # deg: the floor edge, the horizon line and the atmosphere's ground are not sky
SUN_EXCLUDE_HDR = 3.0           # deg around the sun in the HDR dump: the sky shader's disk (radius max(light_source_angle
                                # / 2, 0.536 deg)) and corona (to 1.34 deg); its halo reaches 4.29 deg but is < 0.5 % of
                                # the sky past 3 deg (sun_disk_intensity 0.1)
SUN_EXCLUDE_SHOT = 6.0          # deg: plus the bloom halo of the screenshots
TAU_CLOUD = 0.25                # |rel| above which a pixel is cloud
TAU_CLEAR = 0.03                # |rel| below which it is clear sky
R_EDGE, R_INTERIOR = 4, 24      # px to the nearest non-cloud sky pixel
SUN_WINDOW = (3.0, 30.0)        # deg from the sun (sun pose)
ANTI_WINDOW = (100.0, 180.0)    # deg from the sun (away pose, control)
MIN_PX = 200                    # fewest pixels a statistic is taken over
SEG_LEN, SEG_COL_STEP, SEG_MIN_CLOUD, MIN_SEGMENTS = 128, 4, 0.75, 20
HF_BAND = (0.125, 0.25)         # cycles/px
MID_BAND = (0.025, 0.0625)      # cycles/px
AP_BANDS = ((0.5, 2.0), (2.0, 4.0), (4.0, 8.0), (8.0, 16.0), (16.0, 32.0))
AP_FALLOFF = ((0.5, 2.0), (8.0, 16.0))          # (far, near)
COV_MIN_MEAN = 1e-3             # display-linear luminance
DOSE_VARIANTS = (('steps32', 32), ('steps16', 16))
DOSE_CROP = (640, 360, 1280, 720)               # sun pose, below the sun: x0, y0, x1, y1


# ── image and geometry helpers ─────────────────────────────────────────────────────────────────

def luminance(rgb):
    """Rec.709 luminance of (..., 3) linear RGB."""
    return np.asarray(rgb, dtype=np.float64) @ REC709


def srgb_to_linear(codes):
    """8-bit sRGB code values -> display-linear [0, 1]."""
    c = np.asarray(codes, dtype=np.float64) / 255.0
    return np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)


def pixel_rays(view_proj, camera_pos, width=W, height=H):
    """(height, width, 3) unit world directions of the pixel centres from an unjittered view-projection (row-vector
    convention as regiondump.world_positions: two well-separated depths, oriented away from the camera, so no depth
    convention is assumed)."""
    xs = (np.arange(width) + 0.5) / width * 2.0 - 1.0
    ys = 1.0 - (np.arange(height) + 0.5) / height * 2.0
    gx, gy = np.meshgrid(xs, ys)
    inv = np.linalg.inv(np.asarray(view_proj, dtype=np.float64))

    def unproject(z):
        clip = np.stack([gx, gy, np.full_like(gx, z), np.ones_like(gx)], axis=-1)
        w = clip @ inv
        return w[..., :3] / w[..., 3:4]

    cam = np.asarray(camera_pos, dtype=np.float64)
    a, b = unproject(0.9), unproject(0.02)
    d = b - a
    d /= np.maximum(np.linalg.norm(d, axis=-1, keepdims=True), 1e-12)
    far = np.where((np.linalg.norm(a - cam, axis=-1) > np.linalg.norm(b - cam, axis=-1))[..., None], a, b)
    ahead = np.einsum('...i,...i->...', d, far - cam)[..., None]
    return np.where(ahead < 0.0, -d, d)


def project_direction(view_proj, camera_pos, direction, width=W, height=H):
    """Output pixel (x, y) of a direction seen from the camera (a point 100 km away), or None behind the camera."""
    p = np.append(np.asarray(camera_pos, dtype=np.float64) + 1.0e7 * np.asarray(direction, dtype=np.float64), 1.0)
    clip = p @ np.asarray(view_proj, dtype=np.float64)
    if clip[3] <= 0.0:
        return None
    ndc = clip[:3] / clip[3]
    return [float((ndc[0] * 0.5 + 0.5) * width - 0.5), float((0.5 - ndc[1] * 0.5) * height - 0.5)]


def ray_angles(rays, sun_dir=SUN_DIR):
    """(elevation, sun angle) in degrees per pixel."""
    s = np.asarray(sun_dir, dtype=np.float64)
    elev = np.degrees(np.arcsin(np.clip(rays[..., 2], -1.0, 1.0)))
    sun = np.degrees(np.arccos(np.clip(rays @ (s / np.linalg.norm(s)), -1.0, 1.0)))
    return elev, sun


def sky_region(geometry, elevation, sun_angle, min_elevation=MIN_ELEVATION, sun_exclude=SUN_EXCLUDE_HDR):
    """Omega: no geometry, above `min_elevation` (None: no elevation cut), away from the sun disk."""
    region = ~np.asarray(geometry, dtype=bool) & (sun_angle > sun_exclude)
    if min_elevation is not None:
        region &= elevation > min_elevation
    return region


# ── metrics ────────────────────────────────────────────────────────────────────────────────────

def cloud_masks(y_clouds, y_sky, region, tau_cloud=TAU_CLOUD, tau_clear=TAU_CLEAR):
    """rel = (Y_clouds - Y_sky) / Y_sky and the cloud / clear / thin masks inside `region`."""
    rel = (np.asarray(y_clouds, dtype=np.float64) - y_sky) / np.maximum(y_sky, 1e-6)
    a = np.abs(rel)
    cloud = region & (a > tau_cloud)
    clear = region & (a < tau_clear)
    return {'rel': rel, 'cloud': cloud, 'clear': clear, 'thin': region & ~cloud & ~clear}


def mask_metrics(masks, region):
    n = int(region.sum())
    if n < MIN_PX:
        return {'sky_px': n, 'cloud_fraction': None, 'clear_fraction': None, 'thin_fraction': None,
                'mean_abs_rel': None}
    return {'sky_px': n,
            'cloud_fraction': float(masks['cloud'].sum()) / n,
            'clear_fraction': float(masks['clear'].sum()) / n,
            'thin_fraction': float(masks['thin'].sum()) / n,
            'mean_abs_rel': float(np.abs(masks['rel'][region]).mean())}


def gpu_cloud_keys(gpu_ms):
    """The cloud passes of a manifest's gpu_ms: every key starting with 'cloud', case-insensitive."""
    return {k: float(v) for k, v in (gpu_ms or {}).items() if k.lower().startswith('cloud')}


def cost_metrics(manifest, manifest_noclouds=None, repeat_manifests=()):
    """Cloud pass cost and frame cost from run manifests (means over each run's timing window)."""
    gpu = (manifest or {}).get('gpu_ms') or {}
    passes = gpu_cloud_keys(gpu)
    out = {'cloud_passes': passes,
           'cloud_ms': float(sum(passes.values())) if passes else None,
           'frame_gpu_ms': float(gpu['_frame']) if '_frame' in gpu else None,
           'frame_cpu_ms': float(((manifest or {}).get('frame_ms') or {}).get('mean', float('nan'))),
           'timing_samples': ((manifest or {}).get('frames') or {}).get('timing_samples')}
    if manifest_noclouds is not None:
        g0 = manifest_noclouds.get('gpu_ms') or {}
        out['frame_gpu_ms_noclouds'] = float(g0['_frame']) if '_frame' in g0 else None
        out['frame_delta_ms'] = (out['frame_gpu_ms'] - out['frame_gpu_ms_noclouds']
                                 if out['frame_gpu_ms'] is not None and out['frame_gpu_ms_noclouds'] is not None
                                 else None)
    runs = [m for m in (manifest,) + tuple(repeat_manifests) if m]
    if len(runs) >= 2:
        def spread(values):
            r = flicker_metrics.repeatability(values)
            return {k: r[k] for k in ('runs', 'mean', 'std', 'min', 'max', 'range')}
        out['repeats'] = {
            'cloud_ms': spread([sum(gpu_cloud_keys(m.get('gpu_ms')).values()) for m in runs]),
            'frame_gpu_ms': spread([float((m.get('gpu_ms') or {}).get('_frame', float('nan'))) for m in runs]),
        }
    return out


def banding_segments(cloud, region, seg_len=SEG_LEN, col_step=SEG_COL_STEP, min_cloud=SEG_MIN_CLOUD):
    """(n, 2) int array of (x, y0): non-overlapping vertical runs of `seg_len` rows inside `region`, each with at
    least `min_cloud` of its pixels in the cloud mask."""
    segs = []
    height, width = region.shape
    for x in range(0, width, col_step):
        col, cl = region[:, x], cloud[:, x]
        y = 0
        while y + seg_len <= height:
            run = col[y:y + seg_len]
            if not run.all():
                y += int(np.argmin(run)) + 1            # past the first pixel outside the region
                continue
            if cl[y:y + seg_len].mean() >= min_cloud:
                segs.append((x, y))
                y += seg_len
            else:
                y += seg_len // 4
    return np.asarray(segs, dtype=np.int64).reshape(-1, 2)


def vertical_spectrum(y, segments, seg_len=SEG_LEN):
    """Mean periodogram of the detrended, Hann-windowed ln Y over the segments (rfft bins; cycles/px = k / seg_len)."""
    if len(segments) == 0:
        return None
    rows = segments[:, 1][:, None] + np.arange(seg_len)[None, :]
    q = np.log(np.maximum(np.asarray(y, dtype=np.float64)[rows, segments[:, 0][:, None]], 1e-6))
    t = np.arange(seg_len) - (seg_len - 1) / 2.0
    q = q - q.mean(axis=1, keepdims=True)
    q = q - np.outer(q @ t / (t @ t), t)
    w = np.hanning(seg_len)
    f = np.fft.rfft(q * w, axis=1)
    return (np.abs(f) ** 2).mean(axis=0) / float((w ** 2).sum())


def band_power(spectrum, band, seg_len=SEG_LEN):
    f = np.fft.rfftfreq(seg_len)
    sel = (f >= band[0]) & (f <= band[1])
    return float(np.asarray(spectrum)[sel].mean())


def banding_metrics(y, segments, seg_len=SEG_LEN):
    """Step-band and structure-band power of the vertical ln Y profiles at the given segments."""
    if len(segments) < MIN_SEGMENTS:
        return {'segments': int(len(segments)), 'hf_power': None, 'mid_power': None, 'hf_over_mid': None}
    p = vertical_spectrum(y, segments, seg_len)
    hf, mid = band_power(p, HF_BAND, seg_len), band_power(p, MID_BAND, seg_len)
    return {'segments': int(len(segments)), 'hf_power': hf, 'mid_power': mid,
            'hf_over_mid': hf / mid if mid > 0 else None, 'spectrum': [float(v) for v in p]}


def temporal_cov(frame_paths, masks):
    """Per-pixel CoV of display-linear Y over the frames (Welford), summarised over each mask of `masks`
    (name -> bool array). The first mask is the headline: when it cannot be measured its statistics are None (C2
    fails); any other mask with fewer than MIN_PX usable pixels is left out and listed in `skipped`."""
    from PIL import Image
    n, mean, m2, cmax = 0, None, None, None
    for p in frame_paths:
        with Image.open(p) as im:
            codes = np.asarray(im.convert('RGB'))
        yv = luminance(srgb_to_linear(codes))
        cmax = codes.max(-1) if cmax is None else np.maximum(cmax, codes.max(-1))
        n += 1
        if mean is None:
            mean, m2 = np.zeros_like(yv), np.zeros_like(yv)
        d = yv - mean
        mean += d / n
        m2 += d * (yv - mean)
    headline = next(iter(masks))
    out = {'frames': n, 'skipped': []}
    if n < 2:
        out[headline] = {'px': 0, 'cov_p95': None}
        return out
    std = np.sqrt(m2 / n)
    valid = mean > COV_MIN_MEAN
    cov = np.where(valid, std / np.maximum(mean, 1e-12), 0.0)
    for name, mask in masks.items():
        sel = mask & valid
        k = int(sel.sum())
        if k < MIN_PX:
            if name == headline:
                out[name] = {'px': k, 'cov_p95': None}
            else:
                out['skipped'].append(f'{name} ({k} px)')
            continue
        c, s = cov[sel], std[sel]
        out[name] = {'px': k, 'cov_p50': float(np.percentile(c, 50)), 'cov_p95': float(np.percentile(c, 95)),
                     'cov_p99': float(np.percentile(c, 99)), 'cov_max': float(c.max()),
                     'changed_fraction': float((c > 0).mean()), 'std_p95': float(np.percentile(s, 95)),
                     'max_code_p50': float(np.percentile(cmax[sel], 50)),
                     'clipped_fraction': float((cmax[sel] >= 255).mean())}
    return out


def noncloud_distance(cloud, region, max_r=R_INTERIOR):
    """Chessboard distance in px (1 = next to a non-cloud pixel, capped at max_r) of each cloud pixel to the nearest
    non-cloud pixel of `region`; 0 elsewhere. Pixels outside the region, and beyond the frame, count as cloud."""
    inside = cloud | ~region
    dist = np.where(cloud, 1, 0).astype(np.int32)
    cur = inside.copy()
    for r in range(2, max_r + 1):
        p = np.pad(cur, 1, constant_values=True)
        nxt = cur.copy()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy or dx:
                    nxt &= p[1 + dy:1 + dy + cur.shape[0], 1 + dx:1 + dx + cur.shape[1]]
        hit = cloud & nxt
        if not hit.any():
            break
        dist[hit] = r
        cur = nxt
    return dist


def silver_lining(y, cloud, region, sun_angle, window, r_edge=R_EDGE, r_interior=R_INTERIOR, y_sky=None):
    """Mean Y of the cloud-edge pixels over mean Y of the cloud-interior pixels inside an angular window from the sun;
    with `y_sky` (the noclouds capture) also the same ratio of the sky behind those pixels (sky_ratio)."""
    dist = noncloud_distance(cloud, region, r_interior)
    win = region & (sun_angle >= window[0]) & (sun_angle <= window[1])
    edge = cloud & win & (dist <= r_edge)
    interior = cloud & win & (dist >= r_interior)
    out = {'window_deg': list(window), 'window_px': int(win.sum()), 'edge_px': int(edge.sum()),
           'interior_px': int(interior.sum())}
    if edge.sum() < MIN_PX or interior.sum() < MIN_PX:
        out.update({'edge_mean': None, 'interior_mean': None, 'ratio': None})
        return out
    yv = np.asarray(y)
    em, im = float(yv[edge].mean()), float(yv[interior].mean())
    out.update({'edge_mean': em, 'interior_mean': im, 'ratio': em / im if im > 0 else None})
    if y_sky is not None:
        ys = np.asarray(y_sky)
        sem, sim = float(ys[edge].mean()), float(ys[interior].mean())
        out.update({'sky_edge_mean': sem, 'sky_interior_mean': sim, 'sky_ratio': sem / sim if sim > 0 else None})
    return out


def aerial_perspective(rgb_clouds, rgb_sky, masks, region, elevation, bands=AP_BANDS, falloff=AP_FALLOFF):
    """Per elevation band: how much the clouds still change the sky (luminance and chromaticity)."""
    def chroma(rgb):
        a = np.asarray(rgb, dtype=np.float64)
        return a[..., :2] / np.maximum(a.sum(-1, keepdims=True), 1e-9)
    cd = np.linalg.norm(chroma(rgb_clouds) - chroma(rgb_sky), axis=-1)
    absrel = np.abs(masks['rel'])
    rows = []
    for lo, hi in bands:
        sel = region & (elevation > lo) & (elevation <= hi)
        k = int(sel.sum())
        if k < MIN_PX:
            rows.append({'band_deg': [lo, hi], 'px': k, 'contrast': None, 'chroma_distance': None,
                         'cloud_fraction': None})
            continue
        rows.append({'band_deg': [lo, hi], 'px': k, 'contrast': float(absrel[sel].mean()),
                     'chroma_distance': float(cd[sel].mean()), 'cloud_fraction': float(masks['cloud'][sel].mean())})
    by = {tuple(r['band_deg']): r for r in rows}
    far, near = by.get(tuple(falloff[0])), by.get(tuple(falloff[1]))
    ok = far is not None and near is not None and far['contrast'] is not None and near['contrast']
    return {'bands': rows, 'falloff': (far['contrast'] / near['contrast']) if ok else None,
            'falloff_bands_deg': [list(falloff[0]), list(falloff[1])]}


def numeric_problems(obj, path=''):
    """Paths of every None / NaN / inf number in a nested metrics dict (bools and strings are not metrics)."""
    bad = []
    if isinstance(obj, dict):
        for k, v in obj.items():
            bad += numeric_problems(v, f'{path}.{k}' if path else str(k))
    elif isinstance(obj, (list, tuple)):
        for i, v in enumerate(obj):
            bad += numeric_problems(v, f'{path}[{i}]')
    elif obj is None:
        bad.append(path)
    elif isinstance(obj, (bool, str)):
        pass
    elif isinstance(obj, (int, float, np.integer, np.floating)):
        if not math.isfinite(float(obj)):
            bad.append(path)
    return bad


def image_delta(y_after, y_base, region):
    """|dY| / Y of two captures of one pose over the sky region."""
    d = np.abs(np.asarray(y_after) - y_base) / np.maximum(y_base, 1e-6)
    v = d[region]
    if v.size < MIN_PX:
        return {'px': int(v.size), 'mean_rel': None}
    return {'px': int(v.size), 'mean_rel': float(v.mean()), 'p99_rel': float(np.percentile(v, 99)),
            'changed_fraction': float((v > 1e-3).mean())}


def flatten(obj, path=''):
    """Numeric leaves of a nested report as {path: value} (spectra left out)."""
    out = {}
    if isinstance(obj, dict):
        for k, v in obj.items():
            if k != 'spectrum':
                out.update(flatten(v, f'{path}.{k}' if path else str(k)))
    elif isinstance(obj, (list, tuple)):
        for i, v in enumerate(obj):
            out.update(flatten(v, f'{path}[{i}]'))
    elif isinstance(obj, (int, float)) and not isinstance(obj, bool):
        out[path] = float(obj)
    return out


def compare(base, after):
    """after against baseline for every numeric metric both reports have."""
    fb, fa = flatten(base), flatten(after)
    return {k: {'baseline': fb[k], 'after': fa[k], 'delta': fa[k] - fb[k], 'ratio': (fa[k] / fb[k]) if fb[k] else None}
            for k in sorted(set(fb) & set(fa))}


# ── captures ───────────────────────────────────────────────────────────────────────────────────

def capture_name(pose, variant=None, repeat=0):
    return f'{pose}-{variant or "clouds"}' + (f'-r{repeat}' if repeat else '')


def capture_plan(poses, dose_poses, repeats):
    """(name, pose, variant, shots, dump, repeat) of every capture, in run order."""
    plan = []
    for pose in poses:
        plan.append((capture_name(pose), pose, None, True, True, 0))
        plan.append((capture_name(pose, 'noclouds'), pose, 'noclouds', True, True, 0))
    for pose in dose_poses:
        for variant, _ in DOSE_VARIANTS:
            plan.append((capture_name(pose, variant), pose, variant, False, True, 0))
    for r in range(1, repeats):
        for pose in poses:
            plan.append((capture_name(pose, None, r), pose, None, False, False, r))
    return plan


def shot_paths(work, first=SHOT_FIRST, last=SHOT_LAST):
    found = []
    for p in glob.glob(os.path.join(work, 'frames', 'f_*.png')):
        m = re.search(r'f_(\d+)\.png$', p)
        if m and first <= int(m.group(1)) <= last:
            found.append((int(m.group(1)), p))
    return [p for _, p in sorted(found)]


def read_log(work):
    try:
        with open(os.path.join(work, 'run.log'), 'rb') as fh:
            return fh.read().decode('utf-8', 'replace')
    except OSError:
        return ''


def log_facts(text):
    """What the cloud pass reported about itself: the noise volumes, the CB it packed, the sky light's SH L0."""
    facts = {'shape_noise_loaded': 'CloudPass: shape noise loaded' in text,
             'detail_noise_loaded': 'CloudPass: detail noise loaded' in text,
             'raymarch_pipeline_created': 'CloudPass: raymarch pipeline created' in text,
             'cloud_cb': [ln.split('CloudPass: ', 1)[-1].strip() for ln in text.splitlines() if 'CloudPass: CB' in ln][:2]}
    m = re.search(r'sunDir=\(([-0-9.]+),([-0-9.]+),([-0-9.]+)\)', text)
    if m:
        d = np.array([float(v) for v in m.groups()])
        facts['cb_sun_dir'] = d.tolist()
        facts['cb_sun_dir_error_deg'] = float(np.degrees(np.arccos(np.clip(
            d @ SUN_DIR / max(float(np.linalg.norm(d)), 1e-9), -1.0, 1.0))))
    m = re.search(r'SkyLight SH L0:.*?scaled: R=([-0-9.]+) G=([-0-9.]+) B=([-0-9.]+)', text)
    if m:
        facts['sky_sh_l0_scaled'] = [float(v) for v in m.groups()]
    return facts


def run_one(work, project, shots, dump, reuse):
    """One capture. Engine failures (device removal, compile failure, closed window, timeout) are recorded, not
    raised, so one failure does not lose the other captures."""
    manifest_path = os.path.join(work, 'manifest.json')
    rec = {'dir': work, 'project': project, 'reused': False, 'error': None, 'exit_code': None}
    if reuse and os.path.isfile(manifest_path):
        rec['reused'] = True
    else:
        kw = {}
        if dump:
            kw.update(regions=[('FRAME', 0, 0, W, H)], region_fields=REGION_FIELDS,
                      region_first=DUMP_FRAME, region_last=DUMP_FRAME)
        spec = f'{SHOT_FIRST}-{SHOT_LAST}' if shots else (f'{DUMP_FRAME}-{DUMP_FRAME}' if dump else None)
        try:
            res = runner.run_capture(work, project, FRAMES, cvars=BASE_CVARS, timing_first=TIMING_FIRST,
                                     clean=True, timeout=1200, screenshot_frames=spec, **kw)
            rec['exit_code'] = res['exit_code']
        except (RuntimeError, subprocess.TimeoutExpired, OSError, ValueError) as e:
            rec['error'] = str(e)
            return rec
    try:
        with open(manifest_path, encoding='utf-8') as fh:
            rec['manifest'] = json.load(fh)
    except (OSError, ValueError) as e:
        rec['error'] = rec['error'] or f'manifest unreadable: {e}'
    return rec


def validate(rec, variant, shots, dump):
    """C1 for one capture: the reasons it does not count as completed (empty = completed)."""
    if rec.get('error'):
        return [rec['error'][:300]]
    why = []
    if rec.get('exit_code') not in (None, 0):
        why.append(f'exit code {rec["exit_code"]}')
    man = rec.get('manifest')
    if not man:
        return why + ['no manifest']
    rendered = (man.get('frames') or {}).get('rendered', 0)
    if rendered < FRAMES:
        why.append(f'{rendered} of {FRAMES} frames rendered')
    res = man.get('resolution') or {}
    for kind in ('output', 'render'):
        if kind in res and list(res[kind]) != [W, H]:
            why.append(f'{kind} resolution {res[kind]}, not {W}x{H} (the FRAME rect and the pixel rays assume it)')
    taa_on = float((man.get('cvars') or {}).get('r.TAA', 0)) >= 0.5
    rec['taa_on'] = taa_on
    if shots:
        k = len(shot_paths(rec['dir']))
        rec['shots'] = k
        if k < MIN_SHOTS:
            why.append(f'{k} screenshots of {SHOT_FIRST}-{SHOT_LAST} (need {MIN_SHOTS})')
    if dump:
        rep = None
        try:
            rep = regiondump.check_sequence(os.path.join(rec['dir'], 'region'), DUMP_FRAME, DUMP_FRAME,
                                            allow_taa_missing=not taa_on)
        except (OSError, ValueError, IndexError) as e:
            why.append(f'region dump unreadable: {e}')
        if rep is not None and not rep['ok']:
            why.append(f'region dump of frame {DUMP_FRAME} not valid: count {rep["count"]}, missing {rep["missing"]}, '
                       f'invalid {rep["invalid"]}')
        rec['dump_ok'] = bool(rep and rep['ok'])
    cloud_keys = gpu_cloud_keys(man.get('gpu_ms'))
    rec['gpu_cloud'] = cloud_keys
    if variant == 'noclouds':
        if cloud_keys:
            why.append(f'the no-clouds control ran a cloud pass: {cloud_keys}')
    elif not cloud_keys:
        why.append('no cloud pass in gpu_ms (the cloud compute did not run in the timing window)')
    if dump and not taa_on and not os.path.isfile(os.path.join(rec['dir'], 'frames', f'f_{DUMP_FRAME}.png')):
        why.append(f'TAA off and no screenshot of frame {DUMP_FRAME} (the colour source without TAA)')
    return why


# ── per-pose analysis ──────────────────────────────────────────────────────────────────────────

def screenshot_linear(work, frame=DUMP_FRAME):
    from PIL import Image
    p = os.path.join(work, 'frames', f'f_{frame}.png')
    if not os.path.isfile(p):
        return None
    with Image.open(p) as im:
        return srgb_to_linear(np.asarray(im.convert('RGB')))


def load_view(work, taa_on=True, screenshot=False):
    """The dump frame of a capture: geometry mask, camera, linear RGB (the dump's TAA output; the screenshot when the
    capture ran without TAA or `screenshot` asks for it, so every capture compared with it uses the same source)."""
    fr = regiondump.load_sequence(os.path.join(work, 'region'), DUMP_FRAME, DUMP_FRAME,
                                  allow_taa_missing=not taa_on)[0]
    rect = fr.rect('FRAME')
    if taa_on and not screenshot:
        rgb, source = rect.channel('output')[..., :3].astype(np.float64), 'dump OUTPUT (TAA output, linear HDR)'
    else:
        rgb, source = screenshot_linear(work), 'screenshot (sRGB-decoded display-linear: a capture ran without TAA)'
    if rgb is None:
        raise ValueError(f'{work}: no colour for frame {DUMP_FRAME}')
    return {'geometry': rect.geometry_mask(), 'rgb': rgb, 'y': luminance(rgb), 'source': source,
            'view_proj': fr.view_proj, 'camera_pos': fr.camera_pos}


def analyse_pose(pose, works, recs, repeat_manifests=()):
    """Every metric of one pose. `works`: variant (None = clouds) -> capture directory of the completed captures;
    `recs`: (pose, variant) -> capture record."""
    rc, r0 = recs[(pose, None)], recs[(pose, 'noclouds')]
    out = {'cost': cost_metrics(rc.get('manifest'), r0.get('manifest'), repeat_manifests)}
    # One colour source for every capture of the pose: the HDR dump when all of them ran with TAA, else the screenshots.
    shots_only = not all(recs[(pose, v)].get('taa_on', True) for v in works)
    vc = load_view(works[None], rc.get('taa_on', True), shots_only)
    v0 = load_view(works['noclouds'], r0.get('taa_on', True), shots_only)
    rays = pixel_rays(vc['view_proj'], vc['camera_pos'])
    elev, sun = ray_angles(rays)
    geometry = vc['geometry'] | v0['geometry']
    region = sky_region(geometry, elev, sun, None if pose == 'above' else MIN_ELEVATION, SUN_EXCLUDE_HDR)
    masks = cloud_masks(vc['y'], v0['y'], region)
    out['source'] = vc['source']
    out['geometry'] = {'sky_px': int(region.sum()), 'geometry_px': int(geometry.sum()),
                       'sun_angle_min_deg': float(sun.min())}
    if region.any():
        out['geometry']['elevation_deg'] = [float(elev[region].min()), float(elev[region].max())]
    sun_px = project_direction(vc['view_proj'], vc['camera_pos'], SUN_DIR)
    if pose == 'sun' and sun_px is not None:
        out['geometry']['sun_pixel'] = sun_px
    out['mask'] = mask_metrics(masks, region)

    segs = banding_segments(masks['cloud'], region)
    band = banding_metrics(vc['y'], segs)
    if band['hf_power'] is not None:
        band['control_hf_power'] = banding_metrics(v0['y'], segs)['hf_power']
        band['hf_over_control'] = (band['hf_power'] / band['control_hf_power']) if band['control_hf_power'] else None
        steps = ref.CLOUDS['primary_steps']
        dose, dose_ms = {str(steps): band['hf_power']}, {str(steps): out['cost']['cloud_ms']}
        for variant, n in DOSE_VARIANTS:
            if variant in works:
                vd = load_view(works[variant], recs[(pose, variant)].get('taa_on', True), shots_only)
                dose[str(n)] = banding_metrics(vd['y'], segs)['hf_power']
                keys = gpu_cloud_keys((recs[(pose, variant)].get('manifest') or {}).get('gpu_ms'))
                dose_ms[str(n)] = float(sum(keys.values())) if keys else None
        if len(dose) > 1:
            band['dose_hf_power'] = dose
            vals = [dose.get(str(n)) for n in (16, 32, steps)]
            band['dose_monotone'] = all(v is not None for v in vals) and vals[0] > vals[1] > vals[2]
            out['cost']['cloud_ms_by_steps'] = dose_ms
    out['banding'] = band

    shot_region = region & (sun > SUN_EXCLUDE_SHOT)
    tc = temporal_cov(shot_paths(works[None]), {'sky': shot_region, 'cloud': shot_region & masks['cloud'],
                                                'clear': shot_region & masks['clear']})
    tc['control'] = temporal_cov(shot_paths(works['noclouds']), {'sky': shot_region})
    out['temporal'] = tc
    if pose == 'sun':
        out['silver'] = silver_lining(vc['y'], masks['cloud'], region, sun, SUN_WINDOW, y_sky=v0['y'])
    elif pose == 'away':
        out['silver'] = silver_lining(vc['y'], masks['cloud'], region, sun, ANTI_WINDOW, y_sky=v0['y'])
    if pose == 'horizon':
        out['aerial'] = aerial_perspective(vc['rgb'], v0['rgb'], masks, region, elev)
    return out, {'region': region, 'masks': masks, 'y': vc['y']}


# ── montage ───────────────────────────────────────────────────────────────────────────────────

def fmt(v, spec='.3g'):
    return 'n/a' if v is None else format(v, spec)


def _tile(img, label, size=(640, 360)):
    from PIL import Image, ImageDraw
    im = Image.new('RGB', size, (40, 40, 40)) if img is None else (
        img.resize(size, Image.BILINEAR) if img.size != size else img.copy())
    d = ImageDraw.Draw(im)
    d.rectangle([0, 0, size[0], 14], fill=(0, 0, 0))
    d.text((4, 2), label, fill=(255, 255, 0))
    return im


def _shot(work, frame=DUMP_FRAME):
    from PIL import Image
    p = os.path.join(work, 'frames', f'f_{frame}.png')
    if not os.path.isfile(p):
        return None
    with Image.open(p) as im:
        return im.convert('RGB')


def _mask_overlay(work, arrays):
    from PIL import Image
    base = _shot(work)
    if base is None:
        return None
    a = np.asarray(base).astype(np.float64)
    masks, region = arrays['masks'], arrays['region']
    dist = noncloud_distance(masks['cloud'], region, R_INTERIOR)
    edge = masks['cloud'] & (dist <= R_EDGE)
    interior = masks['cloud'] & (dist >= R_INTERIOR)
    for m, colour in ((masks['clear'], (0, 160, 255)), (interior, (255, 0, 255)), (edge, (255, 255, 0)),
                      (~region, (0, 0, 0))):
        a[m] = 0.5 * a[m] + 0.5 * np.asarray(colour, dtype=np.float64)
    return Image.fromarray(a.clip(0, 255).astype(np.uint8))


def _delta_tile(y_after, y_base, region):
    from PIL import Image
    d = np.clip(np.abs(y_after - y_base) / np.maximum(y_base, 1e-6) * 10.0, 0.0, 1.0)
    rgb = np.stack([d, d * 0.6, 1.0 - d], -1) * region[..., None]
    return Image.fromarray((rgb * 255).astype(np.uint8))


def _crop(work, box=DOSE_CROP):
    im = _shot(work)
    return im.crop(box) if im is not None else None


def write_montage(rows, path):
    from PIL import Image
    if not rows:
        return None
    tw, th = rows[0][0].size
    sheet = Image.new('RGB', (max(len(r) for r in rows) * tw, len(rows) * th), (20, 20, 20))
    for i, r in enumerate(rows):
        for j, t in enumerate(r):
            sheet.paste(t, (j * tw, i * th))
    sheet.save(path, quality=88)
    return path


# ── main ──────────────────────────────────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(description='Volumetric clouds harness (baseline and comparison).')
    ap.add_argument('--baseline', action='store_true', help='capture into test-clouds/baseline')
    ap.add_argument('--reuse', action='store_true', help='re-analyse the captures that already have a manifest')
    ap.add_argument('--out', default=OUT)
    ap.add_argument('--poses', default=','.join(ref.DEFAULT_POSES))
    ap.add_argument('--dose-poses', default='sun', help='poses that also get steps32 / steps16 ("" = none)')
    ap.add_argument('--repeats', type=int, default=2, help='runs of every clouds capture (cost spread)')
    args = ap.parse_args()
    poses = [p for p in args.poses.split(',') if p]
    unknown = [p for p in poses if p not in ref.POSES]
    if unknown:
        raise SystemExit(f'unknown poses {unknown}; have {sorted(ref.POSES)}')
    dose_poses = [p for p in args.dose_poses.split(',') if p and p in poses]
    mode = 'baseline' if args.baseline else 'after'
    root = os.path.join(args.out, mode)
    os.makedirs(root, exist_ok=True)
    results = []
    report = {'task': TASK, 'mode': mode, 'captures': {}, 'metrics': {}, 'log': {}}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    report['config'] = {
        'frames': FRAMES, 'shots': [SHOT_FIRST, SHOT_LAST], 'dump_frame': DUMP_FRAME, 'timing_first': TIMING_FIRST,
        'region_fields': REGION_FIELDS, 'cvars': BASE_CVARS, 'clouds': ref.CLOUDS, 'sun_dir': list(ref.SUN_DIR),
        'min_elevation': MIN_ELEVATION, 'sun_exclude_hdr': SUN_EXCLUDE_HDR, 'sun_exclude_shot': SUN_EXCLUDE_SHOT,
        'tau_cloud': TAU_CLOUD, 'tau_clear': TAU_CLEAR, 'r_edge': R_EDGE, 'r_interior': R_INTERIOR,
        'sun_window': SUN_WINDOW, 'anti_window': ANTI_WINDOW, 'seg_len': SEG_LEN, 'seg_col_step': SEG_COL_STEP,
        'seg_min_cloud': SEG_MIN_CLOUD, 'hf_band': HF_BAND, 'mid_band': MID_BAND, 'ap_bands': AP_BANDS,
        'poses': {p: ref.POSES[p] for p in poses}, 'source_fingerprint': runner.source_fingerprint()}

    # The scene and project files on disk must be the generator's: a stale one measures something else.
    plan = capture_plan(poses, dose_poses, max(1, args.repeats))
    stale = set()
    for _, pose, variant, _, _, _ in plan:
        if not (os.path.isfile(ref.project_path(pose, variant)) and os.path.isfile(ref.scene_path(pose, variant))):
            raise SystemExit(f'{ref.project_path(pose, variant)} or its scene is missing: '
                             'run python scripts/rendering/cloud_reference.py first')
        for path, expected in ((ref.scene_path(pose, variant), ref.scene_json(pose, variant)),
                               (ref.project_path(pose, variant), ref.project_json(pose, variant))):
            with open(path, encoding='utf-8') as fh:
                if json.load(fh) != json.loads(json.dumps(expected)):
                    stale.add(os.path.basename(path))
    if stale:
        print(f'  WARNING: {len(stale)} scene / project file(s) differ from cloud_reference.py ({sorted(stale)[:3]}); '
              're-run the generator', flush=True)
    report['stale_scenes'] = sorted(stale)
    if not args.reuse:
        runner.require_exe()

    # ── captures ──
    recs, works, repeat_mans, failures = {}, {}, {}, []
    for name, pose, variant, shots, dump, repeat in plan:
        work = os.path.join(root, name)
        print(f'capture {name}', flush=True)
        rec = run_one(work, ref.project_path(pose, variant), shots, dump, args.reuse)
        why = validate(rec, variant, shots, dump)
        rec['completed'] = not why
        if why:
            failures.append(f'{name}: {"; ".join(why)}')
            print(f'    not completed: {"; ".join(why)}', flush=True)
        if repeat:
            if rec['completed']:
                repeat_mans.setdefault(pose, []).append(rec['manifest'])
        else:
            recs[(pose, variant)] = rec
            if rec['completed']:
                works.setdefault(pose, {})[variant] = work
            if variant != 'noclouds':
                report['log'][name] = log_facts(read_log(work))
        report['captures'][name] = {k: v for k, v in rec.items() if k != 'manifest'}
        report['captures'][name]['identity'] = runner.capture_identity(work)
    check('C1 every capture completed without device removal', not failures,
          f'{len(plan) - len(failures)}/{len(plan)} captures completed' + (f'; {failures}' if failures else ''))

    # ── metrics ──
    arrays = {}
    for pose in poses:
        if not all(k in works.get(pose, {}) for k in (None, 'noclouds')):
            report['metrics'][pose] = {'error': 'the clouds or noclouds capture did not complete'}
            continue
        try:
            m, arrays[pose] = analyse_pose(pose, works[pose], recs, repeat_mans.get(pose, ()))
        except (OSError, ValueError, KeyError, IndexError) as e:
            report['metrics'][pose] = {'error': f'analysis failed: {e!r}'}
            print(f'  {pose}: analysis failed: {e!r}', flush=True)
            continue
        report['metrics'][pose] = m
        c, mk, b, t = m['cost'], m['mask'], m['banding'], m['temporal']
        print(f'  {pose}: cloud passes {c["cloud_passes"]} = {fmt(c["cloud_ms"])} ms, frame {fmt(c["frame_gpu_ms"])} ms '
              f'(no clouds {fmt(c.get("frame_gpu_ms_noclouds"))}); cloud {fmt(mk["cloud_fraction"])}, clear '
              f'{fmt(mk["clear_fraction"])} of {mk["sky_px"]} sky px', flush=True)
        print(f'    banding: hf {fmt(b.get("hf_power"))} (control {fmt(b.get("control_hf_power"))}, hf/mid '
              f'{fmt(b.get("hf_over_mid"))}, {b["segments"]} segments); dose {b.get("dose_hf_power")} monotone '
              f'{b.get("dose_monotone")}; temporal CoV p95 {fmt((t.get("sky") or {}).get("cov_p95"))} over '
              f'{t.get("frames")} frames (control {fmt(((t.get("control") or {}).get("sky") or {}).get("cov_p95"))})',
              flush=True)
        if 'silver' in m:
            s = m['silver']
            print(f'    silver lining {s["window_deg"]} deg from the sun: edge {fmt(s["edge_mean"])} / interior '
                  f'{fmt(s["interior_mean"])} = {fmt(s["ratio"])} ({s["edge_px"]} / {s["interior_px"]} px; the sky '
                  f'behind them {fmt(s.get("sky_ratio"))})', flush=True)
        if 'aerial' in m:
            print('    aerial perspective: ' + ', '.join(
                f'{r["band_deg"]} deg contrast {fmt(r["contrast"])} chroma {fmt(r["chroma_distance"])}'
                for r in m['aerial']['bands']) + f'; falloff {fmt(m["aerial"]["falloff"])}', flush=True)
    for name, facts in report['log'].items():
        print(f'  log {name}: shape noise {facts["shape_noise_loaded"]}, detail noise {facts["detail_noise_loaded"]}, '
              f'CB sunDir {facts.get("cb_sun_dir")} ({fmt(facts.get("cb_sun_dir_error_deg"))} deg from SUN_DIR), '
              f'SH L0 {facts.get("sky_sh_l0_scaled")}', flush=True)

    problems = []
    for pose in poses:
        m = report['metrics'].get(pose, {})
        problems += [f'{pose}: {m["error"]}'] if 'error' in m else [f'{pose}.{p}' for p in numeric_problems(m)]
    check('C2 every metric is finite', not problems,
          f'{len(problems)} problem(s)' + (f': {problems[:12]}' if problems else ''))

    # ── comparison with the baseline (default run) and montage ──
    rows = []
    base_path = os.path.join(args.out, 'baseline', 'clouds.json')
    base = None
    if mode == 'after':
        check('C3 the baseline report exists', os.path.isfile(base_path), base_path)
        if os.path.isfile(base_path):
            with open(base_path, encoding='utf-8') as fh:
                base = json.load(fh)
    if base is not None:
        report['comparison'] = {'metrics': compare(base.get('metrics', {}), report['metrics']), 'image': {}}
        for pose in poses:
            bw = os.path.join(args.out, 'baseline', capture_name(pose))
            if pose not in arrays or not os.path.isdir(os.path.join(bw, 'region')):
                continue
            # One colour source for both sides: the screenshots when either run fell back to them (TAA off).
            after_shots = str(report['metrics'][pose].get('source', '')).startswith('screenshot')
            base_shots = str((base.get('metrics', {}).get(pose) or {}).get('source', '')).startswith('screenshot')
            try:
                vb = load_view(bw, base.get('captures', {}).get(capture_name(pose), {}).get('taa_on', True),
                               screenshot=after_shots or base_shots)
                ya = arrays[pose]['y']
                if base_shots and not after_shots:
                    shot = screenshot_linear(works[pose][None])
                    if shot is None:
                        raise ValueError(f'no screenshot of frame {DUMP_FRAME} in the after capture')
                    ya = luminance(shot)
            except (OSError, ValueError, IndexError) as e:
                report['comparison']['image'][pose] = {'error': repr(e)}
                continue
            delta = image_delta(ya, vb['y'], arrays[pose]['region'])
            delta['source'] = vb['source']
            report['comparison']['image'][pose] = delta
            print(f'  {pose}: after vs baseline image: mean |dY|/Y {fmt(delta.get("mean_rel"))}, p99 '
                  f'{fmt(delta.get("p99_rel"))}', flush=True)
            rows.append([_tile(_shot(bw), f'{pose}: baseline'),
                         _tile(_shot(works[pose][None]), f'{pose}: after'),
                         _tile(_delta_tile(ya, vb['y'], arrays[pose]['region']),
                               f'{pose}: |dY|/Y x10 (blue 0, orange >= 0.1)')])
        heads = ('cost.cloud_ms', 'cost.frame_gpu_ms', 'mask.cloud_fraction', 'banding.hf_power',
                 'temporal.sky.cov_p95', 'silver.ratio', 'aerial.falloff')
        for k, row in report['comparison']['metrics'].items():
            if k.split('.', 1)[-1] in heads:
                print(f'    {k}: {row["baseline"]:.6g} -> {row["after"]:.6g} (x{fmt(row["ratio"], ".4f")})', flush=True)
    else:
        for pose in poses:
            if pose not in arrays:
                continue
            m = report['metrics'][pose]
            rows.append([
                _tile(_shot(works[pose][None]), f'{pose}: clouds, cloud passes {fmt(m["cost"]["cloud_ms"])} ms'),
                _tile(_shot(works[pose]['noclouds']), f'{pose}: no clouds (control)'),
                _tile(_mask_overlay(works[pose][None], arrays[pose]),
                      f'{pose}: cloud {fmt(m["mask"]["cloud_fraction"], ".2f")} (edges yellow, interiors magenta), '
                      'clear blue')])
    if 'sun' in works and 'sun' in dose_poses:
        crops = [_tile(_crop(works['sun'][v]), f'sun, crop {DOSE_CROP}: {n} steps')
                 for v, n in DOSE_VARIANTS[::-1] if v in works['sun']]
        crops.append(_tile(_crop(works['sun'][None]), f'sun, crop: {ref.CLOUDS["primary_steps"]} steps ({mode})'))
        rows.append(crops)
    report['montage'] = write_montage(rows, os.path.join(root, 'clouds_montage.jpg'))
    if report['montage']:
        print(f'  montage: {report["montage"]}', flush=True)

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed', flush=True)
    report['checks'] = [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]
    with open(os.path.join(root, 'clouds.json'), 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1, default=str)
    print(f'  report: {os.path.join(root, "clouds.json")}', flush=True)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
