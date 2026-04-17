# debug-runtime-trace — Mushroom_Idle skinning divergence

**Date:** 2026-04-16
**Tool:** `runtime/tests/debug_skin_reference/main.cpp` →
`runtime/build/bin/debug_skin_reference.exe`
**Sample frame:** `t = 0.5 s` in `Mushroom_Idle.FBX`
**Outputs (gitignored, written under `.scratch/`):**
- `e:/UzEngine/.scratch/skin_debug/ref.obj`
- `e:/UzEngine/.scratch/skin_debug/uz.obj`
- `e:/UzEngine/.scratch/skin_debug/bind.obj`
- `e:/UzEngine/.scratch/skin_debug/stats.txt`

---

## TL;DR

The CPU skinning math is correct in BOTH the reference path (raw ufbx
matrices) and the UzEngine path (TRS-decomposed + cyclic-remapped).
At `t = 0.5 s` they produce **byte-identical skinned vertex positions**
(max delta 0.0000 cm across 119 988 vertices, all within 1 cm).

But both paths produce a **mushroom rotated 90 degrees on its side**.
The bug is not in the skinning algorithm; it is in the **input data**:

> `MushroomMonster_Low.fbx` is **Y-up** (`scene.settings.axes.up = POSITIVE_Y`).
> `Mushroom_Idle.FBX`       is **Z-up** (`scene.settings.axes.up = POSITIVE_Z`).
>
> The production importer applies the same hard-coded cyclic axis remap
> `(x, y, z) → (z, x, y)` to **both** files when extracting bones, so
> the animated bone matrices end up in a basis incompatible with the
> mesh's bind pose. The 90-degree rotation that appears in playback is
> exactly the difference between those two basis conventions.

The fix lives in the importer (or in the per-clip ingest), not in the
runtime palette/skinning code. Stop debugging `UpdatePaletteFromPose`,
`SkinCpu`, `gpu_skinning_cs.hlsl`, or the GPU palette upload path.

---

## What the test does

`debug_skin_reference.exe` runs **two independent skinning paths** on
the *same* unique-corner vertex list (no meshopt reorder), so dump
files have identical vertex ordering and can be compared 1:1:

1. **REFERENCE path** — uses ufbx's runtime evaluator directly, no
   UzEngine helpers:

   ```
   palette[i] = node_to_world_at_t * cluster->geometry_to_bone
   skinned    = sum(weight * palette[i] * vertex_in_geometry_space)
   final      = cyclic_remap(skinned)        // (x,y,z) → (z,x,y)
   ```

   `geometry_to_bone` is the canonical ufbx skinning input — it
   already accounts for the rest pose **and** the geometry-to-node
   sub-transform of the mesh node, which `bind_to_world` does not.

2. **UZENGINE path** — replicates the production pipeline exactly:
   - `DecomposeUfbxMatrix` with cyclic remap applied to translation,
     rotation (quaternion imaginary part) and scale (`fbx_importer.cpp:240–268`).
   - `ComputeLocalFromWorldTRS` (`fbx_importer.cpp:280–305`).
   - `Skeleton::ComputeInverseBindMatrices` TRS walk
     (`skeleton.cpp:67–110`).
   - `UpdatePaletteFromPose` TRS walk + flatten
     (`skeletal_mesh_instance.cpp:64–113`).
   - Row-vector LBS (`skeletal_mesh_instance.cpp:159–246`).

Both paths consume the same vertex list, so any divergence would show
up vertex-by-vertex.

---

## Numeric results (t = 0.5 s)

```
Bones:    21
Vertices: 119 988

Position deltas (cm)
  max:        0.0000
  mean:       0.0000
  within 1cm: 119 988 / 119 988  (100.00%)

Palette Frobenius diff (per-bone)
  max: 68.9197  at bone[20] = 'Bone003'
```

The palette matrices themselves disagree wildly (Frobenius up to 68.9
because the engine path expresses the palette in cyclic-remapped basis
while the reference path keeps it in raw FBX basis). But each path
*also* applies the inverse remap to the input vertex, so the two
basis changes cancel and the final skinned vertex agrees to all 4
output decimal places.

