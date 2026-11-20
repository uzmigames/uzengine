"""Reflection probes (phase3a 3.37.0): the GPU validation of the probe layer on the demo scene
(scripts/rendering/reflection_probes_demo.py), items 2.1-2.4 of the rulebook task.

Every view is captured twice from the same scene: probes on, and probes disabled (`enabled: false`,
the probe-less render). The deferred lighting's region dump gives, per pixel, the linear-HDR split-sum
specular (`specular_indirect`), the active-path bits (bit 256 = a reflection probe contributed) and the
world position. The demo project pins the other reflection layers off (SSR, ray-traced reflections,
SDF reflections, environment MIS), so the specular is the probe / sky layer itself.

Gates:
  G1  room, not sky (2.1): on the chrome sphere 150 cm from the red wall (room-south and room-east
      views), the pixels whose parallax-corrected reflected ray hits the red (west) wall (the room box,
      analytic) are red - redness R - max(G, B) above half the wall's plateau - on >= 70 % with the
      probes on and <= 5 % with them off (the sky).
  G1b parallax: the measured red region's IoU with the authored-box prediction >= 0.75 in each view,
      >= 0.2 above the plain-direction lookup's IoU, and the best-fitting box scale among 0.75..1.5 is
      within [0.9, 1.1].
  (G1 and G1b were first pre-registered as the dominant channel over all three coloured walls; the
  diagnostics showed the capture leaves the walls the sun does not reach black - the capture shader is
  unshadowed N.L with no ambient - so those walls carry no colour. The superseded numbers are still
  reported under G1['dominant_channel'].)
  G2  outside every probe (2.2): the far sphere (1302 cm from the outdoor probe, radius 900) never takes
      the probe path and its specular_indirect is bit-identical with the probes on and off.
  G3  blend (2.3): along the chrome strip's centreline (strip view, 10 cm bins of specular_indirect
      luminance), (a) the step across each visible influence / fade boundary of both probes (>= 3
      visible) is <= 1.5 x the largest other step within 100 cm of it (a hard edge is the outlier step
      at the boundary; a smoothstep fade has its smallest steps there), and (b) no step anywhere along
      the visible strip exceeds 15 % of the probes-on profile's range (2 x the most a smoothstep over the
      narrowest fade band, 200 cm, moves per 10 cm bin: 1.5 x 10 / 200 = 7.5 %).
      (First pre-registered as <= 3 x the median step within 100 cm, which a smooth onset next to a
      flat stretch fails; that ratio is still reported. The strip profile is what exposed the box
      projection of receivers outside the proxy box, fixed in ReflectionProbeLookup.)
  G4  rough vs smooth (2.4): the same sphere at roughness 0.05 and 0.5 (room-south-smooth vs
      room-south): mean normalised luminance gradient |grad L| / L over the sphere's interior,
      smooth / rough >= 2.
  G5  sanity: no NaN / Inf in specular_indirect; the probe bit on >= 95 % of the room sphere's pixels
      with the probes on and on none with them off.
Reported (no gate): the capture's radiance against the frame's own lighting of the same wall points
(the mirror sphere's specular_indirect / F0 over the radiance of the wall pixel its parallax ray hits).

The probes-on captures run with `r.ReflectionProbes=1` (item 5.1: the CVar defaults to 0, probes off);
the `-off` captures (probes `enabled: false`) run at the default.

Other invocations:
  --exe PATH --off-only    the probes-off captures only, with another build (the pristine baseline)
  --exe PATH --identity    every view (probes authored AND disabled) at the default r.ReflectionProbes=0:
                           the OFF byte-identity set, captured once per build
  --compare-off A B        the probes-off captures of two output dirs agree byte for byte (screenshot
                           PNG and the dump's lighting and TAA records)
  --compare-identity A B   as --compare-off over every view of two --identity output dirs
  --shipped                capture a shipped scene that authors a reflection_probe (demo.uzscene)
  --compare-shipped A B    the difference between two builds' shipped-scene captures (reported)

Usage: python scripts/rendering/test_reflection_probes.py [--out DIR] [--exe PATH] [--off-only] [--reuse]
"""
import argparse
import json
import math
import os
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regiondump  # noqa: E402
import runner  # noqa: E402
import reflection_probes_demo as demo  # noqa: E402

