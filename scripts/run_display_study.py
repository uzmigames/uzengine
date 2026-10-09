"""Exposure, bloom and display attribution for the window residual (followup-11).

Order of business, and why:

1. **Validate the replay.** With bloom off the tonemap is a per-pixel function of the recorded linear
   radiance, so the offline chain (`scripts/flicker/display_model.py`) must reproduce the engine's
   PNG. Nothing else in this study is trusted until that closes.
2. **Units.** For the same pixels, report linear Rec.709 luminance, the resolve's 4x-scaled YCoCg Y,
   the pre-exposed colour and the display code values side by side, with the effective exposure of
   every frame.
3. **Exposure.** Replay the identical HDR sequence at 0.5x, 1x and 2x the baseline exposure, and
   capture the same three exposures on the GPU to confirm the HDR input did not move.
4. **Bloom.** Capture with bloom on and off; check the recorded HDR is unchanged, then measure the
   dark-side floor, the local contrast and the exact display threshold counts.
5. **PreExposeWeights.** Diagnostic only: bright and dark sides, p99 error and temporal response.
   5b. **Bloom at the operating exposure** (added 2026-10-08). Step 4 runs at a fixed exposure of
   1.0; the 2026-09-09 package measured bloom once, at the auto-exposure operating point (~67).
   This measures it there, with repeated processes per condition (ledger H-16b).
6. **Attribution table** separating the upstream radiance oscillation from the exposure gain and the
   bloom floor, without turning a count ratio into an energy claim.

Reproducibility (rewritten 2026-10-08). The first version asserted `max |delta| == 0.0` between
separate engine processes. followup-14 then found that this renderer is not bit-reproducible across
processes (`docs/analysis/flicker-hypothesis-ledger.md`, "The renderer is not bit-reproducible across
processes"; `docs/guides/build-and-deployment.md`, "Capture reproducibility"). Every configuration that
enters an equivalence claim is now captured in `--repeats` processes (at least 3), interleaved, and
judged by `indistinguishable()`: the rule `scripts/test_correction_gates.py` adopted on 2026-10-08
(max(between) <= max(within) on max |delta|), plus a mean |delta| that must not read SEPARATED, which
catches a uniform shift hiding under the largest single-value wobble. The first version's seventh
check was `check(True, ...)`, which could not fail; it now tests the reflections-off substitution
against a decision floor modelled on the ledger's predeclared rule 1 (see DECISION_FRACTION).

Retention (2026-10-08). Every run's manifest and log go to `<out>/runs/<label>/`, with the PNGs of the
runs that took any (16 runs x 24 frames, about 0.35 GB at the 2026-09-09 PNG size), and the repeat-0
region dump of each configuration to `<out>/raw/<label>/`: ten dumps of about 0.68 GB each (24 frames x
98 304 px x 18 float4 records, the `taa+extra` layout of `region_dump.hlsli`), about 6.8 GB in all;
`--no-keep-raw` releases them. The first version deleted every dump and kept 3 of its 9 manifests. A
package is never overwritten: `--out` must not already hold a `display-study.json`.

Usage: python scripts/run_display_study.py --out DIR [--repeats 4] [--no-keep-raw]
       python scripts/run_display_study.py --self-test   (no engine: synthetic checks of the rule)
"""
from __future__ import annotations

import argparse
import itertools
import json
import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import criteria as crit  # noqa: E402
import display_model as dm  # noqa: E402
import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 24
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
ROI = ('window', 770, 150, 192, 512)
SATURATION, CONTRAST, OPERATOR = 1.0, 0.65, 1      # the QA project's r.ColorSaturation/Contrast
BASE_EXPOSURE = 1.0
#: Processes per configuration wherever two configurations are claimed to give the same records:
#: at least 3, 4 by default, as in `scripts/test_correction_gates.py`.
MIN_REPEATS = 3
DEFAULT_REPEATS = 4
#: A same-configuration spread must stay under this fraction of the signal (on mean |delta|) for an
#: equivalence check to have any power (the 2 % of `scripts/test_correction_gates.py`, which applies
#: it to the max |delta| spread instead).
POWER = 0.02
#: Modelled on the predeclared decision rule (`docs/analysis/flicker-hypothesis-ledger.md`, rule 1:
#: max(3 x the 95 % half-width of the baseline repeatability, 5 % of the baseline mean)): here a
#: change counts only beyond max(the range of the repeats, 5 % of the reference value). The range
#: stands in for the half-width term; the 5 % term binds at the effects measured on 2026-09-09
#: (dark side 11.2x, exposure 0.064x).
DECISION_FRACTION = 0.05