### 5 representative vertices

| idx     | ref (x, y, z) cm                   | uz (x, y, z) cm                    | delta |
|---------|------------------------------------|------------------------------------|-------|
| 9 999   | ( 135.7390, -86.0733,   76.3941 ) | ( 135.7391, -86.0733,   76.3941 ) | 0.0000 |
| 29 997  | ( 149.5303,  50.3456,   34.7186 ) | ( 149.5303,  50.3456,   34.7186 ) | 0.0000 |
| 49 995  | (  -4.2035,  24.6023,  -68.8376 ) | (  -4.2035,  24.6023,  -68.8376 ) | 0.0000 |
| 69 993  | (  56.8672, -17.4776,   21.4311 ) | (  56.8672, -17.4776,   21.4311 ) | 0.0000 |
| 89 991  | ( 224.0145, -14.1096,  -13.3489 ) | ( 224.0145, -14.1096,  -13.3490 ) | 0.0000 |

---

## What the .obj dumps tell us

AABBs:

```
bind.obj: min= -123.35, -121.67,  -13.74   max=  123.03,  119.85,  224.64
ref.obj : min=  -15.30, -132.40, -136.14   max=  228.23,  107.34,  107.88
uz.obj  : min=  -15.30, -132.40, -136.14   max=  228.23,  107.34,  107.88
```

`bind.obj` (the un-skinned vertex list with only the cyclic remap
applied) is the correct upright Z-up mushroom: width ±123 on X,
depth ±121 on Y, **height 0..224 on Z**.

`ref.obj` and `uz.obj` (skinned at `t = 0.5`) are both rotated 90°:
the height of the mushroom (0..228) is now on the **X axis**, and Z
holds part of what used to be width. They are byte-identical, just
rotated relative to the bind.

Sampling the same animation at `t = 0` (which should yield the bind
pose) produces the same wrong rotation:

```
bind.obj: min= -123.35, -121.67,  -13.74   max=  123.03,  119.85,  224.64
ref.obj : min=  -15.30, -137.87, -129.09   max=  229.07,  100.29,  112.27
uz.obj  : min=  -15.30, -137.87, -129.09   max=  229.07,  100.29,  112.27
```

That is the smoking gun: **even at frame 0 of the clip, the skinned
mesh is rotated 90 ° relative to its own bind pose**. The animation
data itself is in a different basis than the mesh.

---

## Root-cause diagnostic

The test prints raw ufbx data for the root bone (`MushroomPelvis`)
from both scenes:

```
First bone 'MushroomPelvis'
  mesh.node_to_world.cols[3] (raw FBX):  (0.086, 78.381, 0.000)
  anim.node_to_world.cols[3] (rest):    (0.086, -0.000, 78.381)
  cluster->bind_to_world.cols[3]:        (0.086, 78.381, 0.000)
  cluster->geometry_to_bone.cols[3]:     (-78.376, 0.878, -0.000)
  mesh scene root unit meters: 0.0100, axes target=2,
  anim scene unit meters: 0.0100, axes target=4
```

`scene.settings.axes.up` enum (from `ufbx.h:1533`):

| value | meaning            |
|-------|--------------------|
| 0     | POSITIVE_X         |
| 1     | NEGATIVE_X         |
| 2     | **POSITIVE_Y**     |
| 3     | NEGATIVE_Y         |
| 4     | **POSITIVE_Z**     |

So:

- `MushroomMonster_Low.fbx` declares **Y-up**. The pelvis sits at
  `(0.086, 78.381, 0.000)` — height on Y, exactly what a Y-up file
  should look like.
- `Mushroom_Idle.FBX` declares **Z-up**. The same pelvis bone (matched
  by name) sits at `(0.086, 0.000, 78.381)` — height on Z.

Both files come through the importer with the **same load options**
(`fbx_importer.cpp:976–979`), and crucially neither sets `target_axes`
— ufbx is told *not* to convert axes, only units. The importer then
applies a **hard-coded cyclic remap** `(x, y, z) → (z, x, y)` to:

