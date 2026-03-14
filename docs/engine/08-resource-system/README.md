# Resource System

Asset registry, importers, content-hash caching, .uzpak archives, VFS, mesh and texture pipelines, and offline asset cooking.

> **Status**: Planned
> **Source**: `runtime/engine/resource/`, `tools/uzcook/`

| Document | Topic |
|----------|-------|
| [01-overview.md](01-overview.md) | Asset system overview, registry, handle types, lifetime management |
| [02-importers.md](02-importers.md) | Asset importers — glTF, FBX, PNG/DDS/KTX2, WAV/OGG |
| [03-cache-and-sidecars.md](03-cache-and-sidecars.md) | Content-hash cache, sidecar metadata files, invalidation |
| [04-pak-archives.md](04-pak-archives.md) | .uzpak archive format, mount points, streaming |
| [05-virtual-filesystem.md](05-virtual-filesystem.md) | VFS overlay — loose files, pak mounts, redirect rules |
| [06-mesh-pipeline.md](06-mesh-pipeline.md) | Mesh import, meshoptimizer, LOD generation, vertex cache optimization |
| [07-texture-pipeline.md](07-texture-pipeline.md) | Texture import, BC7/BC5/BC4 compression, mip generation |
| [08-asset-cooking.md](08-asset-cooking.md) | Offline asset cooking pipeline — uzcook CLI, incremental cooking |
