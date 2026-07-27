# Proposal: phase0_scene-load-optimization

## Why

Loading the Sponza sample scene takes ~5.3 s on an RTX 4090 + NVMe machine — far slower
than equivalent scenes in Unreal/Unity/Godot, which load pre-processed (cooked) content
in well under a second. Measured breakdown (uzengine.exe --verbose, 2026-07-26):

| Phase | Time | Root cause |
|---|---|---|
| Mesh loading (138 .uzmesh, ~190 MB) | 4.9 s | Single-threaded read+parse+staging on main thread, 4 meshes/frame budget, `WaitIdle()` per flush |
| Texture loading (70 raw PNGs, 501 MB) | 3.8 s (overlapped) | No cooked `.uztexture` companions exist — full spng decode + CPU mipgen + uncompressed RGBA8 upload (1.52 GB) |
| Material PSO DXC compiles (26 unique) | 1.28 s | Synchronous DXC on main thread inside first SyncWorldToDrawList, no bytecode disk cache |
| .uzmat parsing | minor | Re-read+re-parsed once per entity (138 reads for 28 unique files) |

The preprocessing system other engines use ALREADY EXISTS in UzEngine (`uzcook` →
`.uztexture` BC1/BC5/BC7 + `.uzimport` content-hash sidecars + `.uzpak`), and the runtime
already prefers cooked companions (`SceneRenderer::LoadTexture` probes `foo.uztexture`
before `foo.png` and uploads it with zero CPU decode via the UploadRing). The Sponza
content was simply never cooked, and the mesh/material path has no parallelism.

Source: load-time analysis session 2026-07-26 (measured logs; see design.md).

## What Changes

1. **Cook Sponza textures** with `uzcook --textures-only` so the runtime takes the
   existing `.uztexture` fast path (BC-compressed, GPU-ready mips, ~4x less upload/VRAM).
   Derived `.uztexture`/`.uzimport` files are gitignored (reproducible via uzcook).
2. **Remove the mid-frame `WaitIdle()` from `FlushDeferredMeshUploads`** — record copies
   + barriers in the frame command list and defer staging-buffer destruction to the
   existing fence-guarded deferred-destroy queue instead of stalling CPU+GPU.
3. **Parallelize mesh file I/O + parse** with an `AsyncMeshLoader` worker pool
   (mirrors `AsyncTextureDecoder`): `SceneRenderer::LoadMesh` reserves a placeholder
   slot and submits; `FlushPendingLoads` polls completed CPU meshes and batch-uploads.
   Raise the per-frame integration budget accordingly.
4. **Cache parsed `.uzmat` material files per unique path** during scene load
   (138 reads → 28 reads for Sponza).
5. **Add a DXIL bytecode disk cache for material PSOs** keyed by graph content hash +
   shader model, so warm runs skip the 26 DXC compiles.
6. **Wire the dead CVar `r.Scene.MaxTextureUploadsPerFrame`** (currently defined but the
   hardcoded constant is used instead).
7. **Fix prefab loss on the 3-arg `LoadScene` overload** — callers without a VFS
   silently skip `"prefab"` entries (runtime.cpp / application_platform.cpp paths).

Out of scope (future tasks): editor auto-cook-on-demand file watching, runtime .uzpak
mounting, texture mip streaming.

## Impact

- Affected specs: asset-pipeline (texture cook usage), renderer (mesh upload flow)
- Affected code:
  - `runtime/engine/renderer/mesh/gpu_mesh.{h,cpp}` (flush without WaitIdle)
  - `runtime/engine/renderer/upload/async_mesh_loader.{h,cpp}` (new)
  - `runtime/engine/renderer/scene/scene_mesh_assets.cpp`, `scene_texture_assets.cpp`,
    `scene_sync.cpp`, `scene_renderer_impl.h`, `scene_renderer_init.cpp`
  - `runtime/engine/renderer/material/material_pipeline.{h,cpp}` (bytecode disk cache)
  - `runtime/engine/ecs/loaders/scene_loader.cpp` (.uzmat cache + prefab VFS fix)
  - `sample/content/textures/sponza/*.uztexture` (generated, gitignored)
- Breaking change: NO (formats unchanged; loose PNG path remains as fallback)
- User benefit: Sponza scene load drops from ~5.3 s toward ~1 s; ~4x less texture VRAM
  (1.52 GB → ~0.4 GB); no mid-frame GPU stalls while streaming meshes in; warm-run
  material compile cost near zero. All existing scenes keep working unchanged.
