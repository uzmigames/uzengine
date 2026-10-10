## 1. Implementation
- [x] 1.1 Add `attachEntity` (Entity) + `attachBoneName` (std::string) fields to `VfxEmitterComponent`
- [x] 1.2 Add matching fields to `ParticleEmitter` runtime + sync in scene_vfx_sync create/restart paths
- [x] 1.3 Implement `ResolveAttachTransform()` helper in scene_vfx_sync that looks up SkeletalMeshComponent and queries the bone world matrix
- [x] 1.4 Override emitter world position each frame before the spawn CS dispatch when attach is active
- [x] 1.5 Handle missing-bone / missing-entity with one-shot warning log + fallback to SceneComponent transform
- [x] 1.6 Serialize `attachBoneName` (+ entity reference by name/id) in emitter_serializer JSON round-trip
- [x] 1.7 Add "Attach" UI section in Parameters panel with entity picker + bone dropdown

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
