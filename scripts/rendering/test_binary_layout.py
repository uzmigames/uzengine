"""The BINARY must write the layout the sources declare.

`scripts/rendering/test_region_format.py` checks that the Python reader, the C++ header and the HLSL header
agree with each other. All three can agree and still be irrelevant, because what analyses read is
what the compiled engine wrote — and on 2026-09-10 those came apart: a change to
`region_dump_layout.h` was not picked up by the build, `zig build` reported success and rebuilt
nothing, and a capture was analysed against a layout the binary was not using. It failed loudly only
because the record count had grown; a change that swapped two records of equal size would have been
read as data.

Two things made that possible and both are worth stating:

  * the build does not treat headers as dependencies, so editing only a header can leave the binary
    stale while the build still succeeds;
  * `zig build 2>&1 | tail -n` followed by `$?` reports the exit status of `tail`, not of the build.
    Every such check is vacuous.

This closes the loop: it runs the engine for three frames and requires the file it produced to carry
the record count and the per-group layout that the sources declare. Run it after any change to the
region-dump format, and after any build whose result matters.

Usage: python scripts/rendering/test_binary_layout.py
"""
from __future__ import annotations

import os
import re
import shutil
import struct
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump as rd  # noqa: E402
import runner  # noqa: E402

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def main():
    print('the binary writes the layout the sources declare')
    runner.require_exe()
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

    cpp = open(os.path.join(repo, 'runtime/engine/renderer/frame/region_dump_layout.h'),
               encoding='utf-8').read()
    m = re.search(r'kRegionGroupRecords\[6\] = \{([^}]*)\}', cpp)
    source_layout = tuple(int(v.strip().rstrip('u')) for v in m.group(1).split(','))
    check(source_layout == rd.CURRENT_LAYOUT,
          'the C++ header and the Python reader agree on the layout',
          f'{source_layout} vs {rd.CURRENT_LAYOUT}')

    work = os.path.join(tempfile.gettempdir(), 'uz_binary_layout')
    fields = 1 | 2 | 16 | 32          # the `gather` preset: every group this test can exercise
    res = runner.run_capture(work, os.path.join(repo, 'scripts', 'rendering', 'sponza-flicker-w.uzproject'), 60,
                             regions=[('probe', 150, 260, 8, 8)], region_fields='gather',
                             region_first=50, region_last=52, timing_first=40, clean=True)
    if res['exit_code'] != 0:
        check(False, 'the engine ran', f'exit {res["exit_code"]}')
        return 1

    files = sorted(f for f in os.listdir(res['region_dir']) if f.endswith('.bin'))
    check(bool(files), 'the capture produced region files', f'{len(files)} files')
    if not files:
        return 1

    raw = open(os.path.join(res['region_dir'], files[0]), 'rb').read(64)
    head = struct.unpack_from('<12I', raw, 0)
    version, file_fields, rpp = head[1], head[4], head[5]
    packed = struct.unpack_from('<I', raw, 60)[0]

    expected_rpp = sum(n for i, n in enumerate(source_layout) if fields & (1 << i))
    check(file_fields == fields, 'the binary honoured the requested field mask',
          f'{file_fields} vs {fields}')
    check(rpp == expected_rpp,
          'the binary writes the record count the sources declare',
          f'file {rpp}, sources {expected_rpp} — a mismatch means the build is stale')

    declared = rd.unpack_layout(packed, version)
    want = tuple(n if fields & (1 << i) else 0 for i, n in enumerate(source_layout))
    check(declared == want, 'the binary declares the per-group layout the sources declare',
          f'{declared} vs {want}')

    # and the reader must be able to read what the binary wrote, at the group it claims
    frame = rd.load_frame(os.path.join(res['region_dir'], files[0]))
    reachable = []
    for cname, (bit, rec, _) in rd.CHANNELS.items():
        if not (frame.fields & bit):
            continue
        try:
            frame.rect('probe').channel(cname)
            reachable.append(cname)
        except Exception as e:                                    # noqa: BLE001
            check(False, f'channel {cname} is readable from the file the binary wrote', str(e))
    check(len(reachable) == sum(1 for _, (bit, _, _) in rd.CHANNELS.items() if frame.fields & bit),
          'every channel of every present group is readable from the binary\'s own file',
          f'{len(reachable)} channels')

    shutil.rmtree(work, ignore_errors=True)
    print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
