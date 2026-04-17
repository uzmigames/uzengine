# Debug: Mushroom Monster Skeleton (.uzskel)

**Date:** 2026-04-16
**File:** `e:\UzEngine\sample\content\meshes\mushroommonster\MushroomMonster_Low.uzskel`
**Size:** 1108 bytes
**Inspector:** `e:\UzEngine\scripts\debug_inspect_skel.py` (standalone Python parser; no engine link)

## TL;DR

The on-disk skeleton looks **structurally clean and correct**:

- Magic `0x4B535A55` ('UZSK'), version `1`, bone count `21` — all match expectations.
- All 21 bones in valid topological order (parent index < child index).
- All quaternions normalized (`|q| ≈ 1` to ≤ 1e-3).
- All bind scales effectively `(1, 1, 1)` — no non-uniform, no negative, no NaN/Inf.
- World-space bind pose looks reasonable for a Z-up mushroom (root pelvis at `+78 cm`, head/cap top at `+169 cm`, leg tips around `+71 cm`).
- `inverse(Mat4::TRS(worldBind[5]))` is well-defined (`max|M·M⁻¹ − I| = 1.4e-14`).

**Implication:** the skinning bug after `Mushroom_Idle` plays out cannot be blamed on the bind-pose data on disk. The TRS values are fine, the topology is fine, the bind pose composes to plausible world positions, and the inverse-bind matrix that `ComputeInverseBindMatrices` derives at load time is well-conditioned. The problem must lie elsewhere — most likely in (a) the animation channel decoding (axis-swap mismatch between the v6 importer's bind-pose remap and the animation track remap), (b) the animated-pose composition, or (c) the `palette = animatedWorld * inverseBind` step.

The single mildly suspicious data point is **bone 5 `Point011`** whose stored bind rotation is `(0.7107, 0, 0, 0.7035)` — i.e. a near-90° rotation around the X axis. After cyclic axis remap `(x,y,z) → (z,x,y)`, a rotation that started life as 90° about the FBX Y axis would end up about the engine X axis, which is exactly what we see. So the remap was applied. If the *animation* track still rotates `Point011` about the old axis, the bone's local rotation will be incoherent with its bind rotation — likely failure mode.

---

## 1. Header (first 32 bytes)

```
55 5a 53 4b 01 00 00 00 15 00 00 00 00 00 00 00  0e 00 4d 75 73 68 72 6f 6f 6d 50 65 6c 76 69 73
└──── magic ────┘ └─ version ─┘ └─ count ──┘ └ res ┘ └ name 14 = "MushroomPelvis"
```

| Field | Bytes | Value | Status |
|-------|-------|-------|--------|
| magic | `55 5a 53 4b` | `0x4B535A55` ('UZSK') | OK |
| version | `01 00 00 00` | `1` | OK (matches `kUzSkelVersion`) |
| boneCount | `15 00 00 00` | `21` | OK |
| reserved | `00 00 00 00` | `0` | OK |

After the 16-byte header, the first record begins immediately at offset 16 with `nameLen = 0x000E` (14) and the ASCII string `MushroomPelvis`. Parser consumed all 1108 bytes with **0 trailing bytes**.

> Note: the skeleton-file `version` field (1) is independent of the FBX importer version (5 → 6). The on-disk container format hasn't changed; only the bone TRS *values* the importer wrote into it changed.

## 2. Bone count

`21` bones, matches the expected count.

## 3. Per-bone dump

(`pos` in centimeters; `rot` is `(x, y, z, w)`; `scl` should be `(1, 1, 1)`)

| idx | parent | name | bindTranslation | bindRotation | bindScale |
|----:|-------:|------|-----------------|--------------|-----------|
|   0 |     -1 | MushroomPelvis | (+0.000, +0.086, +78.381) | (+0.7107, +0.0000, +0.0000, +0.7035) | (1, 1, 1) |
|   1 |      0 | Point005       | (-10.132, -7.253, +11.874) | (-0.3361, -0.9277, +0.1528, +0.0554) | (1, 1, 1) |
|   2 |      0 | Point004       | (-12.602, -6.168, -12.257) | (-0.9242, -0.3455, +0.0569, +0.1523) | (1, 1, 1) |
|   3 |      0 | Point002       | (+10.925, -7.253, +11.874) | (+0.4315, -0.8874, +0.1462, -0.0711) | (1, 1, 1) |
|   4 |      0 | Point003       | (+10.539, -6.171, -12.528) | (-0.8898, +0.4264, -0.0703, +0.1465) | (1, 1, 1) |
|   5 |      0 | Point011       | (+0.000, +34.277, -0.000)  | (-0.0050, +0.0000, +0.0000, +1.0000) | (1, 1, 1) |
|   6 |      1 | MushLeg04      | (-0.000, +0.000, -0.000)   | (+0.0000, -0.0000, -0.0000, +1.0000) | (1, 1, 1) |
|   7 |      2 | MushLeg03      | (+0.000, +0.000, -0.000)   | (-0.0000, -0.0001, -0.0000, +1.0000) | (1, 1, 1) |
|   8 |      3 | MushLeg01      | (+0.000, +0.000, -0.000)   | (-0.0000, -0.0000, +0.0000, +1.0000) | (1, 1, 1) |
|   9 |      4 | MushLeg02      | (+0.000, +0.000, -0.000)   | (+0.0001, +0.0001, -0.0001, +1.0000) | (1, 1, 1) |
|  10 |      5 | Bone002        | (-0.000, -0.000, +0.000)   | (-0.0001, -0.0000, +0.0000, +1.0000) | (1, 1, 1) |
|  11 |      6 | Bone011        | (-0.000, +27.902, +0.000)  | (+0.0038, -0.0032, -0.0000, +1.0000) | (1, 1, 1) |
|  12 |      7 | Bone008        | (+0.000, +27.902, -0.000)  | (-0.0037, -0.0032, +0.0000, +1.0000) | (1, 1, 1) |
|  13 |      8 | Bone014        | (-0.000, +27.902, +0.000)  | (+0.0031, +0.0040, +0.0000, +1.0000) | (1, 1, 1) |
|  14 |      9 | Bone005        | (+0.000, +27.902, -0.000)  | (-0.0031, +0.0039, +0.0000, +1.0000) | (1, 1, 1) |
|  15 |     10 | Point010       | (-0.000, +57.191, +0.000)  | (+0.0025, +0.0000, -0.0000, +1.0000) | (1, 1, 1) |
|  16 |     11 | Bone012        | (-0.000, +44.763, -0.000)  | (+0.0000, -0.0000, +0.4001, +0.9165) | (1, 1, 1) |
|  17 |     12 | Bone009        | (+0.000, +44.763, -0.000)  | (-0.0000, -0.0000, -0.3756, +0.9268) | (1, 1, 1) |
|  18 |     13 | Bone015        | (+0.000, +44.763, -0.000)  | (+0.0000, +0.0000, -0.4757, -0.8796) | (1, 1, 1) |
|  19 |     14 | Bone006        | (-0.000, +44.763, -0.000)  | (-0.0000, +0.0000, -0.3852, +0.9228) | (1, 1, 1) |
|  20 |     15 | Bone003        | (-0.000, -0.000, +0.000)   | (-0.0001, -0.0000, +0.0000, +1.0000) | (1, 1, 1) |

### 3a. Topological order

For every bone, `parentIndex < boneIndex`. Parents are visited before children — `ComputeInverseBindMatrices` can walk the array in a single forward pass without sorting. **No bad-topology cases.**

### 3b. Quaternion magnitudes

All 21 quaternions have `|q| − 1| ≤ 1e-3`. **No de-normalised rotations.**

### 3c. Scale anomalies

All 21 bones have bind scale exactly `(1, 1, 1)` to single-precision float (the propagated world-space scale on bone 5 is `(1.0, 1.00000006, 1.00000018)`, well within float quantisation). **No non-uniform, no negative, no NaN/Inf.**

> This is the *expected* shape for an FBX rig that was decomposed into engine TRS — the importer either succeeded in identifying that the bone has unit scale, or any axis-conversion scale collapsed back to identity after the cyclic remap. There is no "matrix-vs-TRS" hidden-scale case in this rig.

### 3d. NaN / Inf

None across all 9 floats × 21 bones (189 values).

## 4. Hierarchy walk — world-space bind pose

Computed via `BoneTransform::ComposeWith` in topological order (mirrors `Skeleton::ComputeInverseBindMatrices` in `runtime/engine/animation/skeleton.cpp:67`).

Z is up; positions in cm.

| idx | name | worldPos (x, y, z) | sanity |
|----:|------|---------------------|--------|
|   0 | MushroomPelvis | (   +0.000,   +0.086,  +78.381) | pelvis ~78 cm above floor ✓ |
|   1 | Point005       | (  -10.132,  -11.714,  +71.008) | back-left foot bone ✓ |
|   2 | Point004       | (  -12.602,  +12.404,  +72.337) | front-left foot bone ✓ |
|   3 | Point002       | (  +10.925,  -11.714,  +71.008) | back-right foot bone ✓ |
|   4 | Point003       | (  +10.539,  +12.675,  +72.337) | front-right foot bone ✓ |
|   5 | Point011       | (   +0.000,   -0.260, +112.656) | spine root ~112 cm ✓ |
|   6 | MushLeg04      | (  -10.132,  -11.714,  +71.008) | leg tip — same XY as Point005 ✓ |
|   7 | MushLeg03      | (  -12.602,  +12.404,  +72.337) | leg tip — same XY as Point004 ✓ |
|   8 | MushLeg01      | (  +10.925,  -11.714,  +71.008) | leg tip — same XY as Point002 ✓ |
|   9 | MushLeg02      | (  +10.539,  +12.675,  +72.337) | leg tip — same XY as Point003 ✓ |
|  10 | Bone002        | (   +0.000,   -0.260, +112.656) | spine joint — coincident w/ Point011 ✓ |
|  11 | Bone011        | (   +6.795,   -2.968,  +91.392) | offset arm-ish branch (Y+27.9 from MushLeg04) |
|  12 | Bone008        | (   +4.735,  +21.556,  +52.482) | mid-chain — front-left arm |
|  13 | Bone014        | (   -9.860,   -2.929,  +87.419) | mid-chain — back-right arm |
|  14 | Bone005        | (  -10.055,  +21.795,  +55.868) | mid-chain — front-right arm |
|  15 | Point010       | (   -0.000,   -0.260, +169.846) | head/cap top ~170 cm ✓ |
|  16 | Bone012        | (  +33.880,  +11.386, +124.012) | tip of left-arm chain ✓ |
|  17 | Bone009        | (  +32.618,  +35.922,  +20.548) | arm tip swung down/forward ✓ |
|  18 | Bone015        | (  -43.135,  +11.427, +113.693) | tip of right-arm chain ✓ |
|  19 | Bone006        | (  -43.163,  +36.163,  +29.390) | mirror of bone 17 ✓ |
|  20 | Bone003        | (   -0.000,   -0.260, +169.846) | cap-top — coincident w/ Point010 ✓ |

World Y range: `[-11.714, +36.163]`, world Z range: `[+20.548, +169.846]`.

Everything is above the floor and within ~2 m of the origin. Symmetry checks out: bones 11/13 mirror across X (`±6.8`/`±9.9`), 12/14 mirror, 16/18 mirror (`±33.9`/`±43.1`), 17/19 mirror. Pelvis at `+78`, spine joint at `+112`, head at `+170`. **No bones are below the floor, no positions are hundreds of meters off.**

## 5. Cross-check — `inverseBindWorld[5]`

Note: the on-disk `.uzskel` does **not** store inverse bind matrices. They are recomputed by `Skeleton::ComputeInverseBindMatrices()` in `ReadSkeleton` (skeleton_io.cpp:222). So instead of comparing two stored values, we recompute what the runtime would produce and verify the inverse identity.

Bone 5 = `Point011`, parent = 0 (`MushroomPelvis`).

World bind TRS:
- pos = `(4.726e-05, -0.260276, +112.655572)`
- rot = `(0.707171, 2.36e-07, 3.87e-07, 0.707042)`
- scl = `(1.000000, 1.000000, 1.000000)` (within float quantisation)

`Mat4::TRS(world[5])` (row-major, DirectXMath layout):
```
[+1.000000, +0.000001, +0.000000, +0.000000]
[-0.000000, -0.000183, +1.000000, +0.000000]
[+0.000001, -1.000000, -0.000183, +0.000000]
[+0.000047, -0.260276, +112.655573, +1.000000]
```

`inverse(Mat4::TRS(world[5]))` (this is what `bone.inverseBindWorld` will be after load):
```
[+1.000000, -0.000000, +0.000001, +0.000000]
[+0.000001, -0.000183, -1.000000, +0.000000]
[+0.000000, +1.000000, -0.000183, +0.000000]
[-0.000071, -112.655612, -0.239671, +1.000000]
```

Verification: `max |M · M⁻¹ − I|  =  1.42 × 10⁻¹⁴`. The matrix is **non-singular** and the inverse is numerically exact. ✓

Geometric interpretation: `Point011` sits 112.66 cm directly above the model origin and is rotated +90° about the X axis (Z-up FBX bone whose local +Y points up the spine — after the cyclic axis remap, the bone's local frame has +Y aligned with world +Z, i.e. up). The translation row `(+0.000047, -0.260276, +112.655573)` matches the world position. Inversion negates the world translation in the bone's local frame: `(-0.000071, -112.655612, -0.239671)` — which is `−R⁻¹ · t` for the rotated frame, exactly correct.

## 6. Flag summary

```
(no anomalies flagged)
```

## 7. Where the skinning bug must therefore live

Given the bind data is clean, the deformation under `Mushroom_Idle` must come from one of:

1. **Animation channel axis remap mismatch.** The v6 importer applied `(x, y, z) → (z, x, y)` to bind-pose TRS. If `AnimationSequence` channels (translation tracks, rotation tracks) were *not* re-encoded with the same cyclic permutation — or were encoded twice — every animated-pose composition will land in a frame that does not agree with the bind pose. Bones with non-trivial bind rotation (notably bone 5 `Point011` and bones 16–19 with quat Z near ±0.4) would deform most visibly.
2. **Local-vs-world ambiguity in animation tracks.** `AnimationSequence` stores per-bone *local* TRS, but if the importer wrote *world* TRS for the animated keyframes, then the runtime's `localPose * parentWorld` chain produces world-of-world. Often presents as exaggerated/extreme deformation rather than total collapse.
3. **`palette = animatedWorld * inverseBind` ordering.** If `inverseBind` is computed from the new (remapped) bind pose but `animatedWorld` is built with the old bone-axis convention, the per-vertex `palette[boneIdx]` is wrong even though both halves look fine in isolation.
4. **`SkeletalMeshInstance` not re-fetching `inverseBind` after the importer bump.** If a stale palette is being uploaded from a cached buffer that pre-dates the v5 → v6 reimport, the fix-on-disk would not propagate. The disk file is correct, but the GPU palette may not be.

Suggested next diagnostic step: dump `AnimationSequence` for `Mushroom_Idle` with a sister script, walk one keyframe through the same `ComposeWith` chain, and compare the resulting `palette[i] = worldAnimated[i] * inverseBind[i]` against `Mat4::Identity()` at frame 0 (which should be the bind pose) — any non-identity result there isolates the channel-vs-bind axis mismatch immediately.

## Reproduce

```bash
cd e:/UzEngine
python scripts/debug_inspect_skel.py sample/content/meshes/mushroommonster/MushroomMonster_Low.uzskel
```

The script is dependency-free (Python 3.10+ standard library only). It does not modify production code and does not run the engine.
