## 1. Importer Class

- [ ] 1.1 Create `src/engine/editor/importer.h/cpp` -- `Importer::import(source_path)` as separate class
- [ ] 1.2 Mesh import stub: copy `.glb` to `.import/` with settings recorded
- [ ] 1.3 Texture import: copy to `.import/`, record width/height/channels from PNG header
- [ ] 1.4 PBR shader template: create `assets/templates/shader_pbr.hlsl`

## 2. Testing

- [ ] 2.1 Test mesh import stub creates correct sidecar with settings
- [ ] 2.2 Test texture import records PNG dimensions
- [ ] 2.3 Test PBR shader template content validity
- [ ] 2.4 Verify >= 95% coverage

## 3. Documentation

- [ ] 3.1 Update `docs/specs/asset-pipeline.md` with importer details
