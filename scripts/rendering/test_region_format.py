"""Validates the region-dump reader against synthetic files with known values.

A reader bug shifted every LIGHTING_EXTRA channel by one record for a day (ledger, "A reader bug,
and what it cost"). The check that existed compared two of the reader's own functions to each other,
which cannot catch two copies that are equally wrong and says nothing about files already written.
This builds files whose every channel and every pixel holds a *distinct, predictable* value and
asserts the reader returns exactly those, so a swapped slot, a shifted pixel stride, a mis-sized
group or a truncated file all fail loudly.

  slot mapping     each channel of each group gets its own value; reading channel X must return X
  pixel stride     each pixel gets its own value; reading pixel (x, y) must return that pixel
  layout in file   a format-5 file declares its own layout, and a file whose declared layout
                   disagrees with its records-per-pixel is rejected
  legacy formats   a format-3 file (no layout word) is resolved from the historical table, and an
                   ambiguous or unknown one is rejected rather than guessed at
  truncation       a file cut short is rejected, not silently read as fewer pixels
  three copies     the counts in the Python reader, the C++ header and the HLSL header must agree —
                   checked against the source text, since they are three separate hand-written lists

Usage: python scripts/rendering/test_region_format.py
"""
from __future__ import annotations

import os
import re
import struct
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), 'flicker'))

import regiondump as rd  # noqa: E402

PASS, FAIL = [], []


def check(ok, what, detail=''):
    (PASS if ok else FAIL).append(what)
    print(f'  {"ok  " if ok else "FAIL"} {what}{(" — " + detail) if detail else ""}', flush=True)


def build(fields=59, w=4, h=3, version=5, layout=None, frame=100, truncate=0,
          bad_layout_word=None, stamp_flags=None, gather_stamp=None):
    """A region dump whose every (pixel, record, component) holds a distinct known value."""
    layout = layout or rd.CURRENT_LAYOUT
    rpp = sum(n for i, n in enumerate(layout) if fields & (1 << i))
    packed = 0
    for i, n in enumerate(layout):
        if fields & (1 << i):
            if version >= 5:
                packed |= (n & 0x1f) << (i * 5)
            else:
                packed |= (n & 0x3f) << (i * 6)
    if bad_layout_word is not None:
        packed = bad_layout_word

    # The writer ORs every rect's flags into the header's, so the fixture does too.
    flags = rd.FLAG_TAA_ACTIVE | (stamp_flags or 0)
    head = struct.pack('<12I', rd.MAGIC_V2, version, frame, 1, fields, rpp,
                       flags, 1920, 1080, 1920, 1080, 3)
    head += struct.pack('<3f', 0.125, -0.25, 1.0) + struct.pack('<I', packed)
    assert len(head) == 64
    body = head + (np.zeros(36, dtype=np.float32).tobytes() if version >= 3 else b'')
    body += struct.pack('<16s4I', b'roi', 10, 20, w, h)
    body += struct.pack('<4I', frame, frame, flags, 0)

    # value(pixel, record, component) = pixel*1000 + record*10 + component, exactly recoverable
    data = np.zeros((h * w * rpp + 4, 4), dtype=np.float32)
    for py in range(h):
        for px in range(w):
            p = py * w + px
            for r in range(rpp):
                for c in range(4):
                    data[p * rpp + r, c] = p * 1000 + r * 10 + c
    if gather_stamp is not None:
        data[h * w * rpp + 2] = (
            np.array([gather_stamp], dtype=np.uint32).view(np.float32)[0], 8.0, 6.0, 1.0)
    body += data.tobytes()
    if truncate:
        body = body[:-truncate]
    path = os.path.join(tempfile.mkdtemp(prefix='uz_fmt_'), f'region_{frame}.bin')
    with open(path, 'wb') as f:
        f.write(body)
    return path, layout, rpp


