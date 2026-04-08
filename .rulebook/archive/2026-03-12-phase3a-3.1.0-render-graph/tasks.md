## 1. Implementation

- [x] 1.1 Implement RgBuilder: pass/texture/buffer declaration API, DAG construction
- [x] 1.2 Implement RgPass: read/write resource declarations, execution callback registration
- [x] 1.3 Implement RgTexture/RgBuffer: virtual resource handles with usage tracking
- [x] 1.4 Implement topological sort with cycle detection
- [x] 1.5 Implement automatic RHI barrier scheduling from resource state transitions
- [x] 1.6 Implement transient resource aliasing (lifetime analysis, memory pool reuse)
- [x] 1.7 Implement pass culling (unused output resources → skip execution)
- [x] 1.8 Implement frame resource pooling (reuse allocations across frames)
- [x] 1.9 Migrate existing clear-screen/triangle passes to render graph
- [x] 1.10 Enforce graph-only rule: remove all manual ResourceBarrier calls from renderer (all 5 demos migrated)

## 2. Testing

- [x] 2.1 Test DAG construction: single pass, linear chain, diamond dependency
- [x] 2.2 Test cycle detection: reject circular dependencies with error
- [x] 2.3 Test barrier insertion: correct resource states at pass boundaries
- [x] 2.4 Test transient aliasing: non-overlapping lifetimes share memory
- [x] 2.5 Test pass culling: disabled output → pass skipped
- [x] 2.6 Test frame pooling: allocations reused across frames
- [ ] 2.7 Verify zero D3D12 validation errors in all graph configurations — deferred to 3.2.0 (needs integrated demo)
- [x] 2.8 Verify 95%+ coverage on all new code

## 3. Documentation

- [x] 3.1 Update `docs/engine/12-rendering/04-render-graph.md` with final API
- [x] 3.2 Add render graph integration guide for pass authors
