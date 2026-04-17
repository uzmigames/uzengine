"""Inspect a .uzskel binary file.

Standalone parser — does not link against the engine. Mirrors the layout
documented in runtime/engine/animation/skeleton_io.h and the matrix math in
runtime/engine/math/mat4.cpp (row-major DirectXMath, TRS = Scaling * Rot *
Translation).

Run:
    python scripts/debug_inspect_skel.py <path.uzskel>
"""

from __future__ import annotations

import math
import struct
import sys
from dataclasses import dataclass


MAGIC = 0x4B535A55  # 'UZSK' little-endian
HEADER_SIZE = 16


@dataclass
class Bone:
    index: int
    name: str
    parent: int
    pos: tuple[float, float, float]
    rot: tuple[float, float, float, float]  # (x, y, z, w)
    scl: tuple[float, float, float]


def parse(buf: bytes) -> list[Bone]:
    if len(buf) < HEADER_SIZE:
        raise ValueError(f"file too small: {len(buf)} bytes")

    magic, version, bone_count, reserved = struct.unpack_from("<IIII", buf, 0)
    print(f"Header bytes (hex): {buf[:32].hex(' ')}")
    print(f"  magic     = 0x{magic:08X} ({'OK' if magic == MAGIC else 'BAD'} — expected 0x{MAGIC:08X} 'UZSK')")
    print(f"  version   = {version}")
    print(f"  boneCount = {bone_count}")
    print(f"  reserved  = {reserved}")
    print()

    if magic != MAGIC:
        raise ValueError("magic mismatch — not a .uzskel file")

    bones: list[Bone] = []
    off = HEADER_SIZE
    for i in range(bone_count):
        (name_len,) = struct.unpack_from("<H", buf, off)
        off += 2
        name = buf[off:off + name_len].decode("utf-8", errors="replace")
        off += name_len
        (parent,) = struct.unpack_from("<h", buf, off)
        off += 2
        px, py, pz = struct.unpack_from("<fff", buf, off)
        off += 12
        rx, ry, rz, rw = struct.unpack_from("<ffff", buf, off)
        off += 16
        sx, sy, sz = struct.unpack_from("<fff", buf, off)
        off += 12
        bones.append(Bone(i, name, parent, (px, py, pz), (rx, ry, rz, rw), (sx, sy, sz)))

    leftover = len(buf) - off
    print(f"Parsed {len(bones)} bones; {leftover} trailing bytes")
    print()
    return bones


# ─── BoneTransform / Mat4 helpers (mirror runtime math) ──────────────────────

def quat_mul(a: tuple[float, ...], b: tuple[float, ...]) -> tuple[float, float, float, float]:
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (
        aw * bx + ax * bw + ay * bz - az * by,
        aw * by - ax * bz + ay * bw + az * bx,
        aw * bz + ax * by - ay * bx + az * bw,
        aw * bw - ax * bx - ay * by - az * bz,
    )


def quat_normalize(q: tuple[float, float, float, float]) -> tuple[float, float, float, float]:
    n = math.sqrt(sum(x * x for x in q))
    if n < 1e-20:
        return (0.0, 0.0, 0.0, 1.0)
    return tuple(x / n for x in q)  # type: ignore[return-value]


def quat_rotate_vec(q: tuple[float, float, float, float], v: tuple[float, float, float]) -> tuple[float, float, float]:
    # v' = q * (v,0) * conj(q)
    qx, qy, qz, qw = q
    vx, vy, vz = v
    # t = 2 * cross(qxyz, v)
    tx = 2.0 * (qy * vz - qz * vy)
    ty = 2.0 * (qz * vx - qx * vz)
    tz = 2.0 * (qx * vy - qy * vx)
    # v' = v + qw * t + cross(qxyz, t)
    rx = vx + qw * tx + (qy * tz - qz * ty)
    ry = vy + qw * ty + (qz * tx - qx * tz)
    rz = vz + qw * tz + (qx * ty - qy * tx)
    return (rx, ry, rz)


def vec_mul(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] * b[0], a[1] * b[1], a[2] * b[2])


def vec_add(a: tuple[float, float, float], b: tuple[float, float, float]) -> tuple[float, float, float]:
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


@dataclass
class TRS:
    pos: tuple[float, float, float]
    rot: tuple[float, float, float, float]
    scl: tuple[float, float, float]


def compose_with(local: TRS, parent: TRS) -> TRS:
    """Mirror BoneTransform::ComposeWith (bone_transform.h:117)."""
    out_scale = vec_mul(parent.scl, local.scl)
    out_rot = quat_normalize(quat_mul(parent.rot, local.rot))
    rotated = quat_rotate_vec(parent.rot, vec_mul(parent.scl, local.pos))
    out_pos = vec_add(parent.pos, rotated)
    return TRS(out_pos, out_rot, out_scale)