RESULTS = []
RUNS = {}


def check(ok, what, detail=''):
    RESULTS.append({'check': what, 'ok': bool(ok), 'detail': detail})
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def indistinguishable(runs_a, runs_b, power=POWER):
    """Are two configurations' linear records indistinguishable from process-to-process variation?

    Bit-identity is not a valid criterion: processes of one configuration land in a few discrete
    states that differ in a few hundred to a few thousand recorded values. Each configuration is
    captured in at least MIN_REPEATS processes. Judged, all three:
      * `criteria.spread_verdict` on the max |delta| of every pair reads INDISTINGUISHABLE (the rule
        `scripts/test_correction_gates.py` adopted on 2026-10-08);
      * the mean |delta| does not read SEPARATED. A uniform shift smaller than the largest
        single-value wobble hides from the max rule but separates every cross pair on the mean.
        (The mean is NOT required to read INDISTINGUISHABLE: that test fails by chance on this
        renderer, followup-14 notes, close-out 2026-10-08.)
      * the same-configuration mean |delta| is small against the signal (`power`, a fraction of the
        mean |radiance| of `runs_a`), or the check could not have detected anything.

    Measured on the eight same-path captures retained in
    `.screenshots/flicker-elimination/40-default-vs-explicit` (RGB, every split; 2026-10-08): the rule
    fails on 16 of 280 splits at 3 vs 3 processes and on 1 of 35 at 4 vs 4, the same splits on which
    the max rule alone fails, each reading SEPARATED on both statistics because the split put the
    discrete states on opposite sides; it detects a uniform +1e-3, a x1.0001 and a x1.001 change and a
    single pixel moved by 5e-3 in 280 of 280 splits at 3 vs 3. A failure in one run is therefore not
    yet a change, whatever its verdict: re-run that comparison with more repeats, and read it as a
    change only if it fails again.
    """
    runs_a, runs_b = list(runs_a), list(runs_b)
    if len(runs_a) < MIN_REPEATS or len(runs_b) < MIN_REPEATS:
        raise ValueError(f'each configuration needs at least {MIN_REPEATS} processes')

    def dist(x, y):
        d = np.abs(x - y)
        return float(d.max()), float(d.mean())

    within = [dist(r[i], r[j]) for r in (runs_a, runs_b) for i, j in itertools.combinations(range(len(r)), 2)]
    between = [dist(x, y) for x in runs_a for y in runs_b]
    scale = float(np.mean([np.abs(x).mean() for x in runs_a]))
    if not crit.is_measurable(scale, *(v for p in within + between for v in p)):
        return {'ok': False, 'verdict_max': 'NOT MEASURABLE', 'verdict_mean': 'NOT MEASURABLE',
                'powered': False, 'processes': [len(runs_a), len(runs_b)],
                'detail': 'not measurable: a non-finite value in the records'}
    w_max, b_max = [w[0] for w in within], [b[0] for b in between]
    w_mean, b_mean = [w[1] for w in within], [b[1] for b in between]
    verdict_max = crit.spread_verdict(b_max, w_max)
    verdict_mean = crit.spread_verdict(b_mean, w_mean)
    powered = scale > 0.0 and max(w_mean) < power * scale
    ok = bool(verdict_max == 'INDISTINGUISHABLE' and verdict_mean != 'SEPARATED' and powered)
    detail = (f'max |delta| between {min(b_max):.3e}..{max(b_max):.3e} ({len(b_max)} pairs), within '
              f'{min(w_max):.3e}..{max(w_max):.3e} ({len(w_max)} pairs) -> {verdict_max}; '
              f'mean |delta| between {min(b_mean):.3e}..{max(b_mean):.3e}, within '
              f'{min(w_mean):.3e}..{max(w_mean):.3e} -> {verdict_mean} (must not be SEPARATED); '
              f'same-configuration mean |delta| = '
              f'{(max(w_mean) / scale * 100) if scale > 0 else float("inf"):.2g} % of the mean {scale:.4g} '
              f'(power needs < {power * 100:g} %)')
    return {'ok': ok, 'verdict_max': verdict_max, 'verdict_mean': verdict_mean, 'powered': bool(powered),
            'between_max_abs': [min(b_max), max(b_max)], 'within_max_abs': [min(w_max), max(w_max)],
            'between_mean_abs': [min(b_mean), max(b_mean)], 'within_mean_abs': [min(w_mean), max(w_mean)],
            'scale': scale, 'processes': [len(runs_a), len(runs_b)], 'detail': detail}


