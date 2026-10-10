"""Does the candidate correction behave differently on mirror and glossy materials? (followup-14 1.5)

The checklist asks for the correction to be covered on mirror/rough materials and normal maps.
Sponza cannot answer the first half: the recorded G-buffer roughness over every measured ROI is
0.5-0.8, with 13 glossy pixels in total and no mirror pixels at all (`materials.json`). So instead
of reporting half the item as unexecutable, `r.Material.RoughnessOverride` and
`r.Material.MetallicOverride` (both default -1 = off) force the material class in the G-buffer,
where the lighting and the recorded roughness both pick it up.

Each condition is measured against the cycle-averaged raw input — the correctly resampled image —
so the question is whether the candidate is more or less accurate on that material class, not
merely whether it differs from the baseline.

Every capture asserts that the override actually reached the G-buffer by reading back the recorded
roughness: a silently ignored CVar produced a wrong conclusion once in this programme.

Usage: python scripts/run_material_class_study.py [--out DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

CVAR = 'r.TAA.ReconstructionHdrWeightExponent'
SCENE_START = 37
FIRST = SCENE_START + 200
FRAMES = 32
LAST = FIRST + FRAMES - 1
# A *deferred, directly lit* surface. The first attempt used the pose W window, where the
# override reached the G-buffer but moved the image by 0.0007 %: that rectangle's radiance comes
# from the sky seen through translucent geometry, which never enters the G-buffer, so forcing a
# material class there measured nothing. The sunlit stone floor of pose A is deferred, so its
# specular response actually depends on roughness and metallic.
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-a.uzproject')
ROI = ('lit_floor', 832, 816, 256, 64)
BASE = {'r.TAA.JitterSequenceLength': 8, 'r.AutoExposure': 0, 'r.Exposure': 1.0}

#: label -> (cvars, expected recorded roughness or None for "whatever the scene has")
CLASSES = {
    'scene (no override)': ({}, None),
    'mirror metal (r=0.05, m=1)': ({'r.Material.RoughnessOverride': 0.05,
                                    'r.Material.MetallicOverride': 1.0}, 0.05),
    'glossy dielectric (r=0.25, m=0)': ({'r.Material.RoughnessOverride': 0.25,
                                         'r.Material.MetallicOverride': 0.0}, 0.25),
    'rough dielectric (r=0.80, m=0)': ({'r.Material.RoughnessOverride': 0.80,
                                        'r.Material.MetallicOverride': 0.0}, 0.80),
}
VARIANTS = [('baseline', 1.0), ('candidate_0.0', 0.0)]


def measure(work, label, cvars, exponent, expect_roughness):
    res = runner.run_capture(os.path.join(work, label), PROJECT, LAST - SCENE_START + 8,
                             regions=[ROI], region_fields='full',
                             region_first=FIRST, region_last=LAST,
                             cvars={**BASE, **cvars, CVAR: f'{exponent:g}'}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    seq = regiondump.load_sequence(res['region_dir'], FIRST, LAST)
    rect0 = seq[0].rect(ROI[0])
    rough = rect0.channel('lighting_reflection')[..., 3]
    geo = rough >= 0.0
    got = float(np.median(rough[geo])) if geo.any() else float('nan')
    if expect_roughness is not None and abs(got - expect_roughness) > 0.02:
        raise RuntimeError(f'{label}: the roughness override did not reach the G-buffer '
                           f'(recorded median {got:.3f}, expected {expect_roughness:.3f})')
    out = np.stack([f.rect(ROI[0]).luminance('taa_output') for f in seq]).astype(np.float64)
    raw = np.stack([f.rect(ROI[0]).luminance('taa_raw') for f in seq]).astype(np.float64)
    cycle = raw.reshape(-1, 8, *raw.shape[1:]).mean(axis=1).mean(axis=0)
    dark = cycle <= np.percentile(cycle, 10)
    bright = cycle >= np.percentile(cycle, 90)
    stats = {
        'recorded_roughness_median': got,
        'std_mean': float(out.std(axis=0).mean()),
        'roi_mean': float(out.mean()),
        'error_vs_reference': float(np.abs(out - cycle[None]).mean()),
        'bright_energy_ratio': float(out[:, bright].mean() / cycle[bright].mean()),
        'black_pixel_output': float(out[:, dark].mean()),
        'black_pixel_correct': float(cycle[dark].mean()),
    }
    shutil.rmtree(res['region_dir'], ignore_errors=True)
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '14-validate-flicker-correction'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(args.out, exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_material_class')
    payload = {'roi': ROI, 'frames': FRAMES, 'first_frame': FIRST, 'classes': {}}

    for cname, (cvars, expect) in CLASSES.items():
        entry = {'cvars': cvars, 'variants': {}}
        for vname, exponent in VARIANTS:
            label = f'{cname}_{vname}'.replace(' ', '_').replace('(', '').replace(')', '') \
                                     .replace('=', '').replace(',', '').replace('.', 'p')
            print(f'  … {cname} {vname}', flush=True)
            entry['variants'][vname] = measure(work, label, cvars, exponent, expect)
        b, c = entry['variants']['baseline'], entry['variants']['candidate_0.0']
        entry['std_change_pct'] = (c['std_mean'] / b['std_mean'] - 1) * 100 if b['std_mean'] else None
        entry['error_change_pct'] = ((c['error_vs_reference'] / b['error_vs_reference'] - 1) * 100
                                     if b['error_vs_reference'] else None)
        # Validity control: forcing a material class must actually change the rendered image. The
        # first version of this study ran on the pose W window, where the override reached the
        # G-buffer but moved the ROI mean by 0.0007 % — that rectangle is lit by the sky through
        # translucent geometry, which never enters the G-buffer, so the comparison measured
        # nothing while looking like a clean pass.
        if cname == 'scene (no override)':
            entry['image_change_pct'] = 0.0
            entry['vacuous'] = False
        else:
            ref_mean = payload['classes']['scene (no override)']['variants']['baseline']['roi_mean']
            entry['image_change_pct'] = (b['roi_mean'] / ref_mean - 1) * 100
            entry['vacuous'] = abs(entry['image_change_pct']) < 1.0
        payload['classes'][cname] = entry
        if entry.get('vacuous'):
            print(f"      WARNING: {cname} moved the image by only "
                  f"{entry['image_change_pct']:+.4f} % — this ROI's radiance does not depend on "
                  f"the material, so the comparison is vacuous and must not be reported as "
                  f"coverage", flush=True)
        print(f"      roughness {b['recorded_roughness_median']:.3f} | "
              f"std {b['std_mean']:.4e} -> {c['std_mean']:.4e} ({entry['std_change_pct']:+.1f} %) | "
              f"error vs reference {b['error_vs_reference']:.4f} -> {c['error_vs_reference']:.4f} "
              f"({entry['error_change_pct']:+.1f} %) | bright energy "
              f"{b['bright_energy_ratio']:.4f} -> {c['bright_energy_ratio']:.4f}", flush=True)

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'material-classes.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
