# Asset Loading Pipeline

How scene assets (meshes, textures, materials) travel from disk to GPU, and how
to keep loads fast. Measured reference: Sponza (138 meshes / ~190 MB, 70
textures / 501 MB source PNGs) loads in ~3 s on RTX 4090 + NVMe; before the
2026-07 optimization pass it took ~5.3 s (see `runtime/CHANGELOG.md` v0.9.7).

## Cooked textures (`.uztexture`) — the single biggest lever

The runtime always probes for a cooked companion before a raw image:
`textures/foo.png` → `textures/foo.uztexture` (`SceneRenderer::LoadTexture`).

- **Cooked path**: file bytes pass straight through the async worker (no
  decode) and upload via the `UploadRing` (`TextureLoader::LoadUzTexViaRing`,
  ~0.3–0.5 ms per 2K texture). Formats: BC7 (color / metallicRoughness), BC5
  (normals — shaders reconstruct Z from RG), BC4 (single-channel masks), BC1.
  Mips are prebuilt. VRAM is ~4× smaller than uncompressed RGBA8.
- **Raw path (fallback)**: spng/libjpeg-turbo/stb decode to RGBA8 + full CPU
  mip-chain generation on `AsyncTextureDecoder` workers, then uncompressed
  upload. Correct but slow and VRAM-hungry — dev-time convenience only.

Cook a content directory once (incremental afterwards via `.uzimport`
content-hash sidecars):

```bash
zig build            # builds uzcook.exe into build/bin
./build/bin/uzcook.exe --source <content>/textures/<set> \
                       --output <content>/textures/<set> --textures-only
```

Companions are written NEXT to the sources and are **gitignored** (derived
data — reproducible, never commit). Format selection is filename-driven
(`uzcook.cpp SelectTextureFormat`): `baseColor/albedo/diffuse` → BC7 sRGB,
`normal` → BC5, `metallicRoughness` (glTF combined map) → **BC7 linear**
(needs G=roughness, B=metallic — never single-channel), `roughness/metallic/
ao/opacity` alone → BC4, unknown → BC7 sRGB.

## Async mesh loading

`.uzmesh` files load on the `AsyncMeshLoader` worker pool (`max(2, hw/2)`
threads): file read + `LoadMeshFromMemory` parse off the main thread, with a
resolved-filesystem-path fast lane that bypasses the shared VFS mutex.

- `SceneRenderer::RequestMeshAsync()` reserves a placeholder slot per path and
  submits; entities stay `meshLoaded=false` until the slot's GPUMesh is
  resident, then `SyncWorldToDrawList` claims it (LOD0 and LOD1..7 paths).
- `IntegrateAsyncMeshLoads()` (called from `FlushPendingLoads`, i.e. before
  the same frame's world sync) polls completed CPU meshes and GPU-uploads up
  to `r.LazyLoad.MeshesPerFrame` per frame (default 32, `0` = unlimited).
  The budget bounds the transient staging-memory spike, not file I/O.
- Upload staging is batched (`UploadMeshDeferred` + one
  `FlushDeferredMeshUploads` per frame) and **stall-free**: no
  `device->WaitIdle()` — staging buffers retire through
  frames-in-flight-aged buckets destroyed 3 frames later.

## Materials

- `.uzmat` graphs are parsed once per unique path per `LoadScene`
  (memoized — Sponza's 138 entities share 28 files).
- Material pixel-shader DXIL is disk-cached by content hash
  (`cache/shaders/<backend>/`); warm runs skip DXC entirely. Remaining
  warm-run cost is driver-side PSO creation (future: ID3D12PipelineLibrary).

## Tuning CVars

| CVar | Default | Meaning |
|------|---------|---------|
| `r.LazyLoad.MeshesPerFrame` | 32 | Async-loaded meshes GPU-integrated per frame (0 = unlimited) |
| `r.Scene.MaxTextureUploadsPerFrame` | 8 | Async-decoded textures GPU-uploaded per frame (1–32) |

Both can be set per scene in the `.uzscene` `"cvars"` block or via
`--cvar name=value`.

## Diagnosing slow loads

Run with `--verbose` and read the timestamps:

- `Pre-loading scene textures...` → `Pre-load complete` — startup drain
  (should be near-instant when content is cooked).
- `SceneRenderer: async mesh integrated ...` — first/last lines bracket the
  mesh phase.
- `SceneRenderer: async texture uploaded ...` — texture phase; `Loaded
  .uztexture via ring` lines confirm the cooked fast path (absence of them
  means the content is not cooked — run uzcook).
- `MaterialPipeline: compiled material PSO` — count × ~15–50 ms of main-thread
  PSO creation.
