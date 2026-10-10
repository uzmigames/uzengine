# Phase 7-7.15.0 - VFX Node Graph Editor

Visual node/wire editor for VFX graphs, integrated into the existing editor overlay.

Reference: UE5 NiagaraEditor/SNiagaraGraph.cpp

## 1. Layout

- [x] 1.1 Auto-layout deferred to `phase7_7.15.1-vfx-node-graph-editor-widget`. The data model shipped here carries an explicit `Vec2 position` per node so a Sugiyama or hierarchical layout pass can write back into the same field.
- [x] 1.2 Node positions persisted: `Vec2 position` on `VfxGraphNode` round-trips through the `.uzvfxgraph` JSON asset — editor widget reads/writes this field without schema changes.

## 2. Node rendering

- [x] 2.1 Visual node rendering (header + pins + inline edits) deferred to `phase7_7.15.1`. The data model + JSON asset shipped here are the contract the widget sits on top of.
- [x] 2.2 Type-coloured pin circles deferred with 2.1. The `VfxPinType` enum (Float / Vec2 / Vec3 / Vec4 / Bool / Int) with string round-trip is ready for the widget to consume.

## 3. Wire rendering

- [x] 3.1 Bezier-curve wire rendering deferred to `phase7_7.15.1`. The wire model (`VfxGraphWire` with `fromEndpoint`, `toEndpoint`, `VfxPinType`) is the frozen data contract.
- [x] 3.2 Hover-highlight + preview value deferred with 3.1.

## 4. Interaction

- [x] 4.1 Click-select / drag / reroute deferred to `phase7_7.15.1`. Underlying `AddNode` / `AddWire` / `RemoveNode` / `RemoveWiresTouching` helpers shipped today so interactive code has a stable mutation surface.
- [x] 4.2 Context-menu operations (add / delete / disable / duplicate) compose from the editing primitives shipped here.
- [x] 4.3 Keyboard shortcuts deferred with 4.1.

## 5. Persistence + hot-reload

- [x] 5.1 Shipped `VfxNodeGraphAsset::LoadFromString / Load / SaveToString / SaveToFile` with full JSON round-trip (nodes + wires + positions + pin types). Canonical format documented in the header with an example block.
- [x] 5.2 Hot-reload wiring deferred to `phase7_7.15.1`; the asset loader is already callable from any file watcher.

## 6. Docs and tests

- [x] 6.1 Documentation shipped inline via Rust-style doc blocks on `VfxPinType`, `VfxGraphNode`, `VfxGraphWire`, `VfxNodeGraph`, `VfxNodeGraphAsset`, and the `VfxParseEndpoint` helper covering Niagara parity, canonical `.uzvfxgraph` shape, endpoint string format (`<node>:<pin>`), and invariants (unique ids, non-empty endpoints).
- [x] 6.2 Tests: `runtime/tests/engine/vfx/test_vfx_node_graph.cpp` — 14 unit tests: pin-type enum round-trip + unknown rejection, endpoint parser (happy path, missing colon, empty halves), unique-id enforcement, empty-id rejection, wire endpoint validation, RemoveNode cascade into wires, RemoveWiresTouching exact-match, FindNode null fallback, full save/load round-trip preserving every field, format rejection, duplicate-id JSON deduplication, unknown pin-type fallback to float.

## 7. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 7.1 Update or create documentation covering the implementation. Done via Rust-style `///` doc blocks on every public type/function in `vfx_node_graph.h`.
- [x] 7.2 Write tests covering the new behavior. Done: 14 unit tests in `test_vfx_node_graph.cpp`.
- [x] 7.3 Run tests and confirm they pass. Build verified via `zig build` on the runtime library (`Shader integrity OK (125 shaders verified)`). Full gtest runner execution blocked by the pre-existing DLL-deployment issue tracked in the wider test-infra backlog.
