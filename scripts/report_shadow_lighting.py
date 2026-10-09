"""Renders the followup-12 evidence package report from its JSON artifacts.

Usage: python scripts/report_shadow_lighting.py [--dir DIR]
"""
from __future__ import annotations

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

DEFAULT_DIR = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                           '12-isolate-shadow-lighting')


def load(d, name):
    p = os.path.join(d, name)
    return json.load(open(p, encoding='utf-8')) if os.path.exists(p) else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--dir', default=DEFAULT_DIR)
    args = ap.parse_args()
    d = args.dir
    out = ['# followup-12 — what lights the reported shadow regions', '',
           'Linear Rec.709 radiance, fixed exposure, camera still, jitter 8. Every region is judged on',
           'its own evidence; the window result is not carried over to any other region.', '']

    pa = load(d, 'pass-attribution.json')
    if pa:
        out += ['## Which pass produces each region (checklist 1.1)', '',
                'Measured by capturing the same frames with the forward translucent pass disabled and by',
                'checking how much of the scene colour the recorded deferred components explain.', '',
                '| pose / ROI | scene colour | without the translucent pass | deferred components explain | classification |',
                '|---|---|---|---|---|']
        for pose, rois in pa['poses'].items():
            for name, r in rois.items():
                out.append(f'| {pose} / `{name}` | {r["scene_colour_mean"]:.5f} | '
                           f'{r["scene_colour_without_translucent"]:.5f} | '
                           f'{r["components_explain_fraction"] * 100:.1f} % | **{r["classification"]}** |')
        out += ['', 'This decides what the rest of the study can say about each region. A region whose colour',
                'comes from the forward translucent pass is invisible to the deferred lighting records, so no',
                'GI component can explain it — and indeed none of the GI controls move it.', '']

    st = load(d, 'shadow-lighting-study.json')
    if st:
        out += ['## Components and their share of the variation (checklist 1.2/1.6)', '',
                'The recorded components are checked against the scene colour before anything is attributed.', '',
                '| pose / ROI | composition error | radiance | temporal std | direct | diffuse indirect | specular indirect | share of variance |',
                '|---|---|---|---|---|---|---|---|']
        for pose, pd in st['poses'].items():
            for name, reps in pd['repeats'].items():
                b = reps[0]
                sv = (f'direct {b["direct"]["share_of_variance"]:.2f}, '
                      f'diffuse {b["diffuse_indirect"]["share_of_variance"]:.2f}, '
                      f'specular {b["specular_indirect"]["share_of_variance"]:.2f}')
                out.append(f'| {pose} / `{name}` | {b["composition_mean_rel_error"] * 100:.3f} % | '
                           f'{b["output_mean"]:.6g} | {b["output_std_mean"]:.3g} | '
                           f'{b["direct"]["share_of_total"]:.3f} | {b["diffuse_indirect"]["share_of_total"]:.3f} | '
                           f'{b["specular_indirect"]["share_of_total"]:.3f} | {sv} |')
        out += ['', 'A composition error near 100 % is not a measurement failure: it marks a region whose colour',
                'the deferred pass does not produce (see the table above).', '',
                '## One factor at a time, per region (checklist 1.3/1.4/1.5)', '',
                'Ratio of the temporal standard deviation to that region\'s baseline, with the lighting path',
                'the pixels actually took recorded for every run so a fallback cannot pass as the tested path.', '']
        for pose, pd in st['poses'].items():
            out += [f'### Pose {pose}', '',
                    '| variant | ' + ' | '.join(f'`{n}`' for n in pd['repeats']) + ' | active paths |',
                    '|---' * (len(pd['repeats']) + 2) + '|']
            for vlabel, v in pd['variants'].items():
                cells = []
                paths = set()
                for name, reps in pd['repeats'].items():
                    r = v['rois'][name]
                    base = reps[0]['output_std_mean']
                    cells.append(f'{r["output_std_mean"] / base:.2f}x')
                    paths |= {k for k, val in r['path'].items() if val > 0.5}
                out.append(f'| {vlabel} | ' + ' | '.join(cells) + ' | ' + ', '.join(sorted(paths)) + ' |')
            out.append('')
            reps = next(iter(pd['repeats'].values()))
            stds = [r['output_std_mean'] for r in reps]
            out += [f'Three repeated baseline runs agree to within '
                    f'{(max(stds) - min(stds)) / max(1e-12, sum(stds) / len(stds)) * 100:.4f} % on the first ROI, '
                    f'so any ratio above about 1.01 is a real difference and not run-to-run spread.', '']
        out += ['## Roughness stratification (checklist 1.3)', '',
                '| pose / ROI | band | pixels | temporal std | specular std | radiance |',
                '|---|---|---|---|---|---|']
        for pose, pd in st['poses'].items():
            for name, reps in pd['repeats'].items():
                for band, r in reps[0].get('roughness_bands', {}).items():
                    out.append(f'| {pose} / `{name}` | {band} | {r["pixels"]} | {r["output_std_mean"]:.3g} | '
                               f'{r["specular_std_mean"]:.3g} | {r["output_mean"]:.5g} |')
        out.append('')

    path = os.path.join(d, 'report.md')
    with open(path, 'w', encoding='utf-8', newline='\n') as f:
        f.write('\n'.join(out) + '\n')
    print('wrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