def capture(work, label, cvars, shots=None):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='taa+extra',
                             region_first=FIRST, region_last=LAST, cvars=cvars,
                             screenshot_frames=shots, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]}')
    RUNS[label] = res
    return res


def load_run(res):
    """The linear records of one capture. Each frame object holds its whole file, so the frames
    are read once and dropped, keeping only what the study uses."""
    seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    rects = [f.rect(ROI[0]) for f in seq]
    return {'rgb': np.stack([r.channel('taa_output')[..., :3] for r in rects]).astype(np.float64),
            'frames': [f.frame for f in seq],
            'exposure': [float(f.exposure) for f in seq],
            'weight_exposure': float(rects[0].channel('taa_output')[..., 3].mean())}


def release(res, out, keep_raw, retained):
    """Copies a run's region dump into the package when it is retained, then deletes it."""
    if keep_raw and retained:
        dst = os.path.join(out, 'raw', os.path.basename(res['dir']))
        if os.path.isdir(dst):
            shutil.rmtree(dst)
        shutil.copytree(res['region_dir'], dst)
    shutil.rmtree(res['region_dir'], ignore_errors=True)


def take(work, out, keep_raw, label, cvars, shots=None, retained=True):
    """Captures one process, reads its linear records and releases its dump."""
    res = capture(work, label, cvars, shots=shots)
    run = load_run(res)
    release(res, out, keep_raw, retained)
    return res, run


def take_interleaved(work, out, keep_raw, plan, repeats):
    """Every configuration of `plan` in `repeats` processes, round-robin: repeat 0 of each, then
    repeat 1 of each, and so on, so a drift over the session cannot line up with one configuration
    (the order `scripts/run_default_vs_explicit.py` pre-registered). Returns {label: [(res, run)]}."""
    got = {label: [] for label, _, _ in plan}
    for r in range(repeats):
        for label, cvars, shots in plan:
            got[label].append(take(work, out, keep_raw, f'{label}_{r}', cvars, shots, r == 0))
    return got


def png_roi(res, frame):
    img = runner.png_array(os.path.join(res['frames_dir'], f'f_{frame}.png'))
    return img[ROI[2]:ROI[2] + ROI[4], ROI[1]:ROI[1] + ROI[3]].astype(np.int32)


def bloom_stats(res, frames, dark, bright):
    """Display-side statistics of one capture's PNGs over `frames`, in code values."""
    pngs = np.stack([png_roi(res, n) for n in frames]).astype(np.float64)
    lumd = pngs @ dm.REC709
    step = np.abs(np.diff(lumd, axis=0))
    pairs = len(step)
    return {
        'dark_floor_mean_code': float(lumd[:, dark].mean()),
        'dark_floor_p99_code': float(np.percentile(lumd[:, dark], 99)),
        'bright_mean_code': float(lumd[:, bright].mean()),
        'local_contrast_bright_over_dark': float(lumd[:, bright].mean() / max(1e-6, lumd[:, dark].mean())),
        'count_gt_40_per_frame': float((step > 40).sum() / max(1, pairs)),
        'count_gt_8_per_frame': float((step > 8).sum() / max(1, pairs)),
        'count_gt_40_total': int((step > 40).sum()),
        'count_gt_8_total': int((step > 8).sum()),
        'frame_pairs': int(pairs),
    }


