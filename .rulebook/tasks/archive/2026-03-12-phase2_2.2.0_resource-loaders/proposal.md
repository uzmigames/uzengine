# Proposal: Resource Loaders (Phase 2.2)

## Why

Phase 3 (First Pixels) requires loading textured 3D meshes. The asset framework (importer registry, cache, sidecars, VFS) was built in Phase 1.2, but no actual mesh or texture importers exist yet. Without these, the renderer cannot load any real geometry or textures — only hardcoded vertex data. The .uzmesh and .uztexture binary formats must also be stabilized before writing GPU upload code, as changing formats after runtime loaders exist is costly.

## What Changes

### New Importers

1. **MeshImporter v1** — Uses fastgltf to parse .glb/.gltf, extract vertex positions/normals/UVs/indices. Applies meshoptimizer vertex cache optimization. Writes .uzmesh binary (header + vertex streams + index buffer). No LOD generation, no tangents, no skinning in v1.

2. **TextureImporter v1** — Uses stb_image to decode PNG/JPG/TGA to RGBA8. Generates box-filter mipmap chain. Writes .uztexture binary (header + mip offsets + uncompressed RGBA8 data). No BC7 compression in v1.

### Binary Format Stabilization

3. **.uzmesh format** — Lock binary layout: `UzMeshHeader` (magic 'UZMS', version, LOD count, submesh count, flags) + `UzMeshLOD[]` + `UzMeshSubmesh[]` + vertex data + index data. Version field enables future format evolution.

4. **.uztexture format** — Lock binary layout: `UzTextureHeader` (magic 'UZTX', version, width, height, mip count, format, flags) + `MipLevel[]` + mip data blobs.

### Runtime Loaders

5. **.uzmesh runtime loader** — Read header, validate magic + version, upload vertex buffer + index buffer to GPU via RHI. Returns `Mesh` handle ready for rendering.

6. **.uztexture runtime loader** — Read header, validate magic + version, create D3D12 texture, upload RGBA8 data via upload context, register bindless SRV descriptor.

### uzcook Integration

7. Register MeshImporter + TextureImporter in uzcook tool. `uzcook assets/` processes all meshes and textures through the cache pipeline.

## Impact

- Affected specs: `docs/engine/08-resource-system/06-mesh-pipeline.md`, `07-texture-pipeline.md`
- Affected code: `core/asset/importers/`, `resource_system/mesh/`, `resource_system/texture/`, `tools/uzcook/`
- Breaking change: NO — additive (new importers + loaders)
- User benefit: Load real 3D meshes and textures from artist files (.glb, .png); asset cache avoids reimport
- Enables: Triangle to Screen (3.0), Textured Rendering (3.0), Deferred Pipeline (3.3)
