"""Behavioural harness for the lighting-component records and the GI freeze controls (followup-12).

Checks what the GPU wrote, each with a negative control:

  composition        the recorded components (direct + diffuse indirect + specular indirect +
                     emissive/MIS) sum to the scene colour the TAA reads; a deliberately dropped
                     component does not
  path bits          with reflections on the `sdf_reflections` bit is set for the shaded pixels and
                     with `r.GI.Reflections=0` it is clear
  seed freeze        `r.GI.Reflections.FreezeSeed` lowers the temporal variation of the recorded
                     reflection term; production does not
  probe freeze       `r.GI.RadianceCache.FreezeAfterFrame` keeps the same lighting path active (a
                     freeze, not a fallback) and leaves the diffuse indirect variation unchanged,
                     because the final gather re-samples every frame; freezing the sampling seed on
                     top makes it worse. Both are asserted as measured, not as expected

Usage: python scripts/test_lighting_components.py [--keep]
"""
from __future__ import annotations

import os
import shutil
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 80
FRAMES = 8
LAST = FIRST + FRAMES - 1
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
# `deferred` is a wall lit only by the deferred pass; `translucent` is the window pane, whose visible
# colour comes from the forward translucent pass and therefore CANNOT be explained by these records.
ROI_DEFERRED = ('deferred_wall', 150, 260, 192, 384)
ROI_TRANSLUCENT = ('pane', 780, 200, 128, 256)   # names are limited to 15 characters
ROIS = [ROI_DEFERRED, ROI_TRANSLUCENT]
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
REC709 = np.array([0.2126, 0.7152, 0.0722])

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def capture(work, label, cvars):
    print(f'  … {label}', flush=True)
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=ROIS, region_fields='full',
                             region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        check(False, f'{label}: engine exited {res["exit_code"]}', res['log'])
    return regiondump.load_sequence(res['region_dir'], FIRST, LAST)


def comps(frames, roi):
    rects = [f.rect(roi[0]) for f in frames]
    get = lambda n: np.stack([r.channel(n)[..., :3] for r in rects]).astype(np.float64)
    return {'direct': get('direct_light'), 'diffuse': get('diffuse_indirect'),
            'specular': get('specular_indirect'), 'emissive': get('emissive_and_mis'),
            'raw': np.stack([r.luminance('taa_raw') for r in rects]).astype(np.float64),
            'reflection': np.stack([r.luminance('lighting_reflection') for r in rects]).astype(np.float64),
            'geo': rects[0].geometry_mask(), 'paths': rects[0].path_summary()}