# ─── Mat4 (row-major, DirectXMath layout) ────────────────────────────────────

def mat4_identity() -> list[list[float]]:
    return [[1 if i == j else 0 for j in range(4)] for i in range(4)]


def mat4_mul(a: list[list[float]], b: list[list[float]]) -> list[list[float]]:
    out = [[0.0] * 4 for _ in range(4)]
    for i in range(4):
        for j in range(4):
            s = 0.0
            for k in range(4):
                s += a[i][k] * b[k][j]
            out[i][j] = s
    return out


def mat4_translation(p: tuple[float, float, float]) -> list[list[float]]:
    m = mat4_identity()
    m[3][0], m[3][1], m[3][2] = p
    return m


def mat4_scaling(s: tuple[float, float, float]) -> list[list[float]]:
    m = mat4_identity()
    m[0][0], m[1][1], m[2][2] = s
    return m


def mat4_from_quat(q: tuple[float, float, float, float]) -> list[list[float]]:
    """Row-major rotation matrix from quaternion (DirectXMath convention)."""
    x, y, z, w = q
    xx, yy, zz = x * x, y * y, z * z
    xy, xz, yz = x * y, x * z, y * z
    wx, wy, wz = w * x, w * y, w * z
    m = mat4_identity()
    # Row-major: row vectors transformed as v * M.
    m[0][0] = 1 - 2 * (yy + zz)
    m[0][1] = 2 * (xy + wz)
    m[0][2] = 2 * (xz - wy)
    m[1][0] = 2 * (xy - wz)
    m[1][1] = 1 - 2 * (xx + zz)
    m[1][2] = 2 * (yz + wx)
    m[2][0] = 2 * (xz + wy)
    m[2][1] = 2 * (yz - wx)
    m[2][2] = 1 - 2 * (xx + yy)
    return m


def mat4_TRS(pos, rot, scl) -> list[list[float]]:
    """Match Mat4::TRS = Scaling * Rotation * Translation (row-major)."""
    s = mat4_scaling(scl)
    r = mat4_from_quat(rot)
    t = mat4_translation(pos)
    return mat4_mul(mat4_mul(s, r), t)


def mat4_inverse(m: list[list[float]]) -> list[list[float]]:
    """General 4x4 inverse via Gauss-Jordan on augmented matrix."""
    n = 4
    a = [row[:] + [1.0 if i == j else 0.0 for j in range(n)] for i, row in enumerate(m)]
    for col in range(n):
        # Pivot
        pivot = col
        for r in range(col + 1, n):
            if abs(a[r][col]) > abs(a[pivot][col]):
                pivot = r
        if abs(a[pivot][col]) < 1e-20:
            raise ValueError("singular matrix")
        a[col], a[pivot] = a[pivot], a[col]
        d = a[col][col]
        for j in range(2 * n):
            a[col][j] /= d
        for r in range(n):
            if r == col:
                continue
            f = a[r][col]
            for j in range(2 * n):
                a[r][j] -= f * a[col][j]
    return [row[n:] for row in a]


def mat4_format(m: list[list[float]]) -> str:
    rows = []
    for row in m:
        rows.append("  [" + ", ".join(f"{v:+.6f}" for v in row) + "]")
    return "\n".join(rows)


def mat4_max_abs_diff(a: list[list[float]], b: list[list[float]]) -> float:
    return max(abs(a[i][j] - b[i][j]) for i in range(4) for j in range(4))


# ─── Main ─────────────────────────────────────────────────────────────────────

