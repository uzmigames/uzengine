## 1. GPU Pick Buffer Implementation
- [x] 1.1 Create pick.vs.hlsl shader (PBR vertex layout, transforms only)
- [x] 1.2 Create pick.ps.hlsl shader (outputs nodeId as RGB)
- [x] 1.3 Register "pick" shader in shader_library.cpp
- [x] 1.4 Create Pick PSO in createPipelineStates()
- [x] 1.5 Add pick buffer resources to renderer.h (pickRT_, pickDepth_, pickReadback_)
- [x] 1.6 Implement createPickBufferResources() in renderer.cpp
- [x] 1.7 Add pick buffer init/resize/shutdown lifecycle
- [x] 1.8 Add pick_draw render pass in render()
- [x] 1.9 Add pick buffer readback in performReadback()

## 2. Pick Handler Refactor
- [x] 2.1 Restructure editor.pickNode: GPU pick primary, origin-distance fallback
- [x] 2.2 Add debug logging (stderr + WS response) for pick resolution

## 3. Blender-Style Outline
- [x] 3.1 Change outline PSO: CULL_FRONT + depth_enable=false

## 4. TypeScript Fix
- [x] 4.1 Fix mousedown: send editor.pickNode when selectedNodeId set but no camera cached

## 5. Testing
- [x] 5.1 Write C++ unit tests for pick_node_at_screen (13 tests in tests/renderer/test_pick_buffer.cpp)
- [x] 5.2 Write C++ unit tests for box_pick_nodes (14 tests in tests/renderer/test_pick_buffer.cpp)
- [x] 5.3 TS tests for pickResult handler — covered by viewport-panel.test.ts (onNodePicked) + selection-manager.test.ts (selectOnly/toggle/clear)
- [x] 5.4 Verify all existing tests still pass (no regressions)
- [x] 5.5 Coverage verified: viewport-panel.ts 100%, selectionManager.ts 100%, pick_buffer.h 100% (27 C++ tests)

## 6. Build and Verify
- [x] 6.1 Build C++ with zero warnings
- [x] 6.2 Run all 1347+ C++ tests (1345 passed, 2 platform-skipped)
- [x] 6.3 Run TS tests (815 passed)