def main():
    print('region dump format and reader')

    # ---- slot mapping and pixel stride ------------------------------------
    path, layout, rpp = build()
    fr = rd.load_frame(path)
    rect = fr.rect('roi')
    ok_slots, ok_pixels, first_bad = True, True, None
    for name, (bit, rec, _) in rd.CHANNELS.items():
        if not (fr.fields & bit):
            continue
        want_rec = fr.offsets[bit] + rec
        got = rect.channel(name)
        for py in (0, 2):
            for px in (0, 3):
                p = py * 4 + px
                expect = p * 1000 + want_rec * 10
                if abs(float(got[py, px, 0]) - expect) > 1e-3:
                    ok_slots = False
                    first_bad = first_bad or f'{name} at ({px},{py}): {got[py, px, 0]} != {expect}'
    check(ok_slots, 'every channel reads the record it is mapped to, at every pixel',
          first_bad or f'{len(rd.CHANNELS)} channels x 4 pixels')

    # a deliberately wrong offset must be visible to this test
    saved = fr.offsets[rd.FIELD_LIGHTING_EXTRA]
    fr.offsets[rd.FIELD_LIGHTING_EXTRA] = saved + 1
    shifted = rect.channel('diffuse_indirect')[0, 0, 0]
    fr.offsets[rd.FIELD_LIGHTING_EXTRA] = saved
    correct = rect.channel('diffuse_indirect')[0, 0, 0]
    check(abs(float(shifted) - float(correct)) > 1e-3,
          'negative control: a one-record shift changes what the test reads',
          f'{correct} vs {shifted} — this is the bug the reader had')

    # ---- a channel newer than the file raises instead of reading the next group ----
    old_layout = tuple(n - 1 if i == 4 else n for i, n in enumerate(rd.CURRENT_LAYOUT))
    older_frame = rd.load_frame(build(layout=old_layout)[0])
    older = older_frame.rect('roi')
    newest = max((rec, name) for name, (bit, rec, _) in rd.CHANNELS.items()
                 if bit == rd.FIELD_LIGHTING_EXTRA)[1]
    try:
        older.channel(newest)
        raised = False
    except KeyError:
        raised = True
    check(raised, 'a channel added after a file was written raises instead of reading the next group',
          f"'{newest}' on a file whose LIGHTING_EXTRA holds {old_layout[4]} records")
    prior = max((rec, name) for name, (bit, rec, _) in rd.CHANNELS.items()
                if bit == rd.FIELD_LIGHTING_EXTRA and rec < old_layout[4])[1]
    got = float(older.channel(prior)[0, 0, 0])
    want = float((older_frame.offsets[rd.FIELD_LIGHTING_EXTRA] + rd.CHANNELS[prior][1]) * 10)
    check(abs(got - want) < 1e-3, 'negative control: the older file still reads its own last record',
          f"'{prior}' reads {got}, expected {want}")

    # ---- the layout travels in the file -----------------------------------
    # Absent groups pack as zero, so only the groups the file actually contains are compared.
    present = tuple(n for i, n in enumerate(fr.layout) if fr.fields & (1 << i))
    want = tuple(n for i, n in enumerate(rd.CURRENT_LAYOUT) if fr.fields & (1 << i))
    check(present == want and fr.version == 5,
          'a format-5 file declares its own layout', f'{fr.layout} -> present {present}')
    try:
        rd.load_frame(build(bad_layout_word=0)[0])
        check(False, 'a format-5 file whose layout disagrees with its record count is rejected')
    except ValueError:
        check(True, 'a format-5 file whose layout disagrees with its record count is rejected')

    # ---- legacy formats ----------------------------------------------------
    old = build(version=3, layout=(2, 5, 1, 3, 4, 0), fields=27)[0]
    fr3 = rd.load_frame(old)
    check(fr3.layout == (2, 5, 1, 3, 4, 0),
          'a format-3 file is resolved to the layout it was written with', f'{fr3.layout}')
    try:
        rd.load_frame(build(version=99)[0])
        check(False, 'an unknown format version is rejected')
    except ValueError:
        check(True, 'an unknown format version is rejected')

    # ---- the gather stamp --------------------------------------------------
    # The dump buffer is never cleared, so a gather that stopped running leaves last frame's
    # records behind; only the stamp separates them from records written this frame.
    stamped = rd.load_frame(build(gather_stamp=100)[0]).rect('roi')
    unstamped = rd.load_frame(build()[0]).rect('roi')
    check(stamped.gather_stamp == 100 and unstamped.gather_stamp == -1,
          'the gather stamp is read from footer record 2, and its absence is reported',
          f'stamped {stamped.gather_stamp}, never written {unstamped.gather_stamp}')
    stale = rd.load_frame(build(stamp_flags=rd.FLAG_GATHER_MISMATCH)[0])
    absent = rd.load_frame(build(stamp_flags=rd.FLAG_GATHER_MISSING)[0])
    check(not stale.valid and not absent.valid,
          'a stale or absent gather stamp invalidates the file',
          f'mismatch valid={stale.valid}, missing valid={absent.valid}')

    # ---- truncation --------------------------------------------------------
    try:
        rd.load_frame(build(truncate=64)[0]).rect('roi').channel('taa_output')
        check(False, 'a truncated file is rejected')
    except Exception as e:
        check(True, 'a truncated file is rejected', type(e).__name__)

    # ---- the three hand-written copies agree -------------------------------
    repo = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
    cpp = open(os.path.join(repo, 'runtime/engine/renderer/frame/region_dump_layout.h'),
               encoding='utf-8').read()
    m = re.search(r'kRegionGroupRecords\[6\] = \{([^}]*)\}', cpp)
    cpp_layout = tuple(int(v.strip().rstrip('u')) for v in m.group(1).split(','))
    hlsli = open(os.path.join(repo, 'runtime/shaders/runtime/common/region_dump.hlsli'),
                 encoding='utf-8').read()
    names = ('LIGHTING', 'TAA', 'OUTPUT', 'TAA_EXTRA', 'LIGHTING_EXTRA', 'GATHER')
    hlsl_layout = tuple(
        int(re.search(rf'#define UZ_REGION_{n}_RECORDS\s+(\d+)u', hlsli).group(1)) for n in names)
    check(cpp_layout == rd.CURRENT_LAYOUT == hlsl_layout,
          'the record counts agree across the Python reader, the C++ header and the HLSL header',
          f'python {rd.CURRENT_LAYOUT}, c++ {cpp_layout}, hlsl {hlsl_layout}')

    print(f'\n{len(PASS)} passed, {len(FAIL)} failed')
    for f in FAIL:
        print(f'  FAILED: {f}')
    return 1 if FAIL else 0


if __name__ == '__main__':
    raise SystemExit(main())