REPO = runner.REPO
FRAMES = 91
SHOT = FRAMES - 1
LUMA = np.array([0.2126, 0.7152, 0.0722])
BASE_CVARS = {'r.TechPanel.Show': 0, 'r.Toolbar.Show': 0}
PROBES_ON_CVARS = {'r.ReflectionProbes': 1}   # the opt-in (default 0 = off)
ROOM_LO = np.array(demo.ROOM_LO)
ROOM_HI = np.array(demo.ROOM_HI)
PROBE_ROOM = np.array(demo.PROBES['ProbeRoom']['position'])
# Exit face of the room box -> dominant channel of that wall's colour (0 R, 1 G, 2 B; -1 not a coloured wall).
# Faces: 0 west (-x, red), 1 east (+x, blue), 2 south opening (-y), 3 north (+y, green), 4 floor, 5 ceiling.
FACE_CHANNEL = np.array([0, 2, -1, 1, -1, -1])
FACE_NAME = ['west (red)', 'east (blue)', 'south opening', 'north (green)', 'floor', 'ceiling']
ROOM_BOX_HALF = np.array(demo.PROBES['ProbeRoom']['box_extents'])
BOX_SCALES = (0.75, 0.9, 1.0, 1.1, 1.25, 1.5)
PROBE_BIT = 256
SHIPPED_SCENE = 'scenes/demo.uzscene'


def views():
    """(capture name, project file stem) of every validation capture."""
    out = []
    for camera in demo.CAMERAS:
        out.append((camera, demo.validation_scene(camera, True)))
        out.append((camera + '-off', demo.validation_scene(camera, False)))
    for variant in demo.VARIANTS:
        out.append((variant, demo.validation_scene(variant)))
    return out


def capture(out, name, project, reuse, extra_cvars=None):
    work = os.path.join(out, 'runs', name)
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return work
    for attempt in range(2):
        try:
            res = runner.run_capture(work, project, FRAMES, regions=[('FRAME', 0, 0, demo.W, demo.H)],
                                     region_fields='components', region_first=SHOT, region_last=SHOT,
                                     cvars={**BASE_CVARS, **(extra_cvars or {})}, screenshot_frames=str(SHOT),
                                     timing_first=40, timeout=180)
            break
        except RuntimeError as error:   # a TDR is re-run once; anything else is a result
            if '0x887A0005' not in str(error) or attempt == 1:
                raise
            print(f'  {name}: device removed, re-running once', flush=True)
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    print(f'  captured {name}', flush=True)
    return work


class Shot:
    """One capture's dump at frame SHOT."""

    def __init__(self, work, allow_taa_missing=False):
        paths = regiondump.sequence_paths(os.path.join(work, 'region'), SHOT, SHOT)
        if not paths:
            raise SystemExit(f'{work}: no region dump for frame {SHOT}')
        self.work = work
        self.fr = regiondump.load_frame(paths[0][1])
        invalid = regiondump.FLAGS_INVALID & ~(regiondump.FLAG_TAA_MISSING if allow_taa_missing else 0)
        if self.fr.flags & invalid:
            raise SystemExit(f'{work}: invalid frame stamps {regiondump.flag_names(self.fr.flags)}')
        self.taa = not (self.fr.flags & regiondump.FLAG_TAA_MISSING)
        self.rv = self.fr.rect('FRAME')
        self.P = self.fr.world_positions('FRAME')
        self.geo = self.rv.geometry_mask()
        self.spec_raw = self.rv.channel('specular_indirect')
        self.spec = self.spec_raw[..., :3].astype(np.float64)
        self.bits = self.rv.path_mask()
        self.cam = self.fr.camera_pos.astype(np.float64)

    def sphere(self, name, tol=2.0):
        c = np.array(demo.SPHERES[name][0])
        return self.geo & (np.abs(np.linalg.norm(self.P - c, axis=-1) - demo.SPHERE_RADIUS) < tol)

    def radiance(self):
        """Linear radiance of the lit surface (the deferred pass's components, before TAA)."""
        total = np.zeros(self.spec.shape)
        for name in ('direct_light', 'diffuse_indirect', 'specular_indirect'):
            total += self.rv.channel(name)[..., :3]
        total += self.rv.channel('emissive_and_mis')[..., :3]
        return total


def wall_hit(origin, d):
    """Exit face (FACE_NAME index) and distance of rays (origin, d) leaving the room box."""
    d = np.where(np.abs(d) < 1e-9, 1e-9, d)
    t_far = np.maximum((ROOM_LO - origin) / d, (ROOM_HI - origin) / d)
    axis = np.argmin(t_far, axis=-1)
    t = np.take_along_axis(t_far, axis[..., None], -1)[..., 0]
    positive = np.take_along_axis(d, axis[..., None], -1)[..., 0] > 0
    return axis * 2 + positive.astype(int), t