def region_stats(run, dark, boundary):
    ll = run['rgb'] @ dm.REC709
    return {
        'auto_exposure_mean': float(np.mean(run['exposure'])),
        'dark_mean_radiance': float(ll[:, dark].mean()),
        'dark_std_radiance': float(ll[:, dark].std(axis=0).mean()),
        'boundary_mean_radiance': float(ll[:, boundary].mean()),
        'boundary_std_radiance': float(ll[:, boundary].std(axis=0).mean()),
        'roi_mean_radiance': float(ll.mean()),
    }


def save_runs(out):
    """Every run's manifest and log, and the PNGs of the runs that took any, into the package."""
    for label, res in RUNS.items():
        dst = os.path.join(out, 'runs', label)
        os.makedirs(dst, exist_ok=True)
        for src in (res['manifest_path'], res['log']):
            if os.path.exists(src):
                shutil.copy(src, dst)
        if os.path.isdir(res['frames_dir']) and os.listdir(res['frames_dir']):
            fdst = os.path.join(dst, 'frames')
            if os.path.isdir(fdst):
                shutil.rmtree(fdst)
            shutil.copytree(res['frames_dir'], fdst)


def self_test():
    """Synthetic checks of `indistinguishable()`; no engine needed (added 2026-10-08)."""
    rng = np.random.default_rng(11)
    base = rng.uniform(0.0, 8.0, (4, 16, 16, 3))

    def wobble(x, n=3):
        y = x.copy()
        idx = rng.choice(y.size, n, replace=False)
        y.flat[idx] += rng.uniform(1e-3, 1.6e-2, n)
        return y

    other_state = wobble(base, 40)              # a second discrete state of the same render
    cases = [
        ('bit-identical processes are indistinguishable', [base] * 3, [base.copy()] * 3, True),
        ('a sparse wobble in some processes of each configuration is not a change',
         [base, wobble(base), base.copy()], [wobble(base), base.copy(), base.copy()], True),
        ('a wobble in one process only (a zero spread on the other side) is not a change',
         [base] * 3, [base.copy(), base.copy(), wobble(base)], True),
        ('two discrete states, each seen within both configurations, are not a change',
         [base, other_state, base.copy()], [other_state.copy(), base.copy(), base.copy()], True),
        ('a systematic 0.1 % change is detected', [base] * 3, [base * 1.001] * 3, False),
        ('a systematic 1 % change under process wobble is detected',
         [base, wobble(base), base.copy()], [wobble(base * 1.01), base * 1.01, wobble(base * 1.01)], False),
        ('a uniform shift smaller than the largest wobble is detected on the mean',
         [base, base + np.where(np.arange(base.size).reshape(base.shape) == 5, 1.6e-2, 0.0), base.copy()],
         [base + 1e-3] * 3, False),
        ('one pixel moved in every process of one configuration is detected',
         [base] * 3, [np.where(np.arange(base.size).reshape(base.shape) == 7, base + 0.05, base)] * 3, False),
        ('a spread of 5 % of the signal has no power and fails',
         [base, base * 1.05, base.copy()], [base.copy(), base * 1.05, base.copy()], False),
        ('a non-finite record fails rather than passing', [base] * 3,
         [base.copy(), base.copy(), np.where(base > 7.9, np.nan, base)], False),
    ]
    failed = 0
    for what, a, b, want in cases:
        ok = indistinguishable(a, b)['ok'] == want
        failed += 0 if ok else 1
        print(f'  {"ok  " if ok else "FAIL"} {what}')
    try:
        indistinguishable([base, base.copy()], [base, base.copy(), base.copy()])
        failed += 1
        print(f'  FAIL a configuration captured in fewer than {MIN_REPEATS} processes is refused')
    except ValueError:
        print(f'  ok   a configuration captured in fewer than {MIN_REPEATS} processes is refused')
    print(f'{len(cases) + 1 - failed} passed, {failed} failed')
    return 1 if failed else 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '11-isolate-flicker-display'))
    ap.add_argument('--repeats', type=int, default=DEFAULT_REPEATS)
    ap.add_argument('--no-keep-raw', action='store_true')
    ap.add_argument('--self-test', action='store_true')
    args = ap.parse_args()
    if args.self_test:
        return self_test()
    if args.repeats < MIN_REPEATS:
        print(f'--repeats must be at least {MIN_REPEATS}', file=sys.stderr)
        return 2
    # The default directory holds the 2026-09-09 package. Packages are never overwritten (design.md,
    # "Artifacts and reproducibility"), so a re-run names a new directory.
    if os.path.exists(os.path.join(args.out, 'display-study.json')):
        print(f'{args.out} already holds a display-study.json; pass --out with a new directory '
              '(evidence packages are not overwritten)', file=sys.stderr)
        return 2
    runner.require_exe()
    keep_raw = not args.no_keep_raw
    os.makedirs(args.out, exist_ok=True)
    work = tempfile.mkdtemp(prefix='uz_display_study_')
    payload = {'roi': ROI, 'first_frame': FIRST, 'frames': FRAMES, 'repeats': args.repeats,
               'tonemap': {'saturation': SATURATION, 'contrast': CONTRAST, 'operator': OPERATOR}}

    shots = f'{FIRST}-{LAST}'
    base = {'r.AutoExposure': 0, 'r.Exposure': BASE_EXPOSURE, 'r.TAA.JitterSequenceLength': 8}

    print('captures for steps 1-5, interleaved')
    block = take_interleaved(work, args.out, keep_raw, [
        ('bloom_off', {**base, 'r.Bloom': 0}, shots),
        ('exposure_0.5', {**base, 'r.Exposure': BASE_EXPOSURE * 0.5, 'r.Bloom': 0}, None),
        ('exposure_2.0', {**base, 'r.Exposure': BASE_EXPOSURE * 2.0, 'r.Bloom': 0}, None),
        ('bloom_on', {**base}, shots),
        ('auto_exposure', {'r.TAA.JitterSequenceLength': 8, 'r.Bloom': 0}, None),
    ], args.repeats)
    rgb = {label: [run['rgb'] for _, run in got] for label, got in block.items()}

    print('1. replay validation (bloom off, fixed exposure)')
    nobloom = [res for res, _ in block['bloom_off']]
    hdr = rgb['bloom_off'][0]
    frames = block['bloom_off'][0][1]['frames']
    deltas, within_1 = [], []
    for i, n in enumerate(frames):
        want = png_roi(nobloom[0], n)
        got = dm.to_code_values(dm.replay(hdr[i], exposure=BASE_EXPOSURE, saturation=SATURATION,
                                          contrast=CONTRAST, operator=OPERATOR))
        diff = np.abs(got - want)
        deltas.append(int(diff.max()))
        within_1.append(float(np.mean(diff <= 1)))
    frac = float(np.mean(within_1))
    check(max(deltas) <= 2 and frac > 0.999,
          'the offline replay reproduces the engine PNG with bloom off',
          f'max code-value difference {max(deltas)}, {frac * 100:.3f} % of pixels within 1, '
          f'{len(frames)} frames')
    payload['replay_validation'] = {'max_code_delta': deltas, 'fraction_within_1': frac}

    print('2. units on the same pixels')
    peak = np.percentile(hdr.max(axis=(0, 3)), 99)
    lum = hdr @ dm.REC709
    bright = lum.min(axis=0) >= 0.9 * peak * 0.7
    dark = lum.max(axis=0) <= 0.05 * peak
    boundary = ~bright & ~dark
    masks = {'bright': bright, 'dark': dark, 'boundary': boundary, 'all': np.ones_like(bright)}
    ycocg = hdr[..., 0] + 2 * hdr[..., 1] + hdr[..., 2]
    disp = np.stack([dm.replay(h, exposure=BASE_EXPOSURE, saturation=SATURATION,
                               contrast=CONTRAST, operator=OPERATOR) @ dm.REC709 * 255.0 for h in hdr])
    units = {}
    for name, m in masks.items():
        units[name] = {
            'pixels': int(m.sum()),
            'linear_rec709_mean': float(lum[:, m].mean()),
            'linear_rec709_std': float(lum[:, m].std(axis=0).mean()),
            'ycocg_y_mean': float(ycocg[:, m].mean()),
            'pre_exposed_mean': float((lum[:, m] * BASE_EXPOSURE).mean()),
            'display_code_mean': float(disp[:, m].mean()),
            'display_code_std': float(disp[:, m].std(axis=0).mean()),
        }
    payload['units'] = units
    payload['exposure_per_frame'] = block['bloom_off'][0][1]['exposure']
    check(len({round(e, 6) for e in payload['exposure_per_frame']}) == 1,
          'the effective exposure is recorded for every frame and constant in this run',
          f'{payload["exposure_per_frame"][0]}')

    print('3. exposure sweep on the identical HDR input (offline) and on the GPU')
    sweep = {}
    for factor in (0.5, 1.0, 2.0):
        e = BASE_EXPOSURE * factor
        d = np.stack([dm.replay(h, exposure=e, saturation=SATURATION, contrast=CONTRAST,
                                operator=OPERATOR) @ dm.REC709 * 255.0 for h in hdr])
        step = np.abs(np.diff(d, axis=0))
        sweep[f'{factor}x'] = {
            'exposure': e,
            'display_mean': float(d.mean()),
            'display_std_mean': float(d.std(axis=0).mean()),
            'count_gt_40_per_frame': float((step > 40).sum() / (len(d) - 1)),
            'count_gt_8_per_frame': float((step > 8).sum() / (len(d) - 1)),
        }
    payload['exposure_sweep_offline'] = sweep
    gpu = {}
    for factor in (0.5, 2.0):
        label = f'exposure_{factor}'
        eq = indistinguishable(rgb['bloom_off'], rgb[label])
        gpu[f'{factor}x'] = {'equivalence': eq, 'exposure_recorded': block[label][0][1]['exposure'][0]}
    payload['exposure_sweep_gpu'] = gpu
    check(all(v['equivalence']['ok'] for v in gpu.values()),
          'changing the exposure leaves the linear HDR input indistinguishable from process-to-process '
          'variation',
          '; '.join(f'{k}: {v["equivalence"]["detail"]}' for k, v in gpu.items()))

    print('4. bloom on the identical HDR sequence')
    eq = indistinguishable(rgb['bloom_off'], rgb['bloom_on'])
    check(eq['ok'], 'bloom does not change the upstream HDR beyond process-to-process variation',
          eq['detail'])
    repeats = {'bloom_off': [bloom_stats(res, frames, dark, bright) for res in nobloom],
               'bloom_on': [bloom_stats(res, frames, dark, bright) for res, _ in block['bloom_on']]}
    payload['bloom'] = {k: v[0] for k, v in repeats.items()}
    payload['bloom_repeats'] = repeats
    payload['bloom_upstream'] = eq
    payload['bloom_exposure'] = BASE_EXPOSURE

    print('5. auto-exposure operating point, and PreExposeWeights at that point')
    # `r.TAA.PreExposeWeights` multiplies the resolve's HDR weights by the FRAME exposure, so at a
    # fixed exposure of 1.0 it is a no-op by construction. Evaluating it anywhere else would be
    # meaningless, so first measure where auto-exposure actually settles on this pose.
    auto_runs = [run for _, run in block['auto_exposure']]
    auto_exposures = auto_runs[0]['exposure']
    eq = indistinguishable(rgb['bloom_off'], rgb['auto_exposure'])
    payload['auto_exposure'] = {
        'per_frame': auto_exposures,
        'mean': float(np.mean(auto_exposures)),
        'range': [float(min(auto_exposures)), float(max(auto_exposures))],
        'relative_swing': float((max(auto_exposures) - min(auto_exposures)) / np.mean(auto_exposures)),
        'mean_per_process': [float(np.mean(r['exposure'])) for r in auto_runs],
        'equivalence_vs_fixed_baseline': eq,
    }
    check(eq['ok'],
          'auto-exposure leaves the linear HDR input indistinguishable from the fixed-exposure run',
          f"{eq['detail']}; exposure {payload['auto_exposure']['mean']:.2f} "
          f"(swing {payload['auto_exposure']['relative_swing'] * 100:.3f} % over the sequence)")
    # Step 6 uses this configuration as its reflections-on condition (identical CVars).
    reflections_on = [region_stats(r, dark, boundary) for r in auto_runs]
    del block, rgb, auto_runs

    op = round(payload['auto_exposure']['mean'], 3)
    payload['preexpose'] = {'operating_exposure': op,
                            'note': 'at a fixed exposure of 1.0 PreExposeWeights is a no-op by construction'}
    _, pre_run = take(work, args.out, keep_raw, 'preexpose',
                      {**base, 'r.Exposure': op, 'r.TAA.PreExposeWeights': 1, 'r.Bloom': 0})
    _, ref_run = take(work, args.out, keep_raw, 'preexpose_control',
                      {**base, 'r.Exposure': op, 'r.Bloom': 0})
    lp = pre_run['rgb'] @ dm.REC709
    lc = ref_run['rgb'] @ dm.REC709
    for name, m in masks.items():
        payload['preexpose'][name] = {
            'std_mean_control': float(lc[:, m].std(axis=0).mean()),
            'std_mean_preexpose': float(lp[:, m].std(axis=0).mean()),
            'std_p99_control': float(np.percentile(lc[:, m].std(axis=0), 99)),
            'std_p99_preexpose': float(np.percentile(lp[:, m].std(axis=0), 99)),
            'mean_control': float(lc[:, m].mean()),
            'mean_preexpose': float(lp[:, m].mean()),
            'temporal_response_abs_diff_control': float(np.abs(np.diff(lc[:, m], axis=0)).mean()),
            'temporal_response_abs_diff_preexpose': float(np.abs(np.diff(lp[:, m], axis=0)).mean()),
        }
    payload['preexpose']['weight_exposure_recorded'] = pre_run['weight_exposure']
    payload['preexpose']['weight_exposure_control'] = ref_run['weight_exposure']
    check(abs(payload['preexpose']['weight_exposure_recorded'] - op) < 0.01 * op,
          'PreExposeWeights makes the resolve weight with the frame exposure',
          f"{payload['preexpose']['weight_exposure_recorded']:.3f} vs control "
          f"{payload['preexpose']['weight_exposure_control']:.3f} at exposure {op:.2f}")
    del pre_run, ref_run, lp, lc

    print('5b. bloom at the operating exposure')
    # Step 4 runs at exposure 1.0. The 2026-09-09 package measured bloom once at the auto-exposure
    # operating point and found that it LOWERS the threshold count there (bloom off: 39 -> 1547
    # px > 40 per frame); this task first read that as the opposite sign (corrected 2026-10-08,
    # ledger H-16b). Measured here at that exposure, held fixed, in repeated processes.
    ob = take_interleaved(work, args.out, keep_raw, [
        ('bloom_off_op', {**base, 'r.Exposure': op, 'r.Bloom': 0}, shots),
        ('bloom_on_op', {**base, 'r.Exposure': op}, shots),
    ], args.repeats)
    eq = indistinguishable([run['rgb'] for _, run in ob['bloom_off_op']],
                           [run['rgb'] for _, run in ob['bloom_on_op']])
    check(eq['ok'], 'at the operating exposure too, bloom does not change the upstream HDR beyond '
          'process-to-process variation', eq['detail'])
    payload['bloom_operating_exposure'] = {
        'exposure': op,
        'bloom_off': [bloom_stats(res, frames, dark, bright) for res, _ in ob['bloom_off_op']],
        'bloom_on': [bloom_stats(res, frames, dark, bright) for res, _ in ob['bloom_on_op']],
        'upstream': eq,
    }
    del ob

    print('6. the reflections-off confound, quantified')
    # Turning reflections off changes the radiance of the dark side, which moves the auto-exposure
    # operating point, which changes every display-referred count. Measuring the exposure and the
    # dark-side radiance separates that chain from any claim about energy amplification.
    # The reflections-on condition is step 5's auto-exposure configuration (identical CVars; the first
    # version captured it a second time under this name), so its repeated processes also give the
    # same-configuration spread that the decision floor below needs.
    _, off_run = take(work, args.out, keep_raw, 'reflections_off',
                      {'r.TAA.JitterSequenceLength': 8, 'r.Bloom': 0, 'r.GI.Reflections': 0})
    refl = {'reflections_on': reflections_on[0], 'reflections_on_repeats': reflections_on,
            'reflections_off': region_stats(off_run, dark, boundary)}
    del off_run
    on, off = refl['reflections_on'], refl['reflections_off']
    refl['exposure_ratio_off_over_on'] = off['auto_exposure_mean'] / on['auto_exposure_mean']
    refl['dark_radiance_ratio_off_over_on'] = (off['dark_mean_radiance'] / on['dark_mean_radiance']
                                               if on['dark_mean_radiance'] else float('nan'))
    refl['boundary_std_ratio_off_over_on'] = (off['boundary_std_radiance'] / on['boundary_std_radiance']
                                              if on['boundary_std_radiance'] else float('nan'))

    def spread(key):
        values = [r[key] for r in reflections_on]
        return max(values) - min(values)

    # A change counts only beyond max(the same-configuration range, 5 % of the reflections-on
    # value), modelled on the ledger's rule 1 (DECISION_FRACTION).
    dark_floor = max(spread('dark_mean_radiance'), DECISION_FRACTION * on['dark_mean_radiance'])
    exposure_floor = max(spread('auto_exposure_mean'), DECISION_FRACTION * on['auto_exposure_mean'])
    refl['decision_floor'] = {'dark_mean_radiance': dark_floor, 'auto_exposure_mean': exposure_floor}
    payload['reflections_confound'] = refl
    measurable = crit.is_measurable(on['dark_mean_radiance'], off['dark_mean_radiance'],
                                    on['auto_exposure_mean'], off['auto_exposure_mean'],
                                    dark_floor, exposure_floor)
    substitutes = bool(measurable
                       and off['dark_mean_radiance'] - on['dark_mean_radiance'] > dark_floor
                       and on['auto_exposure_mean'] - off['auto_exposure_mean'] > exposure_floor)
    check(substitutes,
          'turning reflections off brightens the dark side and lowers the auto-exposure beyond the '
          'decision floor: a substitution, not a subtraction (H-02b)',
          f"auto-exposure {on['auto_exposure_mean']:.2f} -> {off['auto_exposure_mean']:.2f} "
          f"({refl['exposure_ratio_off_over_on']:.2f}x, floor {exposure_floor:.3g}), dark radiance "
          f"{on['dark_mean_radiance']:.5f} -> {off['dark_mean_radiance']:.5f} (floor {dark_floor:.3g}), "
          f"boundary std {on['boundary_std_radiance']:.5f} -> {off['boundary_std_radiance']:.5f} "
          f"({refl['boundary_std_ratio_off_over_on']:.2f}x)")

    payload['checks'] = RESULTS
    payload['source_fingerprint'] = runner.source_fingerprint()
    payload['retention'] = {'runs': sorted(RUNS), 'raw_dumps_kept': keep_raw}
    save_runs(args.out)
    shutil.rmtree(work, ignore_errors=True)
    path = os.path.join(args.out, 'display-study.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    failed = [r for r in RESULTS if not r['ok']]
    print(f'{len(RESULTS) - len(failed)} passed, {len(failed)} failed')
    return 1 if failed else 0


if __name__ == '__main__':
    raise SystemExit(main())
