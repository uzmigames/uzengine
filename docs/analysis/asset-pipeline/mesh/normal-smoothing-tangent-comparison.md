<!-- doc-meta
title: Normal Smoothing & Tangent Generation — Three.js vs Unity
category: engine-comparison
created: 2026-04-04
-->

# Normal Smoothing & Tangent Generation: Three.js vs Unity

Comparison of mesh normal computation, vertex smoothing, and tangent generation
across Three.js (r170+) and Unity (6000.x / 2022 LTS+). Intended as reference
for UzEngine's own mesh pipeline.

---

## 1. Three.js

### 1.1 Normal Computation — `BufferGeometry.computeVertexNormals()`

**Algorithm**: Area-weighted average of face normals (implicit).

The implementation computes face normals via cross product of two triangle edges.
Crucially, it does **not** normalize the per-face normals before accumulating them
into per-vertex sums. Because the magnitude of a cross product is proportional to
the parallelogram area (2x triangle area), larger triangles contribute more to
the final vertex normal. This is an **implicit area-weighting** scheme.

```js
// Pseudocode of the Three.js algorithm (indexed geometry path):
for each triangle (i0, i1, i2):
    pA = positions[i0], pB = positions[i1], pC = positions[i2]
    cb = (pC - pB)          // edge 1
    ab = (pA - pB)          // edge 2
    faceNormal = cross(cb, ab)   // NOT normalized — magnitude ~ 2*area
    normals[i0] += faceNormal
    normals[i1] += faceNormal
    normals[i2] += faceNormal

// After all faces:
for each vertex:
    normals[v] = normalize(normals[v])
```

**For non-indexed geometry**: every three consecutive vertices form one triangle.
Each vertex's normal is set to that triangle's face normal directly (equivalent
to flat shading), since vertices are not shared.

**Key properties**:

| Property | Value |
|----------|-------|
| Weighting | Area-weighted (implicit, via unnormalized cross product) |
| Smoothing angle threshold | **None** — all shared vertices are averaged unconditionally |
| Hard edge support | No — requires vertex duplication (see `toNonIndexed()`) |
| Attribute requirements | `position` (mandatory), `index` (for smooth normals) |
| UV seam handling | Vertices at UV seams are typically already split by loaders; `computeVertexNormals` sees them as distinct vertices |

**Limitation**: Because there is no smoothing angle, calling `computeVertexNormals()`
on a cube with shared vertices will produce rounded-looking corners rather than
crisp hard edges. To get hard edges, the geometry must have duplicated vertices
at hard edges (non-indexed, or manually split).

### 1.2 Tangent Generation

Three.js provides **two** tangent computation methods:

#### 1.2.1 `BufferGeometry.computeTangents()`

Built-in method. Computes tangent vectors from position, normal, and UV attributes
using a standard Lengyel-style algorithm (not MikkTSpace). Produces `float4`
tangents where `w` encodes handedness (+1 or -1).

```js
// Requirements:
// - index attribute (indexed geometry only)
// - position, normal, uv attributes
geometry.computeTangents();
// Result: geometry now has a 'tangent' Float32BufferAttribute (4 components)
```

**When used**: Manually called by user code. Three.js does NOT auto-compute tangents.

#### 1.2.2 `BufferGeometryUtils.computeMikkTSpaceTangents(geometry, MikkTSpace, negateSign?)`

MikkTSpace implementation via WASM. This is the **recommended** method for
normal-mapped meshes, as it matches the tangent space used by most DCC tools
(Blender, Substance, xNormal).

```js
import { computeMikkTSpaceTangents } from 'three/addons/utils/BufferGeometryUtils.js';
import { MikkTSpace } from 'three/addons/libs/mikktspace.module.js';

await MikkTSpace.ready;  // WASM init
computeMikkTSpaceTangents(geometry, MikkTSpace);
```

**Key difference**: MikkTSpace may split vertices at tangent-space discontinuities,
producing a different index buffer. The built-in `computeTangents()` never splits.

#### 1.2.3 GLTFLoader Tangent Handling

The glTF spec recommends MikkTSpace tangents when none are provided in the file.
Three.js GLTFLoader behavior:

- **Tangents present in glTF**: Imported as-is into the `tangent` attribute.
- **Tangents absent, normal map present**: Three.js does **not** auto-compute tangents.
  Instead, the shader (`MeshStandardMaterial`) uses a screen-space derivative
  fallback (`dFdx`/`dFdy` of position and UV) to compute the TBN matrix in the
  fragment shader. This is less accurate than MikkTSpace but avoids the WASM
  dependency.