def mirror_rays(shot, name, m=None):
    """Sphere pixels (mask m, default all of the sphere's), their reflected rays and how squarely they
    face the camera."""
    m = shot.sphere(name) if m is None else m
    P = shot.P[m]
    n = (P - np.array(demo.SPHERES[name][0])) / demo.SPHERE_RADIUS
    v = P - shot.cam
    v /= np.linalg.norm(v, axis=-1, keepdims=True)
    vn = (v * n).sum(-1, keepdims=True)
    return m, P, v - 2.0 * vn * n, -vn[..., 0]


def box_exit(origin, d, half):
    """Distance along d from origin (inside) to the exit of the box PROBE_ROOM +- half."""
    d = np.where(np.abs(d) < 1e-9, 1e-9, d)
    return np.maximum((PROBE_ROOM - half - origin) / d, (PROBE_ROOM + half - origin) / d).min(axis=-1)


def red_wall_view(on, off):
    """G1 / G1b on the lit red wall: the half-max red region against the box-projected predictions."""
    m, P, r, facing = mirror_rays(on, 'room-mirror', on.sphere('room-mirror') & off.sphere('room-mirror'))
    keep = facing > 0.2

    def predicted_red(scale):
        d = r if scale is None else P + r * box_exit(P, r, ROOM_BOX_HALF * scale)[..., None] - PROBE_ROOM
        return (wall_hit(PROBE_ROOM, d)[0] == 0) & keep

    def redness(shot):
        rgb = shot.spec[m]
        return rgb[:, 0] - np.maximum(rgb[:, 1], rgb[:, 2])

    def iou(a, b):
        return float((a & b).sum() / max((a | b).sum(), 1))

    pred = predicted_red(1.0)
    plateau = float(np.percentile(redness(on)[pred], 90)) if pred.any() else 0.0
    red_on = (redness(on) > 0.5 * plateau) & keep
    red_off = (redness(off) > 0.5 * plateau) & keep
    fits = {s: iou(red_on, predicted_red(s)) for s in BOX_SCALES}
    return {'pixels': int(keep.sum()), 'predicted_red_pixels': int(pred.sum()), 'red_plateau': plateau,
            'cover_on': float(red_on[pred].mean()) if pred.any() else None,
            'cover_off': float(red_off[pred].mean()) if pred.any() else None,
            'iou_authored_box': fits[1.0], 'iou_plain_direction': iou(red_on, predicted_red(None)),
            'iou_by_box_scale': fits, 'best_box_scale': max(fits, key=fits.get)}


def g1_view(on, off):
    m, P, r, facing = mirror_rays(on, 'room-mirror', on.sphere('room-mirror') & off.sphere('room-mirror'))
    face_par, t_par = wall_hit(P, r)
    face_naive, _ = wall_hit(PROBE_ROOM, r)
    exp_par, exp_naive = FACE_CHANNEL[face_par], FACE_CHANNEL[face_naive]
    keep = (facing > 0.2) & (exp_par >= 0)
    meas_on = np.argmax(on.spec[m], axis=-1)
    meas_off = np.argmax(off.spec[m], axis=-1)
    res = {'pixels': int(m.sum()), 'wall_pixels': int(keep.sum()),
           'agree_on': float((meas_on[keep] == exp_par[keep]).mean()) if keep.any() else None}
    rg = keep & (exp_par <= 1)                      # red and green walls: the sky is neither
    res['agree_off_red_green'] = float((meas_off[rg] == exp_par[rg]).mean()) if rg.any() else None
    per = {}
    for face in (0, 1, 3):
        f = keep & (face_par == face)
        if f.any():
            per[FACE_NAME[face]] = {'pixels': int(f.sum()), 'agree_on': float((meas_on[f] == exp_par[f]).mean()),
                                    'agree_off': float((meas_off[f] == exp_par[f]).mean())}
    res['per_wall'] = per
    differ = keep & (exp_naive >= 0) & (exp_naive != exp_par)
    res['parallax'] = {'pixels': int(differ.sum()),
                       'agree_parallax': float((meas_on[differ] == exp_par[differ]).mean()) if differ.any() else None,
                       'agree_plain_direction': float((meas_on[differ] == exp_naive[differ]).mean()) if differ.any() else None}
    # Reported: the capture's radiance against the frame's own lighting of the same wall points.
    hit = P + r * t_par[..., None]
    px = on.fr.project(hit)
    ix, iy = np.rint(px[..., 0]).astype(int), np.rint(px[..., 1]).astype(int)
    inside = keep & (ix >= 0) & (ix < demo.W) & (iy >= 0) & (iy < demo.H)
    ratios = {}
    if inside.any():
        rad = on.radiance()
        seen = np.zeros(inside.shape, dtype=bool)
        seen[inside] = np.linalg.norm(on.P[iy[inside], ix[inside]] - hit[inside], axis=-1) < 15.0
        for face in (0, 1, 3):
            f = seen & (face_par == face)
            if f.sum() >= 20:
                refl = on.spec[m][f] @ LUMA / 0.95
                wall = rad[iy[f], ix[f]] @ LUMA
                ratios[FACE_NAME[face]] = {'pixels': int(f.sum()),
                                           'median_capture_over_frame': float(np.median(refl / np.maximum(wall, 1e-6)))}
    res['capture_vs_frame_radiance'] = ratios
    return res


