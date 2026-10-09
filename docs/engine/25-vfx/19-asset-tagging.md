# VFX asset tagging

Every taggable VFX asset carries a free-form `tags` array so the asset browser
and the snippet library (task 8.8.0) can filter by keyword. Tags are plain
lower-case identifiers — no namespace rules enforced at load.

Reference: UE5 `NiagaraScriptBase::Tags` + `FAssetData::TagsAndValues`.

## Taggable assets

| Asset               | Field                    | File                                                        |
|---------------------|--------------------------|-------------------------------------------------------------|
| `VfxSystem`         | `tags: vector<string>`   | `engine/vfx/system/vfx_system.h`                            |
| `VfxNodeGraph`      | `tags: vector<string>`   | `engine/vfx/editor/vfx_node_graph.h`                        |
| `ParticleEmitter`   | `tags: vector<string>`   | `engine/vfx/components/particle_emitter.h`                  |

## JSON

All three loaders serialise tags as a top-level `"tags"` JSON array of strings.
Missing / empty arrays are legal (the asset is then untagged). Non-string
entries are skipped with a VFX-category warning so malformed elements do not
nullify the whole load.

```json
{
  "format":  "uzvfxsys",
  "version": 1,
  "name":    "Fireball",
  "emitters": [ ... ],
  "tags":    ["fire", "combat", "hero"]
}
```

Serialization emits the `"tags"` key only when the vector is non-empty so
legacy `.uzvfxsys` / `.uzvfxgraph` / `.uzfx` assets round-trip diff-minimal.

## Query grammar

Evaluated by `VfxAssetTagMatches(query, tags)` in
`engine/vfx/asset/vfx_asset_tags.h`. Whitespace-tokenised, disjunctive-normal
form:

| Construct     | Meaning                                                         |
|---------------|-----------------------------------------------------------------|
| `fire`        | asset must carry `fire`                                         |
| `fire magic`  | asset must carry `fire` AND `magic`                             |
| `-ambient`    | asset must NOT carry `ambient` (`-` or `!` prefix)              |
| `a OR b`      | alternative groups — either side matches (literal `OR` token)   |
| `fire -ambient OR magic` | `(fire AND NOT ambient) OR (magic)`                  |
| `""`          | empty query matches every asset                                 |

Matching is case-insensitive on both query tokens and asset tags. Unknown
prefixes fall through as positive matches so typing-in-progress stays
permissive (e.g. the partial query `fire ` matches the same set as `fire`).

## Evaluation cost

The evaluator is linear in `tokens * groups * tags`. For an asset browser
with O(1000) assets, O(10) tags per asset, and O(10) query tokens, total
work is ~100k string comparisons per keystroke — well within interactive
budget. No precomputed index is required.

## Tests

`runtime/tests/engine/vfx/test_vfx_asset_tags.cpp` covers:

- empty query → everything matches,
- single positive term (case-insensitive),
- multi-term AND,
- `-` and `!` negation prefixes,
- `OR` alternative groups,
- compound `A -B OR C` expressions,
- malformed `-` / `!` tokens (no-op),
- stray trailing `OR` (no-op),
- JSON round-trip on all three asset types.
