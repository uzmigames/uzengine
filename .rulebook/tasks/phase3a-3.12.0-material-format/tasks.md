## 0. Material Graph Core (prerequisite)

- [x] 0.1 Implement MaterialExpression base struct with pin system, factory, and type enum (material_expression.h/.cpp)
- [x] 0.2 Implement MaterialGraph class with connection management, cycle detection, topological sort, content hashing (material_graph.h/.cpp)
- [x] 0.3 Implement JSON serialization/deserialization for material graphs (ToJson/FromJson)
- [x] 0.4 Implement .uzmat file I/O (SaveToFile/LoadFromFile) using JSON format
- [x] 0.5 Add Material log category to engine logging system (log.h)

## 1. Implementation

- [x] 1.1 Define .uzmat file format spec — JSON-based material graph format with versioning, domain, shading model, expressions array with pins and connections
- [x] 1.2 Implement .uzmat parser (MaterialGraph::FromJson + LoadFromFile)
- [x] 1.3 Implement .uzmat writer (MaterialGraph::ToJson + SaveToFile)
- [ ] 1.4 Implement MaterialManager (load by path, cache, reference counting)
- [ ] 1.5 Implement texture path resolution (relative to .uzmat file or project root)
- [ ] 1.6 Implement material-to-GPU upload (resolve textures → SRV indices, fill PBRMaterial)
- [ ] 1.7 Implement material hot-reload via FileWatcher (detect change, reload, update GPU data)
- [ ] 1.8 Create default materials (default_lit.uzmat, default_unlit.uzmat) in engine resources
- [ ] 1.9 Update demos to use .uzmat files instead of hardcoded parameters
- [ ] 1.10 Create material showcase demo (multiple .uzmat files, hot-reload demonstration)

## 2. Testing

- [ ] 2.1 Test .uzmat parse roundtrip (write → read → compare all fields)
- [ ] 2.2 Test MaterialManager cache (load same path twice → same instance)
- [ ] 2.3 Test MaterialManager reference counting (release → cache eviction)
- [ ] 2.4 Test texture path resolution (relative and absolute paths)
- [ ] 2.5 Test material hot-reload (modify .uzmat on disk → GPU material updates)
- [ ] 2.6 Test missing texture fallback (invalid path → default white/flat textures)
- [ ] 2.7 Test default materials load correctly from engine resources
- [ ] 2.8 Test malformed .uzmat file produces clear error message
- [ ] 2.9 Verify 95%+ coverage on all new code

## 3. Documentation

- [ ] 3.1 Update `docs/engine/12-rendering/05-material-system.md` with .uzmat format spec
- [ ] 3.2 Add material authoring guide (creating .uzmat files, texture requirements, hot-reload workflow)
