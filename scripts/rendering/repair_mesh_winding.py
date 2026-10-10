"""Audits and repairs the triangle winding of .uzmesh assets against the engine's convention.

The engine renders clockwise-front (the D3D default: `RHICullMode::Back` with the default front face), so a
triangle (a, b, c) is consistent with its vertex normals when dot(-cross(b - a, c - a), avg(normals)) > 0;
`scripts/rendering/test_decal_lod_normals.ps1` (phase0_sponza-followup-04) audits and repairs simplifier artifacts
with that rule but leaves meshes whose LOD0 is itself inconsistent alone. This script handles the other
class, whole-mesh inversions: a mesh exported counter-clockwise (the glTF / OpenGL convention) whose
every triangle disagrees with its outward normals, so back-face culling keeps the far side and the near
hemisphere disappears (`sample/content/meshes/sphere.uzmesh`: the chrome sphere of chrome_test rendered
as the inside of its far half, block B.5b's finding).

Audit: python scripts/rendering/repair_mesh_winding.py [paths or globs]  (default: every sample mesh)
Repair: python scripts/rendering/repair_mesh_winding.py --repair PATH...  flips every triangle of every LOD of the
  given meshes (the last two indices swapped; vertex data untouched). Refuses a mesh whose LOD0 is not
  at least --min-inverted (default 0.99) inconsistent, so partial inconsistencies stay with the LOD tool.
"""
import argparse
import glob
import os
import struct
import sys

import numpy as np

REPO = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..'))
MAGIC = 0x484D5A55


def read_mesh(path):
    b = bytearray(open(path, 'rb').read())
    if struct.unpack_from('<I', b, 0)[0] != MAGIC:
        return None
    stride = struct.unpack_from('<I', b, 12)[0]
    vcount = struct.unpack_from('<I', b, 16)[0]
    ifmt = struct.unpack_from('<I', b, 20)[0]
    icount = struct.unpack_from('<I', b, 24)[0]
    voff = struct.unpack_from('<Q', b, 56)[0]
    ioff = struct.unpack_from('<Q', b, 64)[0]
    istride = 4 if ifmt == 1 else 2
    v = np.frombuffer(bytes(b[voff:voff + vcount * stride]), dtype=np.float32).reshape(vcount, stride // 4)
    lods = [(0, ioff, icount)]
    for l in range(3):
        off, cnt = struct.unpack_from('<II', b, 76 + l * 8)
        if cnt > 0:
            lods.append((l + 1, ioff + off, cnt))
    return {'bytes': b, 'pos': v[:, :3].astype(np.float64), 'nrm': v[:, 3:6].astype(np.float64), 'istride': istride, 'lods': lods, 'vcount': vcount}


def lod_indices(m, lod):
    _, off, cnt = lod
    dt = np.uint32 if m['istride'] == 4 else np.uint16
    return np.frombuffer(bytes(m['bytes'][off:off + cnt * m['istride']]), dtype=dt).reshape(-1, 3)


def inconsistent_fraction(m, idx):
    ok = (idx < m['vcount']).all(1)
    idx = idx[ok]
    p, n = m['pos'], m['nrm']
    f = -np.cross(p[idx[:, 1]] - p[idx[:, 0]], p[idx[:, 2]] - p[idx[:, 0]])   # clockwise-front outward normal
    a = n[idx].mean(1)
    fl, al = np.linalg.norm(f, axis=-1), np.linalg.norm(a, axis=-1)
    valid = (fl > 1e-12) & (al > 1e-9)
    d = np.sum(f[valid] * a[valid], -1)
    return float((d < 0).mean()) if valid.any() else 0.0, int(valid.sum())


def audit(path):
    m = read_mesh(path)
    if m is None:
        return None
    return [(lod[0],) + inconsistent_fraction(m, lod_indices(m, lod)) for lod in m['lods']]


def repair(path, min_inverted):
    m = read_mesh(path)
    rows = audit(path)
    if rows[0][1] < min_inverted:
        raise SystemExit(f'{path}: LOD0 is {rows[0][1]:.1%} inconsistent, below {min_inverted:.0%}: not a whole-mesh inversion (use test_decal_lod_normals.ps1 for simplifier artifacts)')
    b = m['bytes']
    flipped = 0
    for _, off, cnt in m['lods']:
        dt = np.uint32 if m['istride'] == 4 else np.uint16
        idx = np.frombuffer(bytes(b[off:off + cnt * m['istride']]), dtype=dt).reshape(-1, 3).copy()
        idx[:, [1, 2]] = idx[:, [2, 1]]
        b[off:off + cnt * m['istride']] = idx.astype(dt).tobytes()
        flipped += len(idx)
    open(path, 'wb').write(bytes(b))
    return flipped, audit(path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('paths', nargs='*')
    ap.add_argument('--repair', action='store_true')
    ap.add_argument('--min-inverted', type=float, default=0.99)
    args = ap.parse_args()
    paths = []
    for pat in args.paths or [os.path.join(REPO, 'sample', 'content', 'meshes', '**', '*.uzmesh')]:
        paths += glob.glob(pat, recursive=True) if any(ch in pat for ch in '*?') else [pat]
    bad = 0
    for p in sorted(paths):
        rel = os.path.relpath(p, REPO)
        if args.repair:
            flipped, rows = repair(p, args.min_inverted)
            print(f'repaired {rel}: {flipped} triangles flipped; now ' + ', '.join(f'LOD{l} {f:.1%}' for l, f, _ in rows))
            continue
        rows = audit(p)
        if rows is None:
            continue
        inv = rows[0][1]
        tag = 'INVERTED' if inv >= args.min_inverted else ('mixed' if inv > 0.01 else 'ok')
        if tag != 'ok':
            bad += 1
        print(f'{tag:8s} {rel}: ' + ', '.join(f'LOD{l} {f:.1%} of {t}' for l, f, t in rows))
    if not args.repair:
        print(f'{bad} mesh(es) not clean of {len(paths)}')
    return 0


if __name__ == '__main__':
    sys.exit(main())
