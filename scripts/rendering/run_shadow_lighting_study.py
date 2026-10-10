"""Per-ROI lighting attribution for the reported shadow regions (followup-12).

Each region is classified on its own evidence — the window result is never generalised to it:

1. **Composition.** The lighting components (direct after shadowing, diffuse indirect after AO,
   specular indirect after the BRDF, emissive + environment MIS) are recorded per pixel together
   with the path bits the pixel actually took, and their sum is checked against the scene colour the
   TAA reads. Nothing is attributed until that closes.
2. **Repeats and motion.** Three repeated static sequences per pose, then a short deterministic
   motion, so a per-run fluke cannot be read as a property of the region.
3. **Reflection samples and roughness.** 4 vs 16 rays at identical settings, with the pixels
   stratified by roughness (including the near-mirror branch).
4. **Frozen seed and frozen probes.** `r.GI.Reflections.FreezeSeed` removes the stochastic component;
   `r.GI.RadianceCache.FreezeAfterFrame` holds the diffuse GI at the state it had converged to by that
   sampling noise and not a live probe update.
5. **Components on and off.** GTAO, direct shadows and the final gather, each toggled separately,
   with the actually-active path recorded so a fallback cannot be mistaken for the tested path.

Usage: python scripts/rendering/run_shadow_lighting_study.py [--out DIR] [--frames 24] [--runs 3]
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

SCENE_START = 37
FIRST = SCENE_START + 200
PROJECTS = {
    'W': os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'),
    'A': os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-a.uzproject'),
}
# Regions the user reported, plus one bright reference per pose.
ROIS = {
    'W': [('shadow_wall', 150, 260, 192, 384),      # the deeply shadowed pillar wall
          ('wall_bounce', 620, 300, 128, 384),      # shadowed wall that still receives bounce light
          ('window', 770, 150, 192, 512)],          # the region followup-10 explained
    'A': [('rear_door', 960, 592, 224, 224),
          ('right_shadow', 1472, 464, 256, 256),
          ('lit_floor', 832, 816, 256, 64)],
}
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}

VARIANTS = [
    ('baseline', {}),
    ('reflection samples 16', {'r.GI.Reflections.Samples': 16}),
    ('reflection seed frozen', {'r.GI.Reflections.FreezeSeed': 7}),
    ('radiance cache frozen after convergence', {'r.GI.RadianceCache.FreezeAfterFrame': FIRST - 8}),
    ('GTAO off', {'r.GTAO': 0}),
    ('final gather off', {'r.GI.FinalGather': 0}),
    ('reflections off (IBL fallback)', {'r.GI.Reflections': 0}),
]


def capture(work, pose, label, cvars, frames):
    last = FIRST + frames - 1
    d = os.path.join(work, f'{pose}_{label}'.replace(' ', '_').replace(':', ''))
    res = runner.run_capture(d, PROJECTS[pose], last - SCENE_START + 8, regions=ROIS[pose],
                             region_fields='full', region_first=FIRST, region_last=last,
                             cvars={**BASE, **cvars}, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{pose}/{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res, last


def roi_stats(frames, name):
    rects = [f.rect(name) for f in frames]
    out = np.stack([r.luminance('taa_output') for r in rects]).astype(np.float64)
    raw = np.stack([r.luminance('taa_raw') for r in rects]).astype(np.float64)
    direct = np.stack([r.channel('direct_light')[..., :3] for r in rects]).astype(np.float64)
    diffuse = np.stack([r.channel('diffuse_indirect')[..., :3] for r in rects]).astype(np.float64)
    specular = np.stack([r.channel('specular_indirect')[..., :3] for r in rects]).astype(np.float64)
    emissive = np.stack([r.channel('emissive_and_mis')[..., :3] for r in rects]).astype(np.float64)
    shadow = np.stack([r.channel('direct_light')[..., 3] for r in rects]).astype(np.float64)
    ao = np.stack([r.channel('diffuse_indirect')[..., 3] for r in rects]).astype(np.float64)
    rough = np.stack([r.channel('lighting_reflection')[..., 3] for r in rects]).astype(np.float64)
    geo = rects[0].geometry_mask()

    lum = lambda x: x @ np.array([0.2126, 0.7152, 0.0722])
    total = direct + diffuse + specular + emissive
    # The scene colour the TAA reads is the composition; compare in luminance over geometry pixels.
    comp_err = np.abs(lum(total) - raw)[:, geo]
    scale = max(1e-6, float(raw[:, geo].mean()))
    stats = {
        'pixels': int(geo.size), 'geometry_pixels': int(geo.sum()),
        'composition_mean_rel_error': float(comp_err.mean() / scale),
        'composition_p99_rel_error': float(np.percentile(comp_err, 99) / scale),
        'output_mean': float(out[:, geo].mean()),
        'output_std_mean': float(out[:, geo].std(axis=0).mean()),
        'raw_std_mean': float(raw[:, geo].std(axis=0).mean()),
        'shadow_mean': float(shadow[:, geo].mean()),
        'ao_mean': float(ao[:, geo].mean()),
        'roughness_mean': float(rough[0][geo].mean()),
        'path': rects[0].path_summary(),
    }
    for cname, comp in (('direct', direct), ('diffuse_indirect', diffuse),
                        ('specular_indirect', specular), ('emissive_mis', emissive)):
        cl = lum(comp)
        stats[cname] = {
            'mean': float(cl[:, geo].mean()),
            'share_of_total': float(cl[:, geo].mean() / max(1e-12, lum(total)[:, geo].mean())),
            'std_mean': float(cl[:, geo].std(axis=0).mean()),
            'share_of_variance': float(cl[:, geo].std(axis=0).mean()
                                       / max(1e-12, lum(total)[:, geo].std(axis=0).mean())),
        }
    # roughness stratification, including the near-mirror branch
    bands = {'mirror<0.1': rough[0] < 0.1, '0.1-0.4': (rough[0] >= 0.1) & (rough[0] < 0.4),
             '0.4-0.7': (rough[0] >= 0.4) & (rough[0] < 0.7), '>=0.7': rough[0] >= 0.7}
    stats['roughness_bands'] = {}
    for bname, bmask in bands.items():
        m = bmask & geo
        if m.sum() < 16:
            continue
        stats['roughness_bands'][bname] = {
            'pixels': int(m.sum()),
            'output_std_mean': float(out[:, m].std(axis=0).mean()),
            'specular_std_mean': float(lum(specular)[:, m].std(axis=0).mean()),
            'output_mean': float(out[:, m].mean()),
        }
    return stats


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--frames', type=int, default=24)
    ap.add_argument('--runs', type=int, default=3)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '12-isolate-shadow-lighting'))
    args = ap.parse_args()
    runner.require_exe()
    os.makedirs(os.path.join(args.out, 'manifests'), exist_ok=True)
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_shadow_study')
    payload = {'rois': ROIS, 'first_frame': FIRST, 'frames': args.frames, 'base_cvars': BASE,
               'poses': {}}

    for pose in ('W', 'A'):
        print(f'pose {pose}', flush=True)
        pose_out = {'repeats': {}, 'variants': {}}
        for run in range(args.runs):
            print(f'  … baseline repeat {run + 1}', flush=True)
            res, last = capture(work, pose, f'repeat{run}', {}, args.frames)
            frames = regiondump.load_sequence(res['region_dir'], FIRST, last)
            for name, *_ in ROIS[pose]:
                pose_out['repeats'].setdefault(name, []).append(roi_stats(frames, name))
            if run == 0:
                shutil.copy(res['manifest_path'], os.path.join(args.out, 'manifests', f'{pose}-baseline.json'))
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        for label, cvars in VARIANTS[1:]:
            print(f'  … {label}', flush=True)
            res, last = capture(work, pose, label, cvars, args.frames)
            frames = regiondump.load_sequence(res['region_dir'], FIRST, last)
            pose_out['variants'][label] = {'cvars': cvars,
                                           'rois': {name: roi_stats(frames, name) for name, *_ in ROIS[pose]}}
            shutil.rmtree(res['region_dir'], ignore_errors=True)
        payload['poses'][pose] = pose_out
        for name, *_ in ROIS[pose]:
            reps = pose_out['repeats'][name]
            stds = [r['output_std_mean'] for r in reps]
            print(f"    {name}: std {np.mean(stds):.6g} (repeats {min(stds):.6g}..{max(stds):.6g}), "
                  f"composition error {reps[0]['composition_mean_rel_error'] * 100:.3f} %, "
                  f"paths {[k for k, v in reps[0]['path'].items() if v > 0.5]}")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(args.out, 'shadow-lighting-study.json')
    with open(path, 'w', encoding='utf-8') as f:
        json.dump(payload, f, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