def g2(on, off):
    m = on.sphere('far') & off.sphere('far')
    a, b = on.spec_raw[m], off.spec_raw[m]
    out = {'pixels': int(m.sum()), 'probe_bit_pixels': int(((on.bits[m] & PROBE_BIT) != 0).sum()),
           'bit_identical': bool(np.array_equal(a.view(np.uint32), b.view(np.uint32))),
           'max_abs_diff': float(np.abs(a.astype(np.float64) - b.astype(np.float64)).max()) if m.any() else None}
    o = on.sphere('outdoor') & off.sphere('outdoor')
    if o.any():   # control: the outdoor sphere sits inside the outdoor probe
        out['outdoor_control'] = {'pixels': int(o.sum()), 'probe_bit_fraction': float(((on.bits[o] & PROBE_BIT) != 0).mean()),
                                  'mean_rel_change': float(np.abs(on.spec[o] @ LUMA - off.spec[o] @ LUMA).mean()
                                                           / max((off.spec[o] @ LUMA).mean(), 1e-6))}
    return out


def strip_boundaries(z=10.0):
    """(label, y) where the strip centreline (x = 0, z = 10) crosses a probe's fade start or influence edge."""
    out = []
    for name, p in demo.PROBES.items():
        c = np.array(p['position'])
        dz = c[2] - z
        for label, rad in (('fade start', max(p['influence_radius'] - p['blend_distance'], 0.0)),
                           ('influence edge', p['influence_radius'])):
            if rad > abs(dz):
                h = math.sqrt(rad * rad - dz * dz)
                out += [(f'{name} {label}', c[1] - h), (f'{name} {label}', c[1] + h)]
    return out


def strip_profile(shot, bin_cm=10.0):
    # Within |x| < 60 only the strip's top (z = 10) is visible; 5 cm absorbs the reconstruction error of
    # the world position at the strip camera's 25-30 m view distance.
    top = shot.geo & (np.abs(shot.P[..., 0]) < 60.0) & (np.abs(shot.P[..., 2] - 10.0) < 5.0)
    y = shot.P[..., 1][top]
    L = shot.spec[top] @ LUMA
    edges = np.arange(-2800.0, 300.0 + bin_cm, bin_cm)
    idx = np.digitize(y, edges) - 1
    prof = np.full(len(edges) - 1, np.nan)
    for i in np.unique(idx):
        if 0 <= i < len(prof):
            sel = L[idx == i]
            if sel.size >= 3:
                prof[i] = np.median(sel)
    return edges, prof


def g3(on, off):
    edges, prof = strip_profile(on)
    _, prof_off = strip_profile(off)
    centres = 0.5 * (edges[:-1] + edges[1:])
    res = {'boundaries': []}
    for label, yb in strip_boundaries():
        k = int(np.floor((yb - edges[0]) / (edges[1] - edges[0])))
        lo, hi = k - 10, k + 11
        if lo < 0 or hi > len(prof) or np.isnan(prof[lo:hi]).any():
            res['boundaries'].append({'boundary': label, 'y': round(yb, 1), 'visible': False})
            continue
        # The step across the boundary: the bins either side of y_b.
        a = k if centres[k] > yb else k + 1
        step = abs(prof[a] - prof[a - 1])
        local = np.abs(np.diff(prof[lo:hi]))
        local = np.delete(local, (a - 1) - lo)
        floor = 1e-4 * float(np.nanmean(prof[lo:hi]))
        res['boundaries'].append({'boundary': label, 'y': round(yb, 1), 'visible': True, 'step': float(step),
                                  'max_local_step': float(local.max()),
                                  'ratio_to_max': float(step / max(float(local.max()), floor)),
                                  'median_local_step': float(np.median(local)),
                                  'ratio_to_median': float(step / max(float(np.median(local)), floor))})
    # (b) Anywhere along the visible strip: the largest step against the profile's range.
    valid = ~np.isnan(prof)
    steps = np.abs(np.diff(prof))
    both = valid[:-1] & valid[1:]
    if both.any():
        rng = float(np.nanmax(prof) - np.nanmin(prof))
        i = int(np.argmax(np.where(both, steps, -1.0)))
        res['max_step'] = {'step': float(steps[i]), 'y': float(edges[i + 1]), 'range': rng,
                           'fraction_of_range': float(steps[i] / max(rng, 1e-9))}
    res['profile'] = {'y': centres.tolist(), 'on': [None if np.isnan(v) else float(v) for v in prof],
                      'off': [None if np.isnan(v) else float(v) for v in prof_off]}
    return res


