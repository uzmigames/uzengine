## 1. Stage 0 (no behaviour change)
- [x] 1.1 RgStateRegistry, tracked imports, dedup, observe mode, commit after Execute, buffer decay on submit. **Done 2026-10-09** (render_graph_states.{h,cpp}; decay counted per command list, Present = Common; notes.md).
- [x] 1.2 SceneRenderer owns the registry; RgBuilder receives it; OnQueueSubmit after each submit of the frame list. **Done 2026-10-09**, with `BindCommandList` + `IRHICommandList::GetSubmitCount` instead of per-site hooks (notes.md, decisions); the frame-0 warmup records its barriers with `Set`.
- [x] 1.3 MockDevice gtests (test_render_graph_states.cpp, the design's cases 1-12). **Done 2026-10-09**: 13/13 (cases 1-12 plus Present = Common and the builder without a registry), the truth list checking barriers and accesses with D3D12 promotion and decay.

## 2. Stage 1
- [ ] 2.1 Graph imports switched to tracked imports (the design's 7.1) with the required fixes.

## 3. Stage 2-3
- [ ] 3.1 Missing reads declared (7.1 stage-2 notes).
- [ ] 3.2 Manual barriers migrated to Transition (7.3).

## 4. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 4.1 Update or create documentation covering the implementation
- [ ] 4.2 Write tests covering the new behavior
- [ ] 4.3 Run tests and confirm they pass