def main():
    runner.require_exe()
    keep = '--keep' in sys.argv
    work = tempfile.mkdtemp(prefix='uz_lighting_components_')
    print(f'work dir: {work}')
    try:
        frames = capture(work, 'baseline', {})
        base = comps(frames, ROI_DEFERRED)
        pane = comps(frames, ROI_TRANSLUCENT)
        geo = base['geo']
        lum = lambda x: x @ REC709

        def composition_error(c):
            g = c['geo']
            total = lum(c['direct'] + c['diffuse'] + c['specular'] + c['emissive'])
            return float(np.abs(total - c['raw'])[:, g].mean() / max(1e-9, float(c['raw'][:, g].mean())))

        err = composition_error(base)
        check(err < 0.01, 'on a deferred-lit wall the recorded components sum to the scene colour',
              f'mean relative error {err * 100:.4f} % of the ROI mean')
        total = lum(base['direct'] + base['diffuse'] + base['specular'] + base['emissive'])
        scale = max(1e-9, float(base['raw'][:, geo].mean()))
        dropped = lum(base['direct'] + base['diffuse'] + base['emissive'])
        err2 = float(np.abs(dropped - base['raw'])[:, geo].mean() / scale)
        check(err2 > 5 * max(err, 1e-9),
              'negative control: dropping the specular component breaks the composition',
              f'{err2 * 100:.3f} % vs {err * 100:.4f} %')
        pane_err = composition_error(pane)
        check(pane_err > 0.5,
              'negative control: on the translucent pane the deferred records explain nothing',
              f'mean relative error {pane_err * 100:.1f} % — its colour comes from the forward '
              f'translucent pass, which these records do not cover')

        check(base['paths']['sdf_reflections'] > 0.9, 'the SDF reflection path is recorded as active',
              f"{base['paths']['sdf_reflections'] * 100:.1f} % of pixels; other paths "
              f"{[k for k, v in base['paths'].items() if v > 0.5 and k != 'sdf_reflections']}")
        off = comps(capture(work, 'reflections_off', {'r.GI.Reflections': 0}), ROI_DEFERRED)
        check(off['paths']['sdf_reflections'] == 0.0,
              'negative control: with reflections off the path bit is clear',
              f"{off['paths']['sdf_reflections'] * 100:.1f} %")

        frozen = comps(capture(work, 'seed_frozen', {'r.GI.Reflections.FreezeSeed': 7}), ROI_DEFERRED)
        base_var = float(base['reflection'][:, geo].std(axis=0).mean())
        froz_var = float(frozen['reflection'][:, geo].std(axis=0).mean())
        check(froz_var < base_var,
              'freezing the sampling seed lowers the temporal variation of the reflection term',
              f'{froz_var:.3e} vs {base_var:.3e} ({froz_var / max(1e-12, base_var):.2f}x)')

        # Freezing the probes alone leaves the final gather re-sampling every frame (its seed is the
        # frame index, `sdf_diffuse_gather.hlsli`), so the two freezes are checked together and
        # separately.
        rcf = comps(capture(work, 'rc_frozen', {'r.GI.RadianceCache.FreezeAfterFrame': FIRST - 8}), ROI_DEFERRED)
        both = comps(capture(work, 'rc_and_seed_frozen',
                             {'r.GI.RadianceCache.FreezeAfterFrame': FIRST - 8,
                              'r.GI.Reflections.FreezeSeed': 7}), ROI_DEFERRED)
        d_frozen = float(lum(rcf['diffuse'])[:, geo].std(axis=0).mean())
        d_base = float(lum(base['diffuse'])[:, geo].std(axis=0).mean())
        d_both = float(lum(both['diffuse'])[:, geo].std(axis=0).mean())
        # Measured: holding the converged probes does NOT quieten the diffuse indirect, because the
        # final gather re-samples against them every frame. Asserted as a range so a later change
        # that makes probe updates matter here is noticed rather than assumed away.
        check(0.85 < d_frozen / max(1e-12, d_base) < 1.15,
              'holding the converged probes leaves the diffuse indirect variation unchanged',
              f'{d_frozen:.3e} vs {d_base:.3e} ({d_frozen / max(1e-12, d_base):.2f}x) — the variation '
              f'comes from the final gather re-sampling, not from probe updates')
        # Measured, and kept as an assertion so a later change that "fixes" it is noticed: freezing
        # the sampling seed does NOT stabilise the final gather — it pins the sampling pattern and
        # the residual gets *worse*. A valid gather freeze has to hold the gather's output, not its
        # seed; that control does not exist yet and is named in the ledger for followup-14.
        check(d_both > d_base,
              'freezing the seed does not stabilise the final gather (measured, not assumed)',
              f'{d_both:.3e} vs {d_base:.3e} ({d_both / max(1e-12, d_base):.2f}x) — a valid gather '
              f'freeze must hold its output, not its seed')
        check(rcf['paths']['sdf_diffuse_gather'] > 0.9 and rcf['paths']['sdf_reflections'] > 0.9,
              'the freeze keeps the same lighting path active (it is a freeze, not a fallback)',
              f"gather {rcf['paths']['sdf_diffuse_gather'] * 100:.0f} %, "
              f"reflections {rcf['paths']['sdf_reflections'] * 100:.0f} %")
        check(d_base > 0.0, 'negative control: production diffuse indirect does vary',
              f'{d_base:.3e}')
    finally:
        print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
        for f in FAIL:
            print(f'  FAILED: {f}')
        if keep:
            print('kept:', work)
        else:
            shutil.rmtree(work, ignore_errors=True)
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
