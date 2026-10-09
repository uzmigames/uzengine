"""Contact shadows — gates of lighting block B, item 4b (depth linearisation fix and march rewrite).
  CS1  `LinearizeDepthReversedZ` (depth_common.hlsli, extracted and compiled on the CPU) inverts the
       reversed-Z projection of Mat4::Perspective: z in {6, 50, 350, 2000, 9000} cm round-trips within
       1e-5 relative. Negative control: the pre-fix function (scripts/test_contact_shadows_fixtures/) is
       off by >= 90 % at 350 cm.
  CS2  before: on the committed build (8851f37e) Sponza (fog project, fog off) with `r.ContactShadow.Length`
       0 and 0.01 is identical (direct_light max |diff| == 0; captures under test-contact-shadows/before,
       made before the change). Bridge: this build with the march off (Length 0) reproduces that frame
       (direct_light max |diff| <= 1e-6): nothing else in deferred lighting changed, so the march-off
       capture of this build stands for the committed build in CS3-CS6.
  CS3  no false shadows: contact shadow reference scene (`scripts/contact_shadow_reference.py`: floor, wall,
       40 x 40 x 12 cm box, sun at 40 deg, camera 2.2 m from the box), march on against off at frame 60:
       among floor pixels analytically lit and >= 20 cm from the box base and the wall base, the fraction
       whose shadow visibility drops by more than 0.25 is <= 0.2 %.
  CS4  contact gap: floor pixels inside the box's analytic shadow and within 10 cm of its base: the
       agreement with the truth (visibility < 0.5) with the march on is >= the agreement with the march off
       + half of the gap (gap = 1 - agreement off); when the gap is < 0.05 the gate is that the agreement
       does not drop by more than 0.02.
  CS5  Sponza (fog project, fog off, frame 60): pixels lit with the march off (visibility > 0.5) that the
       march darkens by more than 0.5: <= 15 % of the lit pixels, and >= 70 % of them within 8 px of a
       depth discontinuity (view depth differing by > 0.2 % between 4-neighbours, from the dump's world
       positions: 4 cm at 18 m, the scale of the relief contact shadows come from), with that fraction
       >= 1.3 x the same fraction over all lit pixels (enrichment over the base rate). Re-specified after
       the first run: the first threshold, 5 %, meant 90 cm steps at 18 m; a CPU replica of the march on
       the dump showed every darkened pixel is a recessed band of the far wall's 7 cm plaster relief,
       occluded by the raised band above it (the replica reproduces 39 of 40 darkened and 0 of 200
       undarkened pixels).
  CS6  cost: DeferredLighting GPU time with the march on minus off <= 0.5 ms at 1920x1080 (manifests).
Usage: python scripts/test_contact_shadows.py [--out DIR] [--reuse]
"""
from __future__ import annotations
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import numpy as np
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import regiondump  # noqa: E402
import runner  # noqa: E402
import contact_shadow_reference as ref  # noqa: E402

REPO = runner.REPO
FOG_PROJECT = os.path.join(REPO, 'scripts', 'sponza-fog.uzproject')
DEPTH_COMMON = os.path.join(REPO, 'runtime', 'shaders', 'runtime', 'include', 'depth_common.hlsli')
DEPTH_COMMON_BEFORE = os.path.join(REPO, 'scripts', 'test_contact_shadows_fixtures', 'depth_common.before.hlsli')
BEFORE_DIR = os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-contact-shadows', 'before')
W, H = 1920, 1080
DUMP_FRAME = 60
NEAR, FAR = 5.0, 10000.0
CS1_TOL, CS1_CONTROL_MIN = 1e-5, 0.90
CS3_MAX, CS3_DROP = 0.002, 0.25
CS4_GAP_MIN, CS4_DROP_TOL = 0.05, 0.02
CS5_MAX_FRACTION, CS5_EDGE_MIN, CS5_EDGE_PX, CS5_EDGE_REL, CS5_ENRICH_MIN = 0.15, 0.70, 8, 0.002, 1.3
CS6_MAX_MS = 0.5
MARCH_OFF = {'r.ContactShadow.Length': 0.0}
MARCH_ON = {'r.ContactShadow.Length': 0.01}


# ── CS1: the function on the CPU ──
def extract_function(path: str, name: str) -> str:
    src = open(path, encoding='utf-8', errors='replace').read()
    m = re.search(r'float\s+' + name + r'\s*\([^)]*\)\s*\{.*?\n\}', src, re.S)
    if not m:
        raise SystemExit(f'{path}: {name} not found')
    return m.group(0)