def gradient_energy(shot, name):
    L = shot.spec @ LUMA
    m = shot.sphere(name)
    _, _, _, facing = mirror_rays(shot, name)
    face_img = np.zeros(m.shape)
    face_img[m] = facing
    inner = m.copy()
    for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
        inner &= np.roll(np.roll(m, dy, 0), dx, 1)
    inner &= face_img > 0.3
    gy, gx = np.gradient(L)
    g = np.hypot(gx, gy) / np.maximum(L, 1e-6)
    return float(g[inner].mean()), int(inner.sum())


def g4(rough, smooth):
    er, nr = gradient_energy(rough, 'room-rough')
    es, ns = gradient_energy(smooth, 'room-rough')
    return {'roughness_0.5': {'grad_over_L': er, 'pixels': nr}, 'roughness_0.05': {'grad_over_L': es, 'pixels': ns},
            'ratio_smooth_over_rough': es / max(er, 1e-9)}


def g5(shots):
    bad = {k: int((~np.isfinite(s.spec_raw)).sum()) for k, s in shots.items()}
    on, off = shots['room-south'], shots['room-south-off']
    m_on, m_off = on.sphere('room-mirror'), off.sphere('room-mirror')
    return {'non_finite': bad,
            'probe_bit_on': float(((on.bits[m_on] & PROBE_BIT) != 0).mean()),
            'probe_bit_off': float(((off.bits[m_off] & PROBE_BIT) != 0).mean())}


def montage(out, names):
    from PIL import Image, ImageDraw
    tiles = []
    for name in names:
        row = []
        for suffix in ('', '-off'):
            d = os.path.join(out, 'runs', name + suffix, 'frames')
            files = sorted(f for f in os.listdir(d) if f.endswith('.png')) if os.path.isdir(d) else []
            img = Image.open(os.path.join(d, files[-1])).convert('RGB').resize((640, 360)) if files else Image.new('RGB', (640, 360))
            label = f'{name}{suffix or " (probes on)"}' + ('' if files else ' (not captured)')
            ImageDraw.Draw(img).text((8, 8), label, fill=(255, 255, 0))
            row.append(img)
        tiles.append(row)
    sheet = Image.new('RGB', (1280, 360 * len(tiles)))
    for i, row in enumerate(tiles):
        for j, img in enumerate(row):
            sheet.paste(img, (640 * j, 360 * i))
    path = os.path.join(out, 'montage.png')
    sheet.save(path)
    return path


