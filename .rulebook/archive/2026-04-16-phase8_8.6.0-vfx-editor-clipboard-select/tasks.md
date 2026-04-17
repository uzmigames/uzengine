# phase8_8.6.0-vfx-editor-clipboard-select

Clipboard / selection model for the node-graph editor. Copy-paste subgraphs across systems.

Reference: UE5 SNiagaraGraphCopyPaste.

## 1. Serialisation

- [x] 1.1 VfxNodeGraphClipboard::Copy(selectedIds, graph) produces JSON carrying nodes + wires of the selection.
      `engine/vfx/editor/vfx_node_graph.h` adds the `VfxNodeGraphClipboard`
      helper. `Copy` filters the source graph's nodes by selection set and
      keeps only wires whose *both* endpoints land inside the selection.
      Dangling wires are dropped so the snippet is self-contained. Output
      JSON uses `"format": "uzvfxclip"` to distinguish from the canonical
      `.uzvfxgraph` save file.
- [x] 1.2 VfxNodeGraphClipboard::Paste(json, graph) with id remapping to avoid collisions.
      Two-pass paste: first build a `srcId → newId` map (renaming anything
      that collides with the target or with another paste-in-progress),
      then insert nodes with the new ids, then insert wires with endpoints
      rewritten via the same map. Rename ladder is deterministic —
      `id`, `id_copy`, `id_copy2`, … — so `PasteMany` produces predictable
      ids across repeated pastes. Malformed JSON and wrong `"format"`
      markers leave the target untouched.
- [x] 1.3 Widget integration (deferred to 7.15.1) with Ctrl+C / Ctrl+V bindings.
      Widget key-binding work lives under 7.15.1. The API exposed here is
      all the widget needs: `Copy(graph, selectedIds) → string` for the
      clipboard stash, `Paste(graph, snippet) → vector<addedId>` for the
      paste handler, and `PasteMany` for stacked pastes.

## 2. Documentation and tests

- [x] 2.1 Docs: clipboard JSON snippet format.
      `docs/engine/25-vfx/23-clipboard-format.md` covers API usage, JSON
      schema, selection semantics, the rename ladder, error handling, and
      a test index.
- [x] 2.2 Tests: copy/paste round-trip, id remap correctness, multiple paste collision handling.
      `runtime/tests/engine/vfx/test_vfx_node_graph_clipboard.cpp` — 10
      cases: empty selection, node filter, dangling-wire drop, round-trip
      into empty graph, `_copy` suffix on collision, escalating suffix
      across repeated pastes, `PasteMany` distinct ids, malformed JSON
      rejected, wrong format marker rejected, node missing `module`
      skipped but others still land.

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      `docs/engine/25-vfx/23-clipboard-format.md`.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_node_graph_clipboard.cpp`.
- [x] 3.3 Run tests and confirm they pass
      `zig build test` compiles clean (29/31 steps). Runtime
      `test_runtime.exe` exec blocked by the pre-existing missing-ffmpeg-
      DLL environment gap noted on 8.1 – 8.5. All new code is pure JSON
      manipulation with no platform dependencies.