- **No normal map**: Tangents are not needed and not computed.

The GLTFLoader also applies `normalScale.y = -normalScale.y` to flip the green
channel, matching the glTF convention (OpenGL-style Y-up tangent space) vs
Three.js's internal convention.

### 1.3 Flat vs Smooth Shading

#### `material.flatShading = true`

When `flatShading` is enabled on a `MeshStandardMaterial` or `MeshPhongMaterial`,
the vertex shader passes face-flat normals to the fragment shader using
`flat` interpolation qualifiers (GLSL `flat` keyword). In practice, Three.js
computes flat normals in the fragment shader via `cross(dFdx(vViewPosition), dFdy(vViewPosition))`.

This works on **any** geometry (indexed or not) without modifying the geometry data.

```js
const material = new THREE.MeshStandardMaterial({
    flatShading: true,   // fragment shader uses screen-space derivatives for normals
    color: 0xcc8866
});
```

#### `geometry.toNonIndexed()`

Converts an indexed geometry to non-indexed by duplicating all shared vertices.
After this call, every triangle has its own unique vertices. Calling
`computeVertexNormals()` on a non-indexed geometry assigns each vertex the face
normal of its triangle, producing geometrically flat shading (not just a shader
trick).

```js
const nonIndexed = geometry.toNonIndexed();
nonIndexed.computeVertexNormals();  // each vertex gets its face normal
```

**Use case**: When you need per-face attributes (colors, UVs) that differ across
shared vertices, or when you want hard geometric normals baked into the vertex
buffer for non-standard shaders.

### 1.4 OBJ Loader and Smooth Groups

The Three.js `OBJLoader` parses `s` (smoothing group) directives:

- `s 1` / `s on` / `s <N>` — enables smooth shading for subsequent faces.
- `s 0` / `s off` — disables smooth shading (flat faces).

**Implementation**: The OBJLoader creates separate geometry groups when the
smooth group changes. Vertices within a smooth group share normals (averaged);
vertices across smooth group boundaries are duplicated, creating hard edges.

**Known limitation**: If `s` changes mid-object without a `usemtl` directive,
vertex splitting may not work correctly. The OBJLoader treats smooth group
boundaries as vertex-splitting events, but does not support multiple smooth groups
within a single draw group.

### 1.5 `mergeVertices()` — Vertex Deduplication

`BufferGeometryUtils.mergeVertices(geometry, tolerance?)` merges vertices whose
attributes (position, normal, UV, etc.) are within `tolerance` of each other.

```js
import { mergeVertices } from 'three/addons/utils/BufferGeometryUtils.js';

// tolerance: spatial distance threshold for considering vertices identical
// Default: 1e-4
const merged = mergeVertices(geometry, 1e-4);
merged.computeVertexNormals();  // now normals smooth across merged vertices
```

**Algorithm**: Spatial hashing with truncated coordinates. Each vertex's attributes
are quantized to a grid cell based on tolerance, producing a hash key. Vertices
with the same hash are merged (first-seen wins for attribute values).