def profile_plot(out, g3res):
    p = g3res['profile']
    y = np.array(p['y'])
    on = np.array([np.nan if v is None else v for v in p['on']])
    off = np.array([np.nan if v is None else v for v in p['off']])
    path = os.path.join(out, 'strip_profile.png')
    try:
        import matplotlib
        matplotlib.use('Agg')
        import matplotlib.pyplot as plt
    except ImportError:   # PIL line plot: probes on (orange), off (blue), boundaries (grey)
        from PIL import Image, ImageDraw
        W, H, pad = 1200, 420, 40
        keep = ~np.isnan(on) | ~np.isnan(off)
        if not keep.any():
            return None
        x0, x1 = y[keep].min(), y[keep].max()
        top = float(np.nanmax(np.concatenate([on[keep], off[keep]])))
        img = Image.new('RGB', (W, H), (255, 255, 255))
        dr = ImageDraw.Draw(img)
        sx = lambda v: pad + (v - x0) / max(x1 - x0, 1e-6) * (W - 2 * pad)
        sy = lambda v: H - pad - v / max(top, 1e-6) * (H - 2 * pad)
        for b in g3res['boundaries']:
            if x0 <= b['y'] <= x1:
                dr.line([(sx(b['y']), pad), (sx(b['y']), H - pad)], fill=(170, 170, 170))
        for series, colour in ((off, (40, 90, 200)), (on, (230, 120, 20))):
            pts = [(sx(a), sy(b)) for a, b in zip(y, series) if not np.isnan(b)]
            if len(pts) > 1:
                dr.line(pts, fill=colour, width=2)
        dr.text((pad, 8), f'strip centreline: specular_indirect luminance vs y ({x0:.0f}..{x1:.0f} cm, max {top:.3f}); '
                          'orange probes on, blue off, grey fade starts / influence edges', fill=(0, 0, 0))
        img.save(path)
        return path
    fig, ax = plt.subplots(figsize=(11, 4))
    ax.plot(y, on, label='probes on')
    ax.plot(y, off, label='probes off (sky IBL)')
    for b in g3res['boundaries']:
        ax.axvline(b['y'], color='grey', lw=0.6, ls='--')
    ax.set_xlabel('strip centreline y (cm)')
    ax.set_ylabel('specular_indirect luminance (linear)')
    ax.set_title('Chrome strip: probe blend along y (dashed: fade starts / influence edges)')
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=110)
    plt.close(fig)
    return path


def compare_dirs(a, b, names):
    out = {}
    for name in names:
        wa, wb = os.path.join(a, 'runs', name), os.path.join(b, 'runs', name)
        if not (os.path.isdir(wa) and os.path.isdir(wb)):
            out[name] = 'missing'
            continue
        fa = sorted(f for f in os.listdir(os.path.join(wa, 'frames')) if f.endswith('.png'))
        fb = sorted(f for f in os.listdir(os.path.join(wb, 'frames')) if f.endswith('.png'))
        png_same = bool(fa and fb and open(os.path.join(wa, 'frames', fa[-1]), 'rb').read()
                        == open(os.path.join(wb, 'frames', fb[-1]), 'rb').read())
        sa, sb = Shot(wa, allow_taa_missing=True), Shot(wb, allow_taa_missing=True)
        recs = {}
        channels = (('direct_light', 'diffuse_indirect', 'specular_indirect', 'emissive_and_mis')
                    + (('taa_output',) if sa.taa and sb.taa else ()))
        for ch in channels:
            x, y = sa.rv.channel(ch), sb.rv.channel(ch)
            recs[ch] = int((x.view(np.uint32) != y.view(np.uint32)).any(axis=-1).sum())
        out[name] = {'png_identical': png_same, 'differing_pixels_per_record': recs}
    return out


def shipped_project(out):
    proj = os.path.join(out, 'shipped-demo.uzproject')
    content = os.path.relpath(os.path.join(REPO, 'sample', 'content'), out).replace('\\', '/')
    with open(proj, 'w', encoding='utf-8') as fh:
        json.dump({'name': 'Shipped scene with a reflection probe (demo.uzscene)', 'version': '1.0',
                   'content_root': content, 'default_scene': SHIPPED_SCENE,
                   'window': {'width': demo.W, 'height': demo.H}, 'cvars': dict(BASE_CVARS)}, fh, indent=2)
    return proj


def screenshot_linear(work):
    from PIL import Image
    d = os.path.join(work, 'frames')
    files = sorted(f for f in os.listdir(d) if f.endswith('.png'))
    a = np.asarray(Image.open(os.path.join(d, files[-1])).convert('RGB')).astype(np.float64) / 255.0
    return np.where(a <= 0.04045, a / 12.92, ((a + 0.055) / 1.055) ** 2.4)