def main(path: str) -> None:
    with open(path, "rb") as f:
        buf = f.read()
    print(f"File: {path}")
    print(f"Size: {len(buf)} bytes")
    print()

    bones = parse(buf)

    # Topological order check + scale flagging.
    print("=" * 100)
    print("Bone table")
    print("=" * 100)
    print(f"{'idx':>3}  {'parent':>6}  {'name':<28}  {'pos (x,y,z)':<36}  {'rot (x,y,z,w)':<48}  {'scl':<24}  flags")
    flags_summary = []
    for b in bones:
        topo_ok = b.parent < b.index
        qmag = math.sqrt(sum(x * x for x in b.rot))
        sx, sy, sz = b.scl
        nonuniform = abs(sx - sy) > 1e-3 or abs(sy - sz) > 1e-3 or abs(sx - sz) > 1e-3
        nonunit_uniform = (
            not nonuniform
            and (abs(sx - 1.0) > 1e-3 or abs(sy - 1.0) > 1e-3 or abs(sz - 1.0) > 1e-3)
        )
        negative = sx < 0 or sy < 0 or sz < 0
        nan_inf = any(
            (math.isnan(v) or math.isinf(v))
            for v in (*b.pos, *b.rot, *b.scl)
        )
        flag_bits = []
        if not topo_ok:
            flag_bits.append("BAD_TOPO")
        if abs(qmag - 1.0) > 1e-3:
            flag_bits.append(f"|q|={qmag:.4f}")
        if nonuniform:
            flag_bits.append("NONUNIFORM_SCALE")
        elif nonunit_uniform:
            flag_bits.append(f"UNIFORM_SCALE={sx:.4f}")
        if negative:
            flag_bits.append("NEGATIVE_SCALE")
        if nan_inf:
            flag_bits.append("NAN_OR_INF")

        if flag_bits:
            flags_summary.append((b.index, b.name, flag_bits))

        print(
            f"{b.index:>3}  {b.parent:>6}  {b.name:<28}  "
            f"({b.pos[0]:+8.3f},{b.pos[1]:+8.3f},{b.pos[2]:+8.3f})  "
            f"({b.rot[0]:+.4f},{b.rot[1]:+.4f},{b.rot[2]:+.4f},{b.rot[3]:+.4f})  "
            f"({b.scl[0]:+.4f},{b.scl[1]:+.4f},{b.scl[2]:+.4f})  "
            f"{','.join(flag_bits)}"
        )
    print()

    # Walk hierarchy → world bind TRS.
    print("=" * 100)
    print("World-space bind pose (computed via BoneTransform::ComposeWith)")
    print("=" * 100)
    world: list[TRS] = [TRS((0, 0, 0), (0, 0, 0, 1), (1, 1, 1)) for _ in bones]
    for b in bones:
        local = TRS(b.pos, b.rot, b.scl)
        if b.parent < 0:
            world[b.index] = local
        else:
            world[b.index] = compose_with(local, world[b.parent])
        wp = world[b.index].pos
        ws = world[b.index].scl
        print(
            f"  [{b.index:>2}] {b.name:<28}  worldPos=({wp[0]:+9.3f},{wp[1]:+9.3f},{wp[2]:+9.3f})  "
            f"worldScl=({ws[0]:+.4f},{ws[1]:+.4f},{ws[2]:+.4f})"
        )
    print()

    # Geometry sanity: lowest/highest world Y and Z to gauge "above floor".
    pelvis = world[0].pos
    print(f"Root world position (treated as 'pelvis'): {pelvis}")
    min_z = min(w.pos[2] for w in world)
    max_z = max(w.pos[2] for w in world)
    min_y = min(w.pos[1] for w in world)
    max_y = max(w.pos[1] for w in world)
    print(f"World Y range: [{min_y:.3f}, {max_y:.3f}]   World Z range: [{min_z:.3f}, {max_z:.3f}]")
    print()

    # Cross-check: inverseBindWorld[5] vs Mat4::TRS(world[5]).inverse().
    # The on-disk file does NOT store inverseBindWorld — it is recomputed in
    # ReadSkeleton via ComputeInverseBindMatrices. So we just exhibit what the
    # runtime would produce for bone 5 and verify the inverse identity.
    print("=" * 100)
    print("Cross-check — bone 5 inverseBindWorld = inverse(Mat4::TRS(worldBind[5]))")
    print("=" * 100)
    if len(bones) > 5:
        b5 = bones[5]
        w5 = world[5]
        print(f"Bone 5: name='{b5.name}' parent={b5.parent}")
        print(f"  world pos = {w5.pos}")
        print(f"  world rot = {w5.rot}")
        print(f"  world scl = {w5.scl}")
        m = mat4_TRS(w5.pos, w5.rot, w5.scl)
        print()
        print("Mat4::TRS(world[5]):")
        print(mat4_format(m))
        try:
            inv = mat4_inverse(m)
            print()
            print("inverse(Mat4::TRS(world[5])) (this is what runtime stores in inverseBindWorld[5]):")
            print(mat4_format(inv))
            ident = mat4_mul(m, inv)
            err = mat4_max_abs_diff(ident, mat4_identity())
            print()
            print(f"Verify: max|M*M^-1 - I| = {err:.3e}  ({'OK' if err < 1e-4 else 'BAD'})")
        except ValueError as e:
            print(f"  inverse failed: {e}")
    else:
        print("(skipped — fewer than 6 bones)")
    print()

    # Summary.
    print("=" * 100)
    print("Flag summary")
    print("=" * 100)
    if not flags_summary:
        print("  (no anomalies flagged)")
    else:
        for idx, name, flags in flags_summary:
            print(f"  [{idx:>2}] {name:<28}  {', '.join(flags)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("usage: python debug_inspect_skel.py <path.uzskel>", file=sys.stderr)
        sys.exit(2)
    main(sys.argv[1])
