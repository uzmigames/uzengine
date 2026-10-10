"""H-28 step 3: separate the gather's effect from the AO/specular interference it drags along.

The re-run (H-28b) was voided by its isolation control: freezing the gather also moved specular (at
the same frame the frozen texture is first reused) and AO (a nine-frame transient). Ratios between
those changes and the oscillation drop bound orders of magnitude but are not causal shares, so they
cannot settle whether the interference carries the result. This does, on the GPU, with the matrix
fixed beforehand:

| condition | gather | AO and specular |
|---|---|---|
| reference | normal | normal |
| substitution control | normal | replayed from the reference |
| original intervention | frozen | normal |
| isolated intervention | frozen | replayed from the reference |

The replay is per frame and per pixel, taken from *this repeat's own reference capture*, so the
substitution reproduces the reference exactly rather than approximately, and holding one frozen
frame — a second temporal change — is avoided.

Thresholds, from the ledger's pre-registration:

  substitution control   must reproduce the reference: oscillation within 5 %, energy within 0.5 %.
                         If it does not, the mechanism alters the result by itself and nothing below
                         it can be read.
  isolated intervention  at least 80 % of the original reduction must survive — the original is
                         8.621e-06 -> 1.564e-06, so row 4 must fall by at least 5.65e-06 — in every
                         repeat, with energy within 1 % of the reference.
  per frame              the frame-by-frame difference is reported for every row, so a transient
                         cannot be averaged out of view.

Usage: python scripts/rendering/run_h28_isolation.py [--repeats 3] [--frames 128]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump  # noqa: E402
import runner  # noqa: E402

SCENE_START = 37
FIRST = SCENE_START + 200
PROJECT = os.path.join(runner.REPO, 'scripts', 'rendering', 'sponza-flicker-w.uzproject')
ROI = ('shadow_wall', 150, 260, 192, 384)
BASE = {'r.AutoExposure': 0, 'r.Exposure': 1.0, 'r.TAA.JitterSequenceLength': 8}
ORIGINAL_DROP = 8.621e-06 - 1.564e-06          # what H-28b measured, the bar row 4 must clear
SURVIVE_FRACTION = 0.80
#: The replay covers the whole rect; its edge pixels differ from the reference by construction
#: because the TAA neighbourhood reaches outside it. Statistics are taken on the interior.
EDGE = 2


def capture(work, label, cvars, replay=None):
    extra = ('--component-replay', replay) if replay else ()
    res = runner.run_capture(os.path.join(work, label), PROJECT,
                             FIRST + ARGS.frames - 1 - SCENE_START + 8, regions=[ROI],
                             region_fields='components', region_first=FIRST,
                             region_last=FIRST + ARGS.frames - 1, cvars={**BASE, **cvars},
                             extra_args=extra, timing_first=FIRST)
    if res['exit_code'] != 0:
        raise RuntimeError(f'{label}: engine exited {res["exit_code"]} (see {res["log"]})')
    return res


def read(region_dir):
    seq = regiondump.load_sequence(region_dir, FIRST, FIRST + ARGS.frames - 1)
    r = [f.rect(ROI[0]) for f in seq]
    inner = (slice(None), slice(EDGE, -EDGE), slice(EDGE, -EDGE))
    return {
        'frames': np.array([f.frame for f in seq]),
        'output': np.stack([x.luminance('taa_output') for x in r]).astype(np.float64)[inner],
        'ao': np.stack([x.channel('diffuse_indirect')[..., 3] for x in r]).astype(np.float64)[inner],
        'spec': np.stack([x.channel('specular_indirect')[..., :3]
                          for x in r]).astype(np.float64)[inner + (slice(None),)],
    }


def main():
    global ARGS
    ap = argparse.ArgumentParser()
    ap.add_argument('--repeats', type=int, default=3)
    ap.add_argument('--frames', type=int, default=128)
    ap.add_argument('--out', default=os.path.join(runner.REPO, '.screenshots', 'flicker-elimination',
                                                  '16-gather-freeze'))
    ARGS = ap.parse_args()
    runner.require_exe()
    freeze = FIRST - 8
    work = os.path.join(os.environ.get('TEMP', '/tmp'), 'uz_h28_iso')
    replay_dir = os.path.join(ARGS.out, 'replay')
    os.makedirs(replay_dir, exist_ok=True)
    payload = {'roi': ROI, 'first_frame': FIRST, 'frames': ARGS.frames, 'freeze_after': freeze,
               'edge_excluded': EDGE, 'original_drop': ORIGINAL_DROP,
               'survive_fraction': SURVIVE_FRACTION, 'repeats': {}}

    for rep in range(ARGS.repeats):
        print(f'  == repeat {rep + 1}/{ARGS.repeats} ==', flush=True)
        print('  ... reference', flush=True)
        ref = capture(work, f'reference_{rep}', {})
        replay = os.path.join(replay_dir, f'shadow_wall_{rep}.bin')
        subprocess.run([sys.executable,
                        os.path.join(runner.REPO, 'scripts', 'rendering', 'make_component_replay.py'),
                        '--raw', ref['region_dir'], '--roi', ROI[0], '--out', replay],
                       check=True, stdout=subprocess.DEVNULL)
        data = {'reference': read(ref['region_dir'])}
        shutil.rmtree(ref['region_dir'], ignore_errors=True)

        for label, cvars, use_replay in (
                ('substitution_control', {}, True),
                ('original_intervention', {'r.GI.FinalGather.FreezeAfterFrame': freeze}, False),
                ('isolated_intervention', {'r.GI.FinalGather.FreezeAfterFrame': freeze}, True)):
            print(f'  ... {label}', flush=True)
            res = capture(work, f'{label}_{rep}', cvars, replay if use_replay else None)
            data[label] = read(res['region_dir'])
            shutil.rmtree(res['region_dir'], ignore_errors=True)

        ref_d = data['reference']
        entry = {}
        for label, d in data.items():
            entry[label] = {
                'oscillation': float(d['output'].std(axis=0).mean()),
                'energy': float(d['output'].mean()),
                'ao_delta_vs_reference': float(np.abs(d['ao'] - ref_d['ao']).mean()),
                'spec_delta_vs_reference': float(np.abs(d['spec'] - ref_d['spec']).mean()),
                'per_frame_delta': [float(v) for v in
                                    np.abs(d['output'] - ref_d['output']).mean(axis=(1, 2))],
            }
        payload['repeats'][rep] = entry
        r0, sc = entry['reference'], entry['substitution_control']
        oi, ii = entry['original_intervention'], entry['isolated_intervention']
        print(f"      reference             oscillation {r0['oscillation']:.4e} energy {r0['energy']:.6f}")
        print(f"      substitution control  oscillation {sc['oscillation']:.4e} "
              f"({(sc['oscillation'] / r0['oscillation'] - 1) * 100:+.2f} %) energy "
              f"({(sc['energy'] / r0['energy'] - 1) * 100:+.3f} %), AO delta "
              f"{sc['ao_delta_vs_reference']:.2e}, specular delta {sc['spec_delta_vs_reference']:.2e}")
        print(f"      original intervention oscillation {oi['oscillation']:.4e} "
              f"(drop {r0['oscillation'] - oi['oscillation']:.3e})")
        print(f"      isolated intervention oscillation {ii['oscillation']:.4e} "
              f"(drop {r0['oscillation'] - ii['oscillation']:.3e})", flush=True)

    # ---- verdict ------------------------------------------------------------
    reps = payload['repeats']
    sub_ok = all(abs(reps[r]['substitution_control']['oscillation']
                     / reps[r]['reference']['oscillation'] - 1) <= 0.05
                 and abs(reps[r]['substitution_control']['energy']
                         / reps[r]['reference']['energy'] - 1) <= 0.005
                 for r in reps)
    drops = {r: reps[r]['reference']['oscillation'] - reps[r]['isolated_intervention']['oscillation']
             for r in reps}
    survive_ok = all(d >= SURVIVE_FRACTION * ORIGINAL_DROP for d in drops.values())
    energy_ok = all(abs(reps[r]['isolated_intervention']['energy']
                        / reps[r]['reference']['energy'] - 1) <= 0.01 for r in reps)
    payload['substitution_control_ok'] = bool(sub_ok)
    payload['reduction_survives'] = bool(survive_ok)
    payload['energy_ok'] = bool(energy_ok)
    payload['isolated_drops'] = {str(k): v for k, v in drops.items()}
    # Reported alongside the pre-registered absolute threshold, not instead of it: the ratio of the
    # isolated drop to the original drop *within the same run* is immune to the measurement window,
    # which the absolute figure is not.
    payload['survival_ratio'] = {
        str(r): (drops[r] / (reps[r]['reference']['oscillation']
                             - reps[r]['original_intervention']['oscillation']))
        for r in reps}
    payload['verdict'] = (
        'invalid — the substitution mechanism changes the result by itself' if not sub_ok
        else 'supported — the reduction survives isolation' if (survive_ok and energy_ok)
        else 'rejected — the reduction does not survive isolation')
    print(f"\n  substitution control reproduces the reference: {sub_ok}")
    print(f"  isolated drops: " + ', '.join(f'{v:.3e}' for v in drops.values())
          + f" (need >= {SURVIVE_FRACTION * ORIGINAL_DROP:.3e})")
    print(f"  energy within 1 %: {energy_ok}")
    print('  within-run survival (isolated drop / original drop): '
          + ', '.join(f'{v * 100:.1f} %' for v in payload['survival_ratio'].values()))
    print(f"  verdict by the preregistered rule: {payload['verdict']}")

    payload['source_fingerprint'] = runner.source_fingerprint()
    path = os.path.join(ARGS.out, 'h28-isolation.json')
    with open(path, 'w', encoding='utf-8') as fh:
        json.dump(payload, fh, indent=2)
    print('\nwrote', path)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