def compare_shipped(a, b):
    wa, wb = os.path.join(a, 'runs', 'shipped'), os.path.join(b, 'runs', 'shipped')
    sa, sb = Shot(wa, allow_taa_missing=True), Shot(wb, allow_taa_missing=True)
    out = {}
    for ch, x, y in (('specular_indirect', sa.spec @ LUMA, sb.spec @ LUMA),
                     ('final image (linearised PNG)', screenshot_linear(wa) @ LUMA, screenshot_linear(wb) @ LUMA)):
        d = np.abs(x - y)
        out[ch] = {'pixels_changed': int((d > 1e-6).sum()), 'fraction_changed': float((d > 1e-6).mean()),
                   'mean_luma_a': float(x.mean()), 'mean_luma_b': float(y.mean()),
                   'p99_abs_diff': float(np.percentile(d, 99)), 'max_abs_diff': float(d.max())}
    out['probe_bit_fraction_a'] = float(((sa.bits & PROBE_BIT) != 0).mean())
    out['probe_bit_fraction_b'] = float(((sb.bits & PROBE_BIT) != 0).mean())
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'reflection-probes'))
    ap.add_argument('--exe', default=None, help='engine binary (default runtime/build/bin/uzengine.exe)')
    ap.add_argument('--off-only', action='store_true')
    ap.add_argument('--reuse', action='store_true')
    ap.add_argument('--shipped', action='store_true')
    ap.add_argument('--identity', action='store_true',
                    help='capture every view at the default r.ReflectionProbes=0 (the OFF byte-identity set)')
    ap.add_argument('--compare-off', nargs=2, metavar=('A', 'B'))
    ap.add_argument('--compare-identity', nargs=2, metavar=('A', 'B'))
    ap.add_argument('--compare-shipped', nargs=2, metavar=('A', 'B'))
    ap.add_argument('--cvar', action='append', default=[], metavar='NAME=VALUE',
                    help='extra CVar for every capture (e.g. r.TAA=0 for an identity run without TAA)')
    args = ap.parse_args()
    extra = dict(c.split('=', 1) for c in args.cvar)

    if args.compare_off or args.compare_identity:
        pair = args.compare_off or args.compare_identity
        names = [n for n, _ in views() if args.compare_identity or n.endswith('-off')]
        res = compare_dirs(pair[0], pair[1], names)
        ok = all(isinstance(v, dict) and v['png_identical'] and not any(v['differing_pixels_per_record'].values())
                 for v in res.values())
        print(json.dumps(res, indent=1))
        what = 'every view (probes authored and disabled) at r.ReflectionProbes=0' if args.compare_identity else 'probes-off captures'
        print(f'[{"PASS" if ok else "FAIL"}] OFF identity: {what} byte-identical across the two builds')
        return 0 if ok else 1
    if args.compare_shipped:
        res = compare_shipped(*args.compare_shipped)
        print(json.dumps(res, indent=1))
        return 0

    if args.exe:
        runner.EXE = os.path.abspath(args.exe)
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    if args.shipped:
        capture(args.out, 'shipped', shipped_project(args.out), args.reuse, {**PROBES_ON_CVARS, **extra})
        return 0
    vdir = os.path.join(args.out, 'validation')
    demo.generate(vdir)
    works = {}
    for name, stem in views():
        if args.off_only and not name.endswith('-off'):
            continue
        on = not name.endswith('-off') and not args.identity
        works[name] = capture(args.out, name, os.path.join(vdir, f'{stem}.uzproject'), args.reuse,
                              {**(PROBES_ON_CVARS if on else {}), **extra})
    if args.off_only or args.identity:
        return 0

    shots = {k: Shot(w) for k, w in works.items()}
    report, results = {}, []

    def check(gate, ok, detail):
        results.append((gate, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {gate}: {detail}', flush=True)

    rv = {v: red_wall_view(shots[v], shots[v + '-off']) for v in ('room-south', 'room-east')}
    report['G1'] = {'red_wall': rv,
                    'dominant_channel': {v: g1_view(shots[v], shots[v + '-off']) for v in ('room-south', 'room-east')}}
    check('G1 room, not sky', all(x['cover_on'] is not None and x['cover_on'] >= 0.7 and x['cover_off'] <= 0.05
                                  for x in rv.values()),
          '; '.join(f'{v}: red-wall pixels red with probes on {x["cover_on"]:.3f} (>= 0.7), off {x["cover_off"]:.3f} '
                    f'(<= 0.05) over {x["predicted_red_pixels"]} px' for v, x in rv.items()))
    check('G1b parallax', all(x['iou_authored_box'] >= 0.75 and x['iou_authored_box'] - x['iou_plain_direction'] >= 0.2
                              and 0.9 <= x['best_box_scale'] <= 1.1 for x in rv.values()),
          '; '.join(f'{v}: IoU authored box {x["iou_authored_box"]:.3f} (>= 0.75), plain direction '
                    f'{x["iou_plain_direction"]:.3f}, best box scale {x["best_box_scale"]}' for v, x in rv.items()))
    report['G2'] = g2r = g2(shots['far'], shots['far-off'])
    check('G2 outside every probe -> sky IBL', g2r['pixels'] > 0 and g2r['probe_bit_pixels'] == 0 and g2r['bit_identical'],
          f'far sphere {g2r["pixels"]} px, probe bit on {g2r["probe_bit_pixels"]}, specular bit-identical on/off '
          f'{g2r["bit_identical"]} (max |diff| {g2r["max_abs_diff"]}); control {g2r.get("outdoor_control")}')
    report['G3'] = g3r = g3(shots['strip'], shots['strip-off'])
    vis = [b for b in g3r['boundaries'] if b['visible']]
    ms = g3r.get('max_step', {})
    check('G3 smooth blend', len(vis) >= 3 and all(b['ratio_to_max'] <= 1.5 for b in vis)
          and ms.get('fraction_of_range', 1.0) <= 0.15,
          f'{len(vis)} visible boundaries, step / largest local step '
          f'{[(b["boundary"], b["y"], round(b["ratio_to_max"], 2)) for b in vis]} (<= 1.5); largest step along '
          f'the strip {ms.get("fraction_of_range", float("nan")):.3f} of the range at y {ms.get("y")} (<= 0.15)')
    report['G4'] = g4r = g4(shots['room-south'], shots['room-south-smooth'])
    check('G4 rough blurred, smooth sharp', g4r['ratio_smooth_over_rough'] >= 2.0,
          f'|grad L| / L roughness 0.05 {g4r["roughness_0.05"]["grad_over_L"]:.4f} vs 0.5 '
          f'{g4r["roughness_0.5"]["grad_over_L"]:.4f}: ratio {g4r["ratio_smooth_over_rough"]:.2f} (>= 2)')
    report['G5'] = g5r = g5(shots)
    check('G5 sanity', not any(g5r['non_finite'].values()) and g5r['probe_bit_on'] >= 0.95 and g5r['probe_bit_off'] == 0.0,
          f'non-finite {sum(g5r["non_finite"].values())}, probe bit on room sphere: on {g5r["probe_bit_on"]:.3f}, '
          f'off {g5r["probe_bit_off"]:.3f}')
    # G6 capture lit like the frame (item 1.10): through the mirror sphere, the capture's radiance of a wall
    # point over the frame's own lighting of it (specular_indirect / F0 vs the wall pixel the parallax ray
    # hits). The placeholder capture gave 8.6-9 x on the sun-facing red wall and 0.04-0.11 x on the others.
    # The capture omits the frame's screen-space terms (contact shadows, GI, SSR; all off in this project),
    # samples a 128-texel face and a prefiltered mip, and the comparison reads a 15 cm neighbourhood, so
    # the band is [0.7, 1.4] on every wall with >= 20 measured pixels, in both room views.
    fid = {v: report['G1']['dominant_channel'][v]['capture_vs_frame_radiance'] for v in ('room-south', 'room-east')}
    fid_vals = [w['median_capture_over_frame'] for walls in fid.values() for w in walls.values()]
    report['G6'] = fid
    check('G6 capture lit like the frame', len(fid_vals) >= 3 and all(0.7 <= x <= 1.4 for x in fid_vals),
          '; '.join(f'{v}: ' + ', '.join(f'{wall} {w["median_capture_over_frame"]:.2f}x ({w["pixels"]} px)'
                                        for wall, w in walls.items()) for v, walls in fid.items())
          + ' (each within [0.7, 1.4])')
    # Linear-HDR numbers per sphere and view (mean specular_indirect, probes on / off).
    spheres = {}
    for camera in demo.CAMERAS:
        on, off = shots[camera], shots[camera + '-off']
        for s in demo.SPHERES:
            m = on.sphere(s) & off.sphere(s)
            if m.sum() >= 50:
                spheres[f'{camera}/{s}'] = {'pixels': int(m.sum()), 'on_rgb': np.round(on.spec[m].mean(0), 4).tolist(),
                                            'off_rgb': np.round(off.spec[m].mean(0), 4).tolist(),
                                            'probe_bit': float(((on.bits[m] & PROBE_BIT) != 0).mean())}
    report['spheres'] = spheres
    report['montage'] = montage(args.out, list(demo.CAMERAS) + list(demo.VARIANTS))
    report['strip_profile_plot'] = profile_plot(args.out, g3r)
    report['checks'] = [{'gate': g, 'pass': ok, 'detail': d} for g, ok, d in results]
    with open(os.path.join(args.out, 'report.json'), 'w', encoding='utf-8') as fh:
        json.dump(report, fh, indent=1, default=str)
    failed = [g for g, ok, _ in results if not ok]
    print(f'{len(results) - len(failed)}/{len(results)} gates pass' + (f'; failed: {failed}' if failed else ''))
    return 0 if not failed else 1


if __name__ == '__main__':
    raise SystemExit(main())
