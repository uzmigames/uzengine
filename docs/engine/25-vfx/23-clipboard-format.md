# VFX node-graph clipboard

Copy / paste subgraphs across `VfxNodeGraph` instances (or within the same
graph) using a round-trippable JSON snippet. Enables cross-system authoring
reuse — a fire trail built in one system lands in another with a single
`Ctrl+C` → switch graph → `Ctrl+V`.

Reference: UE5 `SNiagaraGraphCopyPaste`.

## API

```cpp
// Serialise selected node ids + their internal wires to a JSON snippet.
std::string snippet = VfxNodeGraphClipboard::Copy(graph, {"n0", "n1"});

// Paste into another graph; colliding ids get a `_copy`, `_copy2`, …
// suffix and every wire endpoint is rewritten to the new ids.
auto addedIds = VfxNodeGraphClipboard::Paste(targetGraph, snippet);

// Paste the same snippet N times — each paste receives distinct ids.
auto triplicate = VfxNodeGraphClipboard::PasteMany(targetGraph, snippet, 3);
```

The JSON shape mirrors `.uzvfxgraph` except `"format"` is `"uzvfxclip"` and
the version stream starts fresh at 1 so the two formats can evolve
independently:

```json
{
  "format":  "uzvfxclip",
  "version": 1,
  "nodes": [
    { "id": "n0", "module": "Spawn",  "position": {"x": 0, "y": 0} },
    { "id": "n1", "module": "Update", "position": {"x": 300, "y": 0} }
  ],
  "wires": [
    { "from": "n0:out", "to": "n1:in", "type": "float" }
  ]
}
```

## Selection semantics

- **Node filter.** Only nodes whose id appears in the caller's selection
  list are included.
- **Wire filter.** A wire is included only when *both* endpoints resolve
  inside the selection. Dangling wires (one endpoint outside) are dropped
  at Copy time because the destination graph has no way to satisfy them.

## Id renaming on paste

`Paste` rewrites every colliding id to a unique form before inserting. The
rename uses a deterministic suffix ladder so repeated pastes produce
predictable ids and wire rewrites stay stable:

| Collision count | Produced id     |
|-----------------|-----------------|
| 0               | `id`            |
| 1               | `id_copy`       |
| 2               | `id_copy2`      |
| 3               | `id_copy3`      |
| …               | `id_copy<N>`    |

Wire endpoints are rewritten in the same pass — `"n0:out"` becomes
`"n0_copy:out"` when `n0` collides, so every connection survives the
rename.

## Error handling

- Malformed JSON and wrong `"format"` markers are rejected; the target
  graph is left untouched and `Paste` returns an empty `added` list.
- Nodes missing the `module` field log a `VFX/Warning` and are skipped.
  Remaining nodes and wires still land.

## Tests

`runtime/tests/engine/vfx/test_vfx_node_graph_clipboard.cpp` covers: empty
selection → valid empty snippet, node filter, dangling-wire drop, round-trip
into empty graph, `_copy` suffix on collision, escalating suffix across
repeated pastes, `PasteMany` produces distinct ids, malformed JSON rejected,
wrong `"format"` marker rejected, node missing `module` skipped.
