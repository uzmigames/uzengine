## 1. Importer Class

- [x] 1.1 Create `src/engine/editor/importer.h/cpp` -- `Importer::import(source_path)` as separate class
- [x] 1.2 Mesh import stub: copy `.glb` to `.import/` with settings recorded
- [x] 1.3 Texture import: copy to `.import/`, record width/height/channels from PNG header
- [x] 1.4 PBR shader template: create `assets/templates/shader_pbr.hlsl`

## 2. Testing

- [x] 2.1 Test mesh import stub creates correct sidecar with settings
- [x] 2.2 Test texture import records PNG dimensions
- [x] 2.3 Test PBR shader template content validity
- [x] 2.4 Verify >= 95% coverage — **42 tests, all passing, 1389/1389 C++ tests pass**

## 3. Documentation

- [x] 3.1 Update `docs/specs/asset-pipeline.md` with importer details (Section 1b added)
