"""Re-reads a `scripts/test_gi_energy_gpu.ps1` log and puts the two writers' energy on the same probes.

The harness's ratio (`ratio DDGI/SDF=`, the D4 gate of `scripts/test_ddgi_dedup.py`) divides the two
engine grid lines, which average different populations: `RC transport: grid` keeps the probes whose
field distance is >= 5 cm (`radiance_cache.cpp` ReadDiagnosticResults), `DDGI transport: grid` keeps
every probe with finite records (`ddgi_system.cpp` ReadDiagnosticResults). Under probe classification
mode 3 (`sample/projects/sponza.uzproject`) the RC population also holds unreachable probes (class 7)
that are never traced and log meanRadiance 0. This script pairs the per-probe lines by index and
reports the ratio over one population at a time. Read-only, no GPU; reported, no verdict.

Caveat: at the snapshot frame the cache's per-probe statistic comes from the frame each probe was last
traced (`r.GI.RadianceCache.ProbesPerFrame` slices) while the DDGI traces every probe every frame, so a
per-probe ratio pairs two different 32-ray sets; the medians are the robust reading, the tails are not.

Populations (followup-01): valid = RC class 8/9/10 (mode 3: reached, traced from its climb, relocated);
building = cells x 5..12 and z 7..14 (H-64: the other 3 072 of the 4 096 probes lie outside it).
Only class 8 pairs two traces from the same point (2026-10-09): the cache traces class 9 from the end of
its climb and class 10 from its relocated position, the DDGI every probe from its grid position, so the
inside-the-building population is also reported on class 8 alone.

Usage: python scripts/gi_energy_populations.py LOG [LOG ...]
       (LOG = scripts/test_gi_energy_gpu.log, or a copy such as <out>/gi_energy_run.log of test_ddgi_dedup.py)
"""
from __future__ import annotations

import re
import statistics
import sys

NUM = r'([-0-9.eE+]+|nan|inf|-inf)'
RE_RC = re.compile(r'RC transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) fieldDistance=' + NUM
                   + r' .*?meanRadiance=' + NUM + r' .*?class=(-?\d+)')
RE_DDGI = re.compile(r'DDGI transport: probe=(\d+) cell=\((\d+),(\d+),(\d+)\) world=\([^)]*\) skyFraction='
                     + NUM + r' meanRadiance=' + NUM)
RE_GRID = re.compile(r'(RC|DDGI) transport: grid [^\r\n]*?probes=(\d+) meanRadiance=' + NUM)
DARK = 0.05   # per-probe ratios only where the cache's mean exceeds this (as R1 of H-61)


def read_text(path: str) -> str:
    raw = open(path, 'rb').read()
    # PowerShell 5.1 redirection writes UTF-16 LE with a BOM.
    encoding = 'utf-16' if raw[:2] in (b'\xff\xfe', b'\xfe\xff') else 'utf-8-sig'
    return raw.decode(encoding, errors='replace')


def in_building(cell) -> bool:
    return 5 <= cell[0] <= 12 and 7 <= cell[2] <= 14


def summary(probes, rc, ddgi) -> str:
    probes = [p for p in probes if p in ddgi]
    if not probes:
        return 'n 0'
    mean_rc = statistics.fmean(rc[p]['mean'] for p in probes)
    mean_ddgi = statistics.fmean(ddgi[p] for p in probes)
    ratio = f'{mean_ddgi / mean_rc:.4f}' if mean_rc > 0 else 'n/a (cache mean 0)'
    per = sorted(ddgi[p] / rc[p]['mean'] for p in probes if rc[p]['mean'] > DARK)
    text = f'n {len(probes)}: mean DDGI {mean_ddgi:.4f} / RC {mean_rc:.4f} = {ratio}'
    if len(per) >= 10:
        deciles = statistics.quantiles(per, n=10)
        text += (f'; per probe (RC mean > {DARK}, n {len(per)}) median {statistics.median(per):.4f}'
                 f', p10 {deciles[0]:.4f}, p90 {deciles[-1]:.4f}')
    return text


def analyse(path: str) -> int:
    text = read_text(path)
    rc = {int(m.group(1)): {'cell': (int(m.group(2)), int(m.group(3)), int(m.group(4))),
                            'field': float(m.group(5)), 'mean': float(m.group(6)), 'cls': int(m.group(7))}
          for m in RE_RC.finditer(text)}
    ddgi = {int(m.group(1)): float(m.group(6)) for m in RE_DDGI.finditer(text)}
    grids = {m.group(1): (int(m.group(2)), float(m.group(3))) for m in RE_GRID.finditer(text)}
    print(path)
    if not rc or not ddgi or set(grids) != {'RC', 'DDGI'}:
        print(f'  missing lines: RC probes {len(rc)}, DDGI probes {len(ddgi)}, grid lines {sorted(grids)}')
        return 1
    (n_rc, g_rc), (n_ddgi, g_ddgi) = grids['RC'], grids['DDGI']
    print(f'  harness definition (engine grid lines): RC {g_rc:.4f} over {n_rc} probes, '
          f'DDGI {g_ddgi:.4f} over {n_ddgi} probes, ratio {g_ddgi / g_rc:.4f}')
    field_ok = [p for p, r in rc.items() if r['field'] >= 5.0]
    unreached = [p for p in field_ok if rc[p]['cls'] == 7]
    print(f'  RC grid population (field >= 5 cm): {len(field_ok)} probes, {len(unreached)} of them class 7 '
          f'(unreachable, cache mean {statistics.fmean(rc[p]["mean"] for p in unreached) if unreached else 0.0:.4f})')
    print(f'  same probes, RC grid population: {summary(field_ok, rc, ddgi)}')
    valid = [p for p, r in rc.items() if r['cls'] in (8, 9, 10)]
    print(f'  same probes, RC valid (class 8/9/10): {summary(valid, rc, ddgi)}')
    print(f'    inside the building: {summary([p for p in valid if in_building(rc[p]["cell"])], rc, ddgi)}')
    same_origin = [p for p in valid if rc[p]['cls'] == 8 and in_building(rc[p]['cell'])]
    print(f'    inside, class 8 only (both writers trace from the grid position): {summary(same_origin, rc, ddgi)}')
    print(f'    outside the building: {summary([p for p in valid if not in_building(rc[p]["cell"])], rc, ddgi)}')
    return 0


def main(argv) -> int:
    if not argv:
        print(__doc__)
        return 2
    return max(analyse(path) for path in argv)


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