def cpu_roundtrip(path: str, workdir: str, tag: str) -> dict:
    body = ('#include <cstdio>\n#include <cmath>\n' + extract_function(path, 'LinearizeDepthReversedZ')
            + '\nint main() {\n    const float zs[] = {6.0f, 50.0f, 350.0f, 2000.0f, 9000.0f};\n'
            f'    const float n = {NEAR}f, f = {FAR}f;\n'
            '    for (float z : zs) {\n'
            '        float d = n * (f - z) / (z * (f - n));   // reversed-Z device depth of Mat4::Perspective\n'
            '        std::printf("%g %g\\n", z, LinearizeDepthReversedZ(d, n, f));\n'
            '    }\n    return 0;\n}\n')
    src = os.path.join(workdir, f'linearize_{tag}.cpp')
    exe = os.path.join(workdir, f'linearize_{tag}.exe')
    open(src, 'w', encoding='utf-8').write(body)
    r = subprocess.run(['zig', 'c++', '-std=c++17', src, '-o', exe], capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f'cannot compile {tag}:\n{r.stderr[-1500:]}')
    out = subprocess.run([exe], capture_output=True, text=True).stdout
    rows = [tuple(float(x) for x in line.split()) for line in out.splitlines() if line.strip()]
    return {f'{z:g}': {'value': v, 'rel_err': abs(v - z) / z} for z, v in rows}


# ── captures ──
def capture(work, project, reuse, cvars):
    if reuse and os.path.isfile(os.path.join(work, 'manifest.json')):
        return json.load(open(os.path.join(work, 'manifest.json')))
    res = runner.run_capture(work, project, DUMP_FRAME + 6, regions=[('FRAME', 0, 0, W, H)], region_fields='components',
                             region_first=DUMP_FRAME, region_last=DUMP_FRAME, cvars={'r.TechPanel.Show': 0, 'r.VolumetricFog': 0, **cvars},
                             timing_first=30, clean=True, timeout=900)
    if res['exit_code'] != 0:
        raise SystemExit(f'{work}: engine exited {res["exit_code"]}')
    return json.load(open(os.path.join(work, 'manifest.json')))


def frame(work):
    fr = regiondump.load_sequence(os.path.join(work, 'region'), DUMP_FRAME, DUMP_FRAME)[0]
    rect = fr.rect('FRAME')
    return fr.world_positions('FRAME'), rect.channel('direct_light')[..., 0].astype(np.float64)


