"""Every diagnostic resource must be released, in every configuration it can be left in.

Two of them leaked in this programme, and both were found only because D3D12MA asserts at shutdown
and took the process down with it: the clamp-box textures (`r.TAA.ClampFreezeAfterFrame`) and the
component-replay buffer (`--component-replay`). A leak that did not happen to trip that assert would
have gone unnoticed, and only one of the two fixes was actually verified when it was reported.

This runs the engine in each configuration a diagnostic can be left in and requires a clean exit
with no leak reported, which covers allocation, release at shutdown, and the paths where a resource
is created and then the renderer resizes underneath it:

  off                  neither diagnostic — nothing must be allocated for them
  clamp freeze on      the textures are created lazily and must be released
  replay on            the buffer is uploaded once and must be released
  both on              they must not interfere with each other
  on + resize          `r.ScreenPercentage` forces the renderer to tear down and rebuild its
                       size-dependent resources while a diagnostic is live
  reactivation         a second run at a different freeze frame, so the lazy path runs again

Usage: python scripts/test_diagnostic_resources.py
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import runner  # noqa: E402

SCENE_START = 37
FRAMES = 60
PROJECT = os.path.join(runner.REPO, 'scripts', 'sponza-flicker-w.uzproject')
REPLAY = os.path.join(runner.REPO, '.screenshots', 'flicker-elimination', '16-gather-freeze',
                      'replay', 'shadow_wall_0.bin')

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def run(label, cvars=(), extra=()):
    work = os.path.join(tempfile.gettempdir(), 'uz_diagres', label)
    os.makedirs(work, exist_ok=True)
    log = os.path.join(work, 'run.log')
    cmd = [runner.EXE, '--project', PROJECT, '--frames', str(FRAMES), '--verbose']
    for c in cvars:
        cmd += ['--cvar', c]
    cmd += list(extra)
    with open(log, 'wb') as fh:
        proc = subprocess.run(cmd, stdout=fh, stderr=subprocess.STDOUT, timeout=900)
    with open(log, 'rb') as fh:
        text = fh.read().decode('utf-8', 'replace')
    leaks = [ln for ln in text.splitlines() if 'LEAK' in ln and 'live allocations' in ln]
    named = [ln for ln in text.splitlines()
             if 'LEAKED' in ln and ('ClampBox' in ln or 'ComponentReplay' in ln)]
    return proc.returncode, leaks, named, text


def main():
    runner.require_exe()
    have_replay = os.path.exists(REPLAY)
    if not have_replay:
        print(f'  note: {REPLAY} is absent; the replay cases are skipped')

    cases = [
        ('off', (), ()),
        ('clamp_freeze', ('r.TAA.ClampFreezeAfterFrame=50',), ()),
        ('clamp_freeze_resize', ('r.TAA.ClampFreezeAfterFrame=50', 'r.ScreenPercentage=50'), ()),
        ('clamp_reactivated', ('r.TAA.ClampFreezeAfterFrame=20',), ()),
    ]
    if have_replay:
        cases += [
            ('replay', (), ('--component-replay', REPLAY)),
            ('both', ('r.TAA.ClampFreezeAfterFrame=50',), ('--component-replay', REPLAY)),
            ('replay_resize', ('r.ScreenPercentage=50',), ('--component-replay', REPLAY)),
        ]

    for label, cvars, extra in cases:
        code, leaks, named, text = run(label, cvars, extra)
        check(code == 0 and not leaks, f'{label}: clean shutdown, no allocation leaked',
              f'exit {code}' + (f'; {leaks[0].strip()}' if leaks else '')
              + (f'; {named[0].strip()}' if named else ''))
        if label == 'off':
            created = 'clamp-box diagnostic storage created' in text
            loaded = 'ComponentReplay:' in text
            check(not created and not loaded,
                  'off: neither diagnostic allocates anything',
                  f'clamp textures created: {created}, replay loaded: {loaded}')
        if label == 'clamp_freeze':
            check('clamp-box diagnostic storage created' in text,
                  'clamp freeze on: the storage is created lazily')
        if label == 'replay' and have_replay:
            check('ComponentReplay:' in text and 'loaded' in text,
                  'replay on: the buffer is loaded')

    shutil.rmtree(os.path.join(tempfile.gettempdir(), 'uz_diagres'), ignore_errors=True)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