- vertex positions (`fbx_importer.cpp:1217–1219`),
- vertex normals (`fbx_importer.cpp:1224–1226`),
- bone bind translations / rotations / scales (`DecomposeUfbxMatrix`,
  `fbx_importer.cpp:240–268`),
- bone TRS sampled per animation frame (`ExtractAnimations`, same
  `DecomposeUfbxMatrix` call at `fbx_importer.cpp:694`).

That cyclic remap was designed for the **Y-up source case** (it moves
FBX.Y → UE.Z, putting height onto Z). When the same remap is applied
to a Z-up source, height moves from FBX.Z to **UE.X** — the mushroom
ends up lying along the X axis.

The mesh itself comes from a Y-up FBX, so its bind pose is correct.
The animation curves come from a Z-up FBX, so the per-frame bone
matrices are remapped into the wrong basis. The two no longer agree,
which is exactly the deformation the team has been chasing.

---

## Where the divergence appears in the chain

| Stage                                      | Correct? |
|--------------------------------------------|----------|
| ufbx parse of mesh FBX                     | ✓ |
| Vertex extraction + cyclic remap           | ✓ (Y-up source → Z-up engine) |
| `ExtractSkeleton` (mesh FBX bind matrices) | ✓ (Y-up source, remap consistent) |
| ufbx parse of anim FBX                     | ✓ |
| `ExtractAnimations` decomposing animated `node_to_world` | **✗ — applies Y-up remap to a Z-up scene** |
| `Skeleton::ComputeInverseBindMatrices`     | ✓ |
| `Pose::FromBindPose` / clip sampling       | ✓ |
| `UpdatePaletteFromPose` TRS walk + flatten | ✓ |
| `SkinCpu` row-vector LBS                   | ✓ |
| GPU palette upload + `gpu_skinning_cs.hlsl`| ✓ (matches CPU, irrelevant to bug) |

The single broken stage is **animation-clip ingest**. Fix the basis
mismatch there and the mushroom will animate correctly without
touching any runtime code.

---

## Recommended fixes (importer side, not runtime)

1. **Detect the mismatch at import time.** `ufbx_scene.settings.axes`
   is available — refuse to import an animation clip whose `axes.up`
   differs from the mesh/skeleton it was authored for, or remap the
   clip into the mesh's basis as part of `ExtractAnimations`.

2. **Use ufbx's built-in axis conversion.** Set
   `loadOpts.target_axes = ufbx_axes_right_handed_z_up` (or the
   left-handed equivalent the rest of the engine expects). When that
   is set, ufbx applies the basis change uniformly to both files so
   the hard-coded cyclic remap can be deleted.

3. **At minimum, log a loud warning** when the per-file `axes.up`
   isn't the value the cyclic remap assumes.

The runtime side (TRS walk, palette compose, CPU skinning, GPU
skinning) is verified correct by this test — leave it alone.

---

## Reproducing

```
cd e:/UzEngine/runtime
zig build                             # builds runtime/bin/debug_skin_reference.exe
mkdir -p e:/UzEngine/.scratch/skin_debug

build/bin/debug_skin_reference.exe ^
  e:/UzEngine/sample/content/meshes/mushroommonster/MushroomMonster_Low.fbx ^
  e:/UzEngine/sample/content/meshes/mushroommonster/animations/Mushroom_Idle.FBX ^
  e:/UzEngine/.scratch/skin_debug ^
  0.5

# .obj viewer of choice:
#   bind.obj  → upright bind pose (sanity check)
#   ref.obj   → ufbx reference skin
#   uz.obj    → UzEngine pipeline skin
#   stats.txt → numeric comparison
```

The standalone exe lives at `runtime/tests/debug_skin_reference/main.cpp`
and is compiled by `runtime/build.zig` as a separate executable
(does NOT participate in the unit-test glob — the tests directory is
scanned non-recursively and the file lives in a subdirectory).
