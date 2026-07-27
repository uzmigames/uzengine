# Scene load performance: cook textures + async mesh + fence-aged staging

**Category**: performance
**Tags**: asset-loading, performance, uzcook, uztexture, async-mesh, staging, d3d12

## Description

UzEngine's texture fast path REQUIRES cooked .uztexture companions — SceneRenderer::LoadTexture probes "foo.uztexture" before "foo.png" and uploads BC data via UploadRing with zero CPU decode (0.3-0.5ms per 2K texture vs spng decode + CPU mipgen + 4x VRAM). Content that ships only PNGs silently takes the slow path. Cook with: uzcook.exe --source <dir> --output <dir> --textures-only (incremental via .uzimport content-hash sidecars; outputs are gitignored derived data).

Mesh loading is async: RequestMeshAsync reserves a placeholder slot (invalid GPUMesh) and AsyncMeshLoader workers read+parse .uzmesh off-thread; IntegrateAsyncMeshLoads GPU-uploads under r.LazyLoad.MeshesPerFrame (32/frame default, 0=unlimited). Entities claim slots only when m_meshes[slot].IsValid() — never draw placeholders.

Stall-free upload flush pattern: keep End()/Execute() (D3D12 implicit buffer state promotion/decay needs the ExecuteCommandLists boundary between copy and draw when no explicit buffer barriers exist) but replace WaitIdle + immediate staging release with handles moved into frames-in-flight-aged buckets (m_stagingRetireBuckets, destroyed 3 SetupFrameData passes later, drained in Shutdown). NOTE: m_deferredDestroyBuffers (next-frame drain) is ONLY safe for producers that fence/WaitIdle before queueing — same-frame unfenced staging must use the aged buckets.

Sponza measured: 5.3s -> 3.0s load (RTX 4090/NVMe); startup texture drain 92 iterations -> 0; mesh phase 4.9s -> 1.25-1.7s.
