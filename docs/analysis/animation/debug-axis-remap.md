# Debug: Cyclic axis remap on `DecomposeUfbxMatrix` — verification

## Verdict

**INCONCLUSIVE / MISDIAGNOSED.** The cyclic axis remap added yesterday in
`runtime/engine/asset/importers/fbx_importer.cpp:240-268` is mathematically
*self-consistent* with the vertex remap. The remap is **not** the cause of the
animation explosion, and removing it would re-break the bind pose.

The actual root cause is something else entirely:
**the per-clip `*.FBX` files use a different axis convention than the
skinned-mesh `MushroomMonster_Low.FBX`**, and the importer never reconciles
the two when extracting an animation channel. The same per-clip FBX is also
extracted with its OWN private skeleton (separate from the mesh's skeleton),
so the local TRS values written into `*.uzanim` live in a coordinate frame
that does not match the bind-pose local TRS in `MushroomMonster_Low.uzskel`.
At runtime the per-bone `localTRS` is fed back through `ComposeWith` (which
multiplies parent-world by local), and the mismatched local frames blow up
into garbage world matrices — i.e. the "exploded mesh".

The cyclic remap is therefore *correct in isolation* (it preserves the
TRS-decomposition→permutation chain end-to-end for any single FBX file), but
it **does not** convert one file's frame into another's, which is what is
actually needed here.

## Evidence

### 1. The mesh FBX is genuinely Y-up (the comment at `fbx_importer.cpp:1213` is correct)

Bytes read directly from `MushroomMonster_Low.FBX` `Properties70`:

| Property        | Value | Meaning                |
|-----------------|-------|------------------------|
| `UpAxis`        | `1`   | +Y is up               |
| `UpAxisSign`    | `1`   |                        |
| `FrontAxis`     | `2`   | +Z is "front" (out of screen) → forward = -Z |
| `FrontAxisSign` | `1`   |                        |
| `CoordAxis`     | `0`   | +X is right            |
| `CoordAxisSign` | `1`   |                        |
| `OriginalUpAxis`| `-1`  | (not specified)        |
| FBX version     | 7400  | (binary, Maya export)  |

This is the standard **Maya / Y-up right-handed** convention.

### 2. ufbx loads with `UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY` but **no `target_axes`**

`fbx_importer.cpp:972-978`:

```cpp
// Do NOT set target_axes — the FBX file already uses Z-up vertex positions.
// Any axis conversion via target_axes remaps the axes and breaks orientation.
ufbx_load_opts loadOpts = {};
loadOpts.target_unit_meters = 0.01f;
loadOpts.space_conversion = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
```

The comment is misleading: per `runtime/thirdparty/ufbx/ufbx.h:3559-3582`,
`UFBX_SPACE_CONVERSION_*` only takes effect *if* `target_axes` *or*
`target_unit_meters` is set. Since `target_axes` is *not* set, ufbx applies
**no axis conversion at all** — it only scales geometry by
`target_unit_meters`. So both `vertex_position[i]` and
`cluster->bind_to_world` are still in **FBX-native Y-up RH centimetres**.

The vertex extractor (`fbx_importer.cpp:1216-1219`) compensates with a
manual cyclic permutation `(x,y,z) → (z,x,y)`, which maps:
- FBX.x (right)    → UE.y  (Z-up's lateral/depth axis)
- FBX.y (up)       → UE.z  (Z-up's vertical axis)
- FBX.z (front=-forward) → UE.x  (Z-up's forward axis)

This permutation has `det = +1` (it is a proper rotation by 120° around
`(1,1,1)/√3`). It does **not** change handedness. Handedness conversion
RH→LH is delivered separately by the explicit triangle-winding swap at
`fbx_importer.cpp:1265-1267`. Both pieces are required and both are correct
for this single file.

### 3. The cyclic permutation of quaternion components is mathematically correct

I verified numerically that for the cyclic permutation matrix
`P = [[0,0,1],[1,0,0],[0,1,0]]` (which sends `(x,y,z) → (z,x,y)`):

- For a translation vector `t`: `P · t = (t.z, t.x, t.y)` — matches the code.
- For a quaternion `q = (qx, qy, qz, qw)`: the proper change-of-basis is
  `q' = R_quat · q · R_quat⁻¹` where `R_quat` is the quaternion form of `P`
  (a 120° rotation around `(1,1,1)/√3`). Computing this explicitly gives
  `q' = (qz, qx, qy, qw)` — **identical to the cyclic permutation of the
  imaginary part**.
- For a diagonal scale `s`: `P · diag(s) · Pᵀ = diag((sz, sx, sy))` — also a
  cyclic permutation, again matching the code.

So the line-240 implementation:

```cpp
bt.position = Vec3{ (float)t.translation.z, (float)t.translation.x, (float)t.translation.y };
bt.rotation = Quat { (float)t.rotation.z,    (float)t.rotation.x,    (float)t.rotation.y, (float)t.rotation.w };
bt.scale    = Vec3{ (float)t.scale.z,        (float)t.scale.x,       (float)t.scale.y };
```

is the correct change-of-basis for ANY single FBX matrix from Y-up RH to
the cyclic-permuted UE frame. **No sign flip is needed** because the
cyclic permutation is a proper rotation (det = +1), and handedness is
already corrected downstream by the winding swap at the vertex level.

### 4. Bind-pose world bone positions in `MushroomMonster_Low.uzskel` are correctly Z-up

I parsed `MushroomMonster_Low.uzskel` (21 bones), composed each bone's
world-space bind transform via the same `BoneTransform::ComposeWith` math
that `Skeleton::ComputeInverseBindMatrices` uses, and printed the world
positions:

| Axis | min     | max     | range  |
|------|---------|---------|--------|
| X    | -43.16  |  33.88  |  77.0  |
| Y    | -11.71  |  36.16  |  47.9  |
| **Z** | **20.55** | **169.85** | **149.3** |

The Z range (~150 cm) dwarfs the X and Y ranges, and the highest bones are
the cap (`Point010`, `Bone003` at z ≈ 170) while the lowest are the leg
roots (`Bone009`, `Bone006` at z ≈ 20–30). **Z is unambiguously "up".** The
cyclic remap correctly mapped FBX-Y onto UE-Z. The bind pose is in the
right frame.

### 5. The animation FBX files use a *different* axis convention

| File                          | UpAxis | FrontAxis | FrontAxisSign | UpAxis meaning  | FBX version |
|-------------------------------|--------|-----------|---------------|-----------------|-------------|
| `MushroomMonster_Low.FBX`     | 1      | 2         | +1            | **Y up (RH)**   | 7400 |
| `Mushroom_Idle.FBX`           | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack01.FBX`       | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack02.FBX`       | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack03.FBX`       | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack04End.FBX`    | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack04_Loop.FBX`  | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Attack04_Start.FBX` | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Block01.FBX`        | 2      | 1         | -1            | **Z up**        | 7300 |
| `Mushroom_Block02.FBX`        | 2      | 1         | -1            | **Z up**        | 7300 |

(Spot-checked 8 additional clips — every one of them is Z-up.)

The skinned mesh was exported by Maya (Y-up). All the animation clips were
exported by 3ds Max (Z-up). The importer applies the cyclic
`(x,y,z)→(z,x,y)` remap unconditionally, so:

- For the Z-up clip files the cyclic remap is **applied to data that is
  already Z-up**, sending UE.x ← FBX.z (which was already vertical) and
  rotating the whole skeleton 120° around `(1,1,1)/√3`.
- The local TRS values written into the clip's `*.uzanim` are therefore in
  a *third* frame — neither FBX-Y-up nor UE-Z-up.

### 6. Direct numerical evidence: bind LOCAL TRS ≠ animation frame-0 LOCAL TRS for the same bone

Parsed `MushroomMonster_Low.uzskel` and `Mushroom_Idle.uzanim` and compared
`MushroomPelvis` (the skeleton root):

| Source                       | Position                  | Rotation (axis-angle)         |
|------------------------------|---------------------------|-------------------------------|
| Skeleton bind LOCAL          | `(0.00, 0.09, 78.38)`     | 90.58° around `(1,0,0)`       |
| Animation `Idle` frame 0     | `(2.70, 0.50,  7.18)`     | ~14° around `(0.99,0.06,−0.23)` |

For an idle animation, frame 0 should equal the rest pose, so the
animation's local TRS for `MushroomPelvis` should be approximately equal
to the skeleton's bind local TRS. They are not even close — the position
magnitude differs by an order of magnitude (78.38 vs 7.18) and the
rotation axis is different. This is what an axis-mismatch between the two
files looks like.

### 7. The animation extractor builds a private skeleton from the *clip* file, not the mesh's skeleton

`fbx_importer.cpp:489-587` (`ExtractSkeletonFromScene`) is invoked by the
animation-only import path. It walks `scene->nodes`, picks bones that are
animated, and produces a brand-new `Skeleton` whose bind-LOCAL TRS values
are derived from the clip file's `node_to_world` (decomposed via the same
`DecomposeUfbxMatrix` with the cyclic remap). It then runs
`ExtractAnimations` on that skeleton.

Because the clip file is Z-up and the cyclic remap is applied anyway, the
clip's `node_to_world` matrices are decomposed in the wrong frame, and
both the clip's bind pose AND its animation curves end up consistently
"wrong-but-self-consistent" *within the clip*. The mismatch only becomes
visible when the clip is played against the mesh's skeleton at runtime,
because `AnimationClip::SamplePose` looks up channels by bone name
(`animation_clip.cpp:115`) and overwrites the bind LOCAL TRS with the
clip's LOCAL TRS — which is in a different frame.

## Why the bind pose still looks correct after yesterday's change

Yesterday's change applied the cyclic remap to `DecomposeUfbxMatrix`, which
is called for **both** bind extraction (`cluster->bind_to_world`) and animation
extraction (`evNode->node_to_world`). Within a single FBX file, both calls
operate on matrices in the same frame, so the remap remaps both consistently
and `palette = inverseBind · world = identity` at the bind frame. The
inverse-bind cancels the remap because the cancellation is purely algebraic.

Bind pose looks correct because the *mesh* file is consumed end-to-end by
itself: the mesh's vertex remap, the mesh's bind decomposition, and the
mesh's `inverseBind` are all evaluated in the same (cyclic-permuted Y-up)
frame, and they cancel cleanly.

Animation explodes because the *clip* file is decomposed in a frame that
is inconsistent with the mesh's bind. The `inverseBind` from the mesh is
in the cyclic-permuted Y-up frame; the clip's local TRS is in the
cyclic-permuted Z-up frame. Multiplying them does not cancel.

## What the correct fix looks like

The cleanest fix is to let ufbx do the conversion that the importer is
currently trying to do by hand. Set `loadOpts.target_axes` and remove the
manual permutations:

```cpp
ufbx_load_opts loadOpts = {};
loadOpts.target_unit_meters = 0.01f;                       // centimetres
loadOpts.target_axes        = ufbx_axes_left_handed_z_up;  // UzEngine convention
loadOpts.space_conversion   = UFBX_SPACE_CONVERSION_MODIFY_GEOMETRY;
loadOpts.handedness_conversion_axis = UFBX_MIRROR_AXIS_X;  // or whichever axis matches
loadOpts.reverse_winding    = true;                        // CW → CCW for our rasterizer
```

(`ufbx_axes_left_handed_z_up` is declared in `runtime/thirdparty/ufbx/ufbx.h:5087`.)

With this change:

1. ufbx normalises both Y-up and Z-up files to UE's left-handed Z-up frame
   on load. The mesh and every clip end up in the *same* frame regardless
   of how they were exported.
2. `vertex_position` is already in UE space — drop the cyclic remap at
   `fbx_importer.cpp:1216-1226`, just write `pos.x/y/z` straight through.
3. `vertex_normal` likewise — drop the remap at `fbx_importer.cpp:1222-1226`.
4. `bind_to_world` and `node_to_world` are already in UE space — `DecomposeUfbxMatrix`
   should drop the remap and use `t.translation.x/y/z`, `t.rotation.x/y/z/w`,
   `t.scale.x/y/z` directly.
5. The triangle winding flip at `fbx_importer.cpp:1265-1267` is replaced by
   `loadOpts.reverse_winding = true` (or kept as-is — see ufbx docs at
   `runtime/thirdparty/ufbx/ufbx.h:4660-4666`).

The cyclic remap was a workaround that happened to produce a correct
single-file result; once ufbx is allowed to normalise axes itself, no
hand-rolled remap is needed and cross-file mismatches disappear.

A smaller "tactical" fix that keeps the manual remap but unblocks
animation playback would be: detect the per-file `scene->settings.axes`
(or the raw `Properties70` UpAxis/FrontAxis/CoordAxis values) inside
`Import()` and either skip the remap for already-Z-up files or apply a
file-specific permutation. This is more brittle and will not generalise
to LH-vs-RH mismatches.

## Files referenced

- `runtime/engine/asset/importers/fbx_importer.cpp:240` — `DecomposeUfbxMatrix` (yesterday's edit site)
- `runtime/engine/asset/importers/fbx_importer.cpp:972-978` — ufbx load opts (no `target_axes`)
- `runtime/engine/asset/importers/fbx_importer.cpp:1213-1226` — vertex/normal cyclic remap
- `runtime/engine/asset/importers/fbx_importer.cpp:1262-1267` — triangle winding swap
- `runtime/engine/asset/importers/fbx_importer.cpp:489-587` — `ExtractSkeletonFromScene` (clip-only path)
- `runtime/engine/asset/importers/fbx_importer.cpp:604-771` — `ExtractAnimations`
- `runtime/engine/animation/animation_clip.cpp:87-130` — `SamplePose` (channel-by-name lookup)
- `runtime/engine/animation/skeleton.cpp:67-112` — `ComputeInverseBindMatrices`
- `runtime/engine/animation/skeletal_mesh_instance.cpp:64-114` — `UpdatePaletteFromPose`
- `runtime/thirdparty/ufbx/ufbx.h:3559-3582` — `ufbx_space_conversion` semantics
- `runtime/thirdparty/ufbx/ufbx.h:4668-4684` — `target_axes` / `target_camera_axes` / `target_light_axes`
- `runtime/thirdparty/ufbx/ufbx.h:5084-5087` — built-in `ufbx_axes_*` constants
- `runtime/thirdparty/ufbx/ufbx.h:1547-1553` — `ufbx_coordinate_axes` (note: `front` is *opposite* of forward)
- `sample/content/meshes/mushroommonster/MushroomMonster_Low.FBX` — Y-up RH, FBX 7400
- `sample/content/meshes/mushroommonster/animations/Mushroom_*.FBX` — Z-up, FBX 7300
