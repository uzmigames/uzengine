# Proposal: phase2-fbx-import

## Why

Artists working in Maya, 3ds Max, and other DCC tools export geometry and animation data as FBX files. Without an FBX importer, every mesh in UzEngine must be hard-coded or manually converted using external tools, which breaks the content creation workflow entirely. The engine currently has no real mesh import path — assets are geometry stubs defined in C++ source code.

The FBX format is the de facto interchange format for skeletal meshes and animation in the game industry. Supporting it via the open-source ufbx library (no SDK license required) means studios can use their existing Maya/3ds Max pipelines without modification. Paired with fastgltf for the glTF path, this covers virtually all modern authoring tools.

Once imported, meshes must be converted to engine-native `.uzmesh` and `.uzskel` binaries that the runtime can load efficiently. LOD generation via meshoptimizer reduces GPU draw call cost at distance, and a VSCode model preview panel lets developers and AI agents inspect imported assets without leaving the editor.

## What Changes

- Integrate ufbx (single-header, no license cost) as the FBX parsing backend; integrate fastgltf for glTF 2.0 + GLB support
- Implement `src/assets/fbx_importer.cpp` — reads .fbx, extracts mesh geometry (positions, normals, UVs, tangents), material slot bindings, bone hierarchy, skin weights, and morph targets
- Implement `src/assets/gltf_importer.cpp` — reads .gltf/.glb, maps to the same internal `ImportedMesh` / `ImportedSkeleton` intermediate structs
- Write `src/assets/mesh_compiler.cpp` — converts `ImportedMesh` to `.uzmesh` binary (header + interleaved vertex buffer + index buffer) and `ImportedSkeleton` to `.uzskel` (bone name table, inverse bind pose matrices, parent indices)
- Integrate meshoptimizer to generate 4 LOD levels (100%, 50%, 25%, 12.5% triangle counts) and embed them in the `.uzmesh` file as LOD sections
- Add `asset.import` IPC command that accepts a file path, runs the appropriate importer, streams progress events back (0–100%), and returns the output asset paths
- Implement a turntable model preview panel in the VSCode extension — rotates the imported mesh under a directional light and streams JPEG frames via the existing WebSocket pipeline
- Register `.uzimport` sidecar JSON per source file storing import settings (coordinate system flip, scale factor, which animation clips to extract, LOD bias)
- Add unit tests in `tests/assets/` covering FBX round-trip (vertex count, bone count, UV channel preservation) and glTF round-trip using bundled test fixtures
- Update `docs/specs/asset-pipeline.md` and `docs/specs/skeletal-mesh.md` with the importer architecture, file format layouts, and LOD generation parameters

## Impact

- Affected specs: docs/specs/asset-pipeline.md, docs/specs/skeletal-mesh.md, docs/specs/animation-system.md
- Affected code: src/assets/fbx_importer.cpp, src/assets/gltf_importer.cpp, src/assets/mesh_compiler.cpp, src/ipc/commands/, vscode-extension/src/panels/ModelPreviewPanel.ts
- Breaking change: NO
- User benefit: Artists can drag .fbx or .glb files into the Asset Browser and have them immediately available as runtime-ready .uzmesh/.uzskel assets without leaving VSCode or running external conversion tools