**Known issues**:
- Tolerance of exactly `0` can cause hash overflow (integer truncation with `~~`).
- The truncation-based hashing can miss vertices that straddle grid cell boundaries,
  even when within tolerance. Reported as [issue #24621](https://github.com/mrdoob/three.js/issues/24621).
- Very large tolerance values can merge vertices that should remain separate,
  creating holes in the mesh.

**Typical workflow**: Import non-indexed geometry (e.g., from STL) -> `mergeVertices()`
-> `computeVertexNormals()` to produce smooth normals from a flat-normal source.

---

## 2. Unity

### 2.1 Normal Computation on Import — `ModelImporter`

Unity's asset import pipeline provides fine-grained control over normal computation.

#### Import Normals Mode (`ModelImporter.importNormals`)

| Mode | Enum | Behavior |
|------|------|----------|
| **Import** | `ModelImporterNormals.Import` | Use normals from the source file (FBX/glTF/OBJ). Default. |
| **Calculate** | `ModelImporterNormals.Calculate` | Discard source normals, recompute from geometry + settings below. |
| **None** | `ModelImporterNormals.None` | No normals on the imported mesh (unlit workflows). |

#### Smoothing Angle (`ModelImporter.normalSmoothingAngle`)

When `importNormals = Calculate`, this angle (degrees) controls hard vs soft edges:

- Edges where the angle between adjacent face normals is **less than** `normalSmoothingAngle`
  are **smooth** (shared vertex normals).
- Edges where the angle **exceeds** `normalSmoothingAngle` are **hard** (vertices are
  split, each side gets the face normal).
- **Default**: 60 degrees.
- **180 degrees**: All edges smooth (no hard edges, recommended for normal-mapped characters).
- **0 degrees**: All edges hard (equivalent to flat shading).

#### Smoothing Source (`ModelImporter.normalSmoothingSource`)

| Source | Enum | Behavior |
|--------|------|----------|
| **Prefer Smoothing Groups** | `PreferSmoothingGroups` | Use FBX smoothing groups if present; fall back to angle. **Default.** |
| **From Smoothing Groups** | `FromSmoothingGroups` | Strictly use FBX smoothing groups (ignores angle). |
| **From Angle** | `FromAngle` | Ignore smoothing groups, use `normalSmoothingAngle` only. |
| **None** | `None` | Unweighted, no smoothing source. |

#### Normal Calculation Mode (`ModelImporter.normalCalculationMode`)

Controls the **weighting** used when averaging face normals into vertex normals:

| Mode | Enum | Description |
|------|------|-------------|
| **Unweighted Legacy** | `Unweighted` | Simple average of face normals (no area or angle weighting). Legacy behavior. |
| **Unweighted** | `UnweightedLegacy` | Same as above, older codepath. |
| **Area Weighted** | `AreaWeighted` | Weight by triangle area. Larger triangles have more influence. |
| **Angle Weighted** | `AngleWeighted` | Weight by the interior angle at the vertex. Produces the most geometrically correct results for most meshes. |
| **Area and Angle Weighted** | `AreaAndAngleWeighted` | Weight by both area and angle (product). **Default in modern Unity.** |

**Best practice**: `AreaAndAngleWeighted` is recommended. Pure area-weighting can
produce artifacts on meshes with very non-uniform triangle sizes. Pure
angle-weighting can produce artifacts on very thin triangles. The combined mode
handles both cases well.

### 2.2 Runtime API — `Mesh.RecalculateNormals()`

```csharp
// Basic call — no smoothing angle, averages ALL shared vertices
mesh.RecalculateNormals();

// With MeshUpdateFlags (2021.2+) — controls validation, not the algorithm
mesh.RecalculateNormals(MeshUpdateFlags.DontValidateIndices);
```

**Algorithm**: Simple unweighted average of face normals at shared vertices.
Equivalent to Three.js `computeVertexNormals()` but **without** the implicit
area weighting (Unity normalizes face normals before accumulating).

**Key limitations**:

1. **No smoothing angle parameter** — `RecalculateNormals()` does not accept a
   smoothing angle at runtime. ALL shared vertices are smoothed unconditionally.
   The import-time smoothing angle is baked into vertex splits during import.

2. **UV seam artifacts** — Vertices split at UV seams (by the importer) are seen
   as distinct by `RecalculateNormals()`, producing visible normal discontinuities
   at UV boundaries. This is the most commonly reported issue.

3. **No hard edge control** — To get hard edges at runtime, you must manually
   duplicate vertices before calling `RecalculateNormals()`.

**Community solution**: The widely-used [`NormalSolver.cs`](https://gist.github.com/runevision/6fd7cc8d841245a53df5d09ccf6b47ff)
by Rune Skovbo Johansen provides a runtime `RecalculateNormals(float angle)`
with smoothing angle support, handling UV seam vertices correctly by comparing
positions rather than vertex indices.

### 2.3 Tangent Computation

#### Import-Time (`ModelImporter.importTangents`)

| Mode | Behavior |
|------|----------|
| **Import** | Use tangents from the source file. |
| **Calculate Mikk** | Compute using **MikkTSpace** algorithm. **Default.** |
| **Calculate Legacy** | Legacy algorithm (pre-2017.3). |
| **Calculate Legacy With Split Tangents** | Legacy + split at tangent discontinuities. For fixing normal map seam artifacts. |
| **None** | No tangents imported. |

#### Runtime — `Mesh.RecalculateTangents()`

```csharp
mesh.RecalculateTangents();
```

Computes tangent vectors from positions, normals, and UV0. The runtime method
uses a simplified algorithm (not full MikkTSpace) for performance. Results may
differ slightly from import-time MikkTSpace tangents.

**Requirements**: Mesh must have normals and UV0 (texcoord0) set before calling.

### 2.4 Vertex Format

Unity uses a fixed vertex attribute layout. Normals and tangents are always
full-precision floats in the standard pipeline:

```
struct Vertex {
    float3 position;    // 12 bytes — POSITION
    float3 normal;      // 12 bytes — NORMAL
    float4 tangent;     // 16 bytes — TANGENT (xyz = tangent dir, w = handedness)
    float4 color;       // 16 bytes — COLOR (optional, can be byte4 = 4 bytes)
    float2 uv0;         //  8 bytes — TEXCOORD0
    float2 uv1;         //  8 bytes — TEXCOORD1 (lightmap, optional)
    // ... up to uv7
};
```

**Tangent handedness**: `tangent.w` is +1.0 or -1.0. The bitangent (binormal) is
computed in the shader as:

```hlsl
float3 bitangent = cross(normal, tangent.xyz) * tangent.w;
```

This convention matches MikkTSpace and is the same across all major engines
(Unreal, Unity, Godot, Three.js).

**Compressed formats** (2021+): Unity supports `VertexAttributeFormat.Float16` and
`SNorm16` for normals/tangents via `Mesh.SetVertexBufferParams()`, but defaults to
Float32. URP/HDRP may use compressed formats for mobile.

### 2.5 Import Pipeline — Format-Specific Behavior

#### FBX Import

- **Smoothing groups**: FBX files store explicit smoothing group IDs per face.
  Unity reads these and splits vertices at smoothing group boundaries.
  Adjacent faces in different smoothing groups produce hard edges.
- **Vertex splitting**: The importer splits vertices at: UV seams, smoothing group
  boundaries, material boundaries, and hard normal edges. This means the runtime
  vertex count is typically 1.5-3x the modeling-tool vertex count.
- **Tangent space**: When `importTangents = CalculateMikk`, Unity runs MikkTSpace
  on the post-split vertex buffer, ensuring tangent continuity within smooth groups.

#### glTF Import (via Unity glTF packages)

- **Normals**: glTF provides per-vertex normals. Unity imports them as-is when
  `importNormals = Import`. The glTF spec requires normals to be unit-length.
- **Tangents**: glTF provides `float4` tangents (xyz + w handedness), matching
  Unity's internal format exactly. Imported directly with no conversion needed.
- **When normals/tangents are absent**: glTF spec recommends MikkTSpace generation.
  Unity's glTF importers (both `UnityGLTF` and `com.unity.cloud.gltfast`)
  compute tangents via MikkTSpace when not present in the file.

### 2.6 Best Practices (from Unity Documentation)

1. **Normal-mapped characters**: Set `normalSmoothingAngle = 180` on import to
   prevent hard edges from creating lighting seams at normal map boundaries.

2. **Hard-surface models**: Use smoothing groups in the DCC tool (Blender, Maya,
   3ds Max). Set `normalSmoothingSource = PreferSmoothingGroups` (default).

3. **Runtime mesh modification**: If modifying vertices at runtime, call
   `RecalculateNormals()` then `RecalculateTangents()` in that order.
   Tangent computation depends on normals being correct.

4. **Avoid RecalculateNormals() on imported meshes**: The import-time normals
   (with smoothing angle, weighting, smoothing groups) are almost always better
   than the runtime `RecalculateNormals()` which has no angle threshold.

5. **UV seam normal fix**: If `RecalculateNormals()` creates visible seams at UV
   boundaries, use a community solution like `NormalSolver.cs` that compares
   vertex positions rather than indices to identify shared vertices.

---

## 3. Comparison Matrix

| Feature | Three.js | Unity |
|---------|----------|-------|
| **Normal weighting** | Area-weighted (implicit) | Configurable: Unweighted, Area, Angle, Area+Angle |
| **Smoothing angle** | None (all shared vertices averaged) | Import-time: 0-180 deg (default 60). Runtime: none. |
| **Smoothing groups** | OBJLoader only (limited) | Full support (FBX, OBJ) via `normalSmoothingSource` |
| **Tangent algorithm** | Built-in (Lengyel) + MikkTSpace (WASM) | Import: MikkTSpace (default). Runtime: simplified. |
| **Auto tangent compute** | Never (user must call) | On import (default). Runtime: user must call. |
| **glTF normal handling** | Preserves from file; no recompute | Preserves from file; optional recalculate |
| **glTF tangent fallback** | Screen-space derivatives in shader | MikkTSpace generation |
| **Flat shading** | `material.flatShading` (shader-side `dFdx/dFdy`) | Requires vertex duplication or shader derivative |
| **Vertex deduplication** | `mergeVertices(tolerance)` with spatial hash | Import-time only; no runtime API |
| **Normal precision** | Float32 (always) | Float32 (default), Float16/SNorm16 optional |
| **Tangent format** | `float4` (xyz + w handedness) | `float4` (xyz + w handedness) |
| **Hard edge creation** | Manual vertex duplication or `toNonIndexed()` | Automatic at import via smoothing angle/groups |
| **Runtime recalculate** | `computeVertexNormals()` — no angle | `RecalculateNormals()` — no angle |

---

## 4. Implications for UzEngine

### What to implement

1. **Area+Angle weighted normals** (Unity's `AreaAndAngleWeighted`) — most
   geometrically correct. Three.js's area-only weighting is a subset.

2. **Smoothing angle threshold** — essential for hard-surface models. Unity's
   import-time approach (split vertices at hard edges, then average within smooth
   groups) is the standard approach.

3. **MikkTSpace tangents** — non-negotiable for normal map compatibility. Both
   engines converge on this. Use the C reference implementation
   (`mikktspace.h` by Morten S. Mikkelsen).

4. **Tangent format**: `float4` with `w = handedness` (+1/-1). This is universal.

5. **Smoothing group support** for FBX/OBJ import — vertex splitting at group
   boundaries during import.

### What to avoid

- **No smoothing angle** (like Three.js) — forces users to pre-split geometry in
  DCC tools. Not acceptable for a general-purpose engine.
- **Unweighted normals** (like Unity's legacy mode) — produces visibly worse results
  on non-uniform meshes. Only keep as a compatibility fallback.
- **Screen-space derivative tangents** (like Three.js GLTFLoader fallback) — too
  inaccurate for AAA quality. Always compute proper tangents.

---

## Sources

- [Three.js BufferGeometry docs](https://threejs.org/docs/#api/en/core/BufferGeometry.computeVertexNormals)
- [Three.js BufferGeometryUtils docs](https://threejs.org/docs/pages/module-BufferGeometryUtils.html)
- [Three.js computeVertexNormals area-weighting discussion (#5059)](https://github.com/mrdoob/three.js/issues/5059)
- [Three.js computeVertexNormals normalization discussion (#12991)](https://github.com/mrdoob/three.js/issues/12991)
- [Three.js MikkTSpace tangents PR (#23802)](https://github.com/mrdoob/three.js/pull/23802)
- [Three.js GLTFLoader source](https://github.com/mrdoob/three.js/blob/dev/examples/jsm/loaders/GLTFLoader.js)
- [Three.js OBJLoader smooth groups issue (#13443)](https://github.com/mrdoob/three.js/issues/13443)
- [Three.js mergeVertices hashing issue (#24621)](https://github.com/mrdoob/three.js/issues/24621)
- [Unity Mesh.RecalculateNormals API](https://docs.unity3d.com/ScriptReference/Mesh.RecalculateNormals.html)
- [Unity ModelImporter.normalSmoothingAngle](https://docs.unity3d.com/6000.0/Documentation/ScriptReference/ModelImporter-normalSmoothingAngle.html)
- [Unity ModelImporterNormalCalculationMode.Unweighted](https://docs.unity3d.com/2020.1/Documentation/ScriptReference/ModelImporterNormalCalculationMode.Unweighted.html)
- [Unity ModelImporterNormalSmoothingSource](https://docs.unity3d.com/ScriptReference/ModelImporterNormalSmoothingSource.html)
- [Unity Model Import Settings reference](https://docs.unity3d.com/Manual/FBXImporter-Model.html)
- [Unity Mesh vertex data manual](https://docs.unity3d.com/6000.3/Documentation/Manual/mesh-vertex-data.html)
- [Unity VertexAttributeDescriptor API](https://docs.unity3d.com/ScriptReference/Rendering.VertexAttributeDescriptor.html)
- [NormalSolver.cs — community runtime smoothing angle](https://gist.github.com/runevision/6fd7cc8d841245a53df5d09ccf6b47ff)
