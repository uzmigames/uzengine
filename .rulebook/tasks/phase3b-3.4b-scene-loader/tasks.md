## 1. Implementation

- [ ] 1.1 Define .uzscene JSON format (entities array, name + parent + components)
- [ ] 1.2 Implement SceneLoader: parse JSON → create entities → deserialize via PropertySystem
- [ ] 1.3 Implement parent-child hierarchy setup from scene file
- [ ] 1.4 Implement headless scene tick (load + tick N frames without renderer)
- [ ] 1.5 Implement scene save: World → .uzscene JSON
- [ ] 1.6 Validate save → reload roundtrip (identical state)

## 2. Testing

- [ ] 2.1 Test .uzscene loading: entities created with correct components
- [ ] 2.2 Test hierarchy: parent-child relationships correct after load
- [ ] 2.3 Test headless: load scene, tick 100 frames, verify behavior state
- [ ] 2.4 Test save → reload → identical state
- [ ] 2.5 Test error handling: missing file, invalid JSON, unknown component
- [ ] 2.6 Verify 95%+ coverage

## 3. Documentation

- [ ] 3.1 Update `docs/engine/11-scene/` with .uzscene format spec
- [ ] 3.2 Document scene loading pipeline