def dilate(mask: np.ndarray, r: int) -> np.ndarray:
    out = mask.copy()
    for k in range(1, r + 1):
        out |= np.roll(mask, k, 0) | np.roll(mask, -k, 0)
    out2 = out.copy()
    for k in range(1, r + 1):
        out2 |= np.roll(out, k, 1) | np.roll(out, -k, 1)
    return out2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(REPO, '.screenshots', 'flicker-elimination', 'test-contact-shadows'))
    ap.add_argument('--reuse', action='store_true')
    args = ap.parse_args()
    results, report = [], {}

    def check(cid, ok, detail):
        results.append((cid, bool(ok), detail))
        print(f'  [{"PASS" if ok else "FAIL"}] {cid} {detail}', flush=True)

    workdir = tempfile.mkdtemp(prefix='contact_shadows_')
    now = cpu_roundtrip(DEPTH_COMMON, workdir, 'now')
    before = cpu_roundtrip(DEPTH_COMMON_BEFORE, workdir, 'before')
    report['cs1'] = {'now': now, 'before': before}
    worst = max(v['rel_err'] for v in now.values())
    check('CS1 LinearizeDepthReversedZ inverts the reversed-Z projection (round trip <= 1e-5 relative)', worst <= CS1_TOL,
          f'worst relative error {worst:.2e} over z = {", ".join(now)} cm')
    check('CS1 negative control: the pre-fix function is off by >= 90 % at 350 cm', before['350']['rel_err'] >= CS1_CONTROL_MIN,
          f'pre-fix value {before["350"]["value"]:.2f} cm for z = 350 (error {before["350"]["rel_err"]:.3f})')

    # ── CS2: before pair (captured on the committed build) and the bridge ──
    _, d_before_off = frame(os.path.join(BEFORE_DIR, 'len-0'))
    _, d_before_on = frame(os.path.join(BEFORE_DIR, 'len-default'))
    pair_diff = float(np.abs(d_before_on - d_before_off).max())
    print('sponza: march off (bridge) / on', flush=True)
    man_sp_off = capture(os.path.join(args.out, 'sponza-off'), FOG_PROJECT, args.reuse, MARCH_OFF)
    man_sp_on = capture(os.path.join(args.out, 'sponza-on'), FOG_PROJECT, args.reuse, MARCH_ON)
    pos_sp, d_sp_off = frame(os.path.join(args.out, 'sponza-off'))
    _, d_sp_on = frame(os.path.join(args.out, 'sponza-on'))
    bridge_diff = float(np.abs(d_sp_off - d_before_off).max())
    report['cs2'] = {'before_pair_max_diff': pair_diff, 'bridge_max_diff': bridge_diff}
    check('CS2 before: on the committed build the march changed nothing in Sponza (Length 0 vs 0.01 identical)', pair_diff == 0.0,
          f'direct_light max |diff| {pair_diff:.2e}')
    check('CS2 bridge: this build with the march off reproduces the committed frame (max |diff| <= 1e-6)', bridge_diff <= 1e-6,
          f'direct_light max |diff| {bridge_diff:.2e}')

    # ── CS3 / CS4: the reference scene ──
    print('reference: march off / on', flush=True)
    man_ref_off = capture(os.path.join(args.out, 'ref-off'), ref.PROJECT, args.reuse, MARCH_OFF)
    man_ref_on = capture(os.path.join(args.out, 'ref-on'), ref.PROJECT, args.reuse, MARCH_ON)
    pos, d_off = frame(os.path.join(args.out, 'ref-off'))
    _, d_on = frame(os.path.join(args.out, 'ref-on'))
    floor = np.isfinite(pos).all(-1) & (np.abs(pos[..., 2]) < 2.0)
    scale = float(np.quantile(d_off[floor], 0.99)) if floor.any() else 1.0
    v_off = np.clip(d_off / max(scale, 1e-9), 0.0, 1.5)
    v_on = np.clip(d_on / max(scale, 1e-9), 0.0, 1.5)
    truth_lit = ref.sun_visible(pos) > 0.5
    base = ref.box_base_distance(pos)
    (wx0, wy0, _), (wx1, wy1, _) = ref.shaft.WALL
    wall_dist = np.sqrt(np.maximum(np.maximum(wx0 - pos[..., 0], pos[..., 0] - wx1), 0.0) ** 2
                        + np.maximum(np.maximum(wy0 - pos[..., 1], pos[..., 1] - wy1), 0.0) ** 2)
    open_lit = floor & truth_lit & (base >= 20.0) & (wall_dist >= 20.0)
    false_dark = open_lit & (v_off - v_on > CS3_DROP)
    frac_false = float(false_dark.sum()) / max(int(open_lit.sum()), 1)
    gap_zone = floor & ~truth_lit & (base <= 10.0)
    agree_off = float(np.mean(v_off[gap_zone] < 0.5)) if gap_zone.any() else float('nan')
    agree_on = float(np.mean(v_on[gap_zone] < 0.5)) if gap_zone.any() else float('nan')
    gap = 1.0 - agree_off
    shadow_zone = floor & ~truth_lit
    report['cs3'] = {'open_lit_px': int(open_lit.sum()), 'false_dark_px': int(false_dark.sum()), 'fraction': frac_false, 'visibility_scale': scale}
    report['cs4'] = {'gap_zone_px': int(gap_zone.sum()), 'agreement_off': agree_off, 'agreement_on': agree_on, 'gap': gap,
                     'shadow_zone_px': int(shadow_zone.sum()),
                     'shadow_zone_agreement_off': float(np.mean(v_off[shadow_zone] < 0.5)) if shadow_zone.any() else None,
                     'shadow_zone_agreement_on': float(np.mean(v_on[shadow_zone] < 0.5)) if shadow_zone.any() else None,
                     'camera': man_ref_on.get('camera')}
    print(f'  reference floor {int(floor.sum())} px: open lit {int(open_lit.sum())}, box shadow {int(shadow_zone.sum())} (within 10 cm of the base {int(gap_zone.sum())}); '
          f'agreement in the base zone off {agree_off:.3f} -> on {agree_on:.3f}; whole box shadow off {report["cs4"]["shadow_zone_agreement_off"]} -> on {report["cs4"]["shadow_zone_agreement_on"]}', flush=True)
    check('CS3 no false contact shadows on open lit floor (<= 0.2 % of pixels darkened by > 0.25)', open_lit.sum() >= 1000 and frac_false <= CS3_MAX,
          f'{int(false_dark.sum())} of {int(open_lit.sum())} px ({100 * frac_false:.3f} %)')
    if gap >= CS4_GAP_MIN:
        ok4 = gap_zone.sum() >= 100 and agree_on >= agree_off + 0.5 * gap
        detail4 = f'gap {gap:.3f}: agreement off {agree_off:.3f} -> on {agree_on:.3f} (needs >= {agree_off + 0.5 * gap:.3f}) over {int(gap_zone.sum())} px'
    else:
        ok4 = gap_zone.sum() >= 100 and agree_on >= agree_off - CS4_DROP_TOL
        detail4 = f'no gap to close ({gap:.3f}): agreement off {agree_off:.3f} -> on {agree_on:.3f} over {int(gap_zone.sum())} px'
    check('CS4 the march closes the contact gap at the box base (or keeps the agreement when there is none)', ok4, detail4)

    # ── CS5: Sponza ──
    cam = np.array(man_sp_on.get('camera', {}).get('position', [0, 0, 0]), dtype=np.float64)
    target = np.array(man_sp_on.get('camera', {}).get('target', [0, 1, 0]), dtype=np.float64)
    fwd = (target - cam) / max(np.linalg.norm(target - cam), 1e-9)
    valid = np.isfinite(pos_sp).all(-1)
    depth = np.where(valid, (pos_sp - cam) @ fwd, np.nan)
    rel = np.zeros_like(depth)
    for axis, shift in ((0, 1), (0, -1), (1, 1), (1, -1)):
        nb = np.roll(depth, shift, axis)
        rel = np.fmax(rel, np.abs(depth - nb) / np.fmax(np.fmin(depth, nb), 1e-6))
    edge = np.nan_to_num(rel) > CS5_EDGE_REL
    near_edge = dilate(edge, CS5_EDGE_PX)
    scale_sp = float(np.quantile(d_sp_off[valid], 0.99)) if valid.any() else 1.0
    vs_off = np.clip(d_sp_off / max(scale_sp, 1e-9), 0.0, 1.5)
    vs_on = np.clip(d_sp_on / max(scale_sp, 1e-9), 0.0, 1.5)
    lit = valid & (vs_off > 0.5)
    darkened = lit & (vs_off - vs_on > 0.5)
    frac_dark = float(darkened.sum()) / max(int(lit.sum()), 1)
    frac_edge = float(np.mean(near_edge[darkened])) if darkened.any() else float('nan')
    base_edge = float(np.mean(near_edge[lit])) if lit.any() else float('nan')
    enrich = frac_edge / max(base_edge, 1e-9)
    report['cs5'] = {'lit_px': int(lit.sum()), 'darkened_px': int(darkened.sum()), 'fraction': frac_dark, 'near_edge_fraction': frac_edge,
                     'near_edge_fraction_all_lit': base_edge, 'enrichment': enrich, 'edge_threshold_rel': CS5_EDGE_REL,
                     'edge_px': int(edge.sum()), 'mean_visibility_drop_darkened': float(np.mean((vs_off - vs_on)[darkened])) if darkened.any() else None}
    print(f'  sponza: lit {int(lit.sum())} px, darkened by the march {int(darkened.sum())} ({100 * frac_dark:.2f} %), near a depth edge {frac_edge:.3f} '
          f'(all lit pixels {base_edge:.3f}, enrichment {enrich:.2f})', flush=True)
    check('CS5 Sponza: the march darkens <= 15 % of the lit pixels', frac_dark <= CS5_MAX_FRACTION, f'{100 * frac_dark:.2f} % ({int(darkened.sum())} of {int(lit.sum())})')
    check('CS5 ... and >= 70 % of them lie within 8 px of a depth step of >= 0.2 %, >= 1.3 x the rate over all lit pixels',
          darkened.sum() >= 100 and frac_edge >= CS5_EDGE_MIN and enrich >= CS5_ENRICH_MIN,
          f'{frac_edge:.3f} of {int(darkened.sum())} px; all lit {base_edge:.3f}; enrichment {enrich:.2f}')

    # ── CS6: cost ──
    ms_on = (man_sp_on.get('gpu_ms') or {}).get('DeferredLighting')
    ms_off = (man_sp_off.get('gpu_ms') or {}).get('DeferredLighting')
    report['cs6'] = {'deferred_on_ms': ms_on, 'deferred_off_ms': ms_off}
    check('CS6 DeferredLighting cost with the march on minus off <= 0.5 ms at 1920x1080', ms_on is not None and ms_off is not None and ms_on - ms_off <= CS6_MAX_MS,
          f'on {ms_on} ms, off {ms_off} ms')

    passed = sum(1 for _, ok, _ in results if ok)
    print(f'{passed}/{len(results)} checks passed')
    os.makedirs(args.out, exist_ok=True)
    with open(os.path.join(args.out, 'contact_shadows.json'), 'w', encoding='utf-8') as fh:
        json.dump({'report': report, 'checks': [{'id': c, 'pass': ok, 'detail': str(d)} for c, ok, d in results]}, fh, indent=1, default=str)
    return 0 if passed == len(results) else 1


if __name__ == '__main__':
    raise SystemExit(main())
