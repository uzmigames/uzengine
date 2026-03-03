## 1. Implementation

### 1a. ECS Component

- [ ] 1.1 Add `CTags { std::bitset<64> bits; }` struct to `src/engine/ecs/components.h`

### 1b. TagRegistry

- [ ] 1.2 Create `src/engine/scene/tag_registry.h` — class definition with `tag_to_bit_` map, `bit_to_tag_` array[64], `next_bit_` counter
- [ ] 1.3 Implement `TagRegistry::get_or_register(std::string_view tag)` → `uint8_t` — registers new tag if not present; returns 255 + emits log warning if registry is full
- [ ] 1.4 Implement `TagRegistry::find(std::string_view tag) const` → `uint8_t` — returns 255 if tag not registered (not an error)
- [ ] 1.5 Implement `TagRegistry::name_of(uint8_t bit) const` → `std::string_view`
- [ ] 1.6 Implement `TagRegistry::count() const` → `uint8_t`
- [ ] 1.7 Implement `TagRegistry::clear()` — resets all state; called from SceneManager on scene unload

### 1c. SceneParser Integration

- [ ] 1.8 Parse `tags = [...]` field in `src/engine/scene/scene_parser.cpp` — accept both `["a","b"]` and `a,b` formats
- [ ] 1.9 For each tag string: call `TagRegistry::get_or_register`, set corresponding bit in entity's `CTags` component
- [ ] 1.10 Emit warning (not error) if registry is full during parse: `[tags] registry full, tag 'X' ignored on node 'Y'`

### 1d. SceneManager Query API

- [ ] 1.11 Add `find_by_tag(std::string_view tag)` → `Array<NodeHandle>` to `SceneManager` — scan all entities with `CTags`, filter by bit
- [ ] 1.12 Add `for_each_with_tag(std::string_view tag, Fn&&)` → iterate in-place, no allocation; preferred for hot paths
- [ ] 1.13 Add `has_any_with_tag(std::string_view tag)` → `bool` — short-circuit scan
- [ ] 1.14 Add `node_has_tag(NodeHandle, std::string_view tag)` → `bool`
- [ ] 1.15 Add `node_add_tag(NodeHandle, std::string_view tag)` — enqueues `SceneCommand::SET_TAGS` (additive)
- [ ] 1.16 Add `node_remove_tag(NodeHandle, std::string_view tag)` — enqueues `SceneCommand::SET_TAGS` (subtractive)
- [ ] 1.17 Add `node_get_tags(NodeHandle)` → `Array<std::string_view>` — returns all active tag names for node
- [ ] 1.18 Add `SceneCommand::SET_TAGS` variant to `src/engine/ecs/scene_commands.h` — carries `NodeHandle`, `CTags new_bits`; applied at frame start

### 1e. Lua Binding

- [ ] 1.19 Register `ctx.scene:find_by_tag(tag)` in `lua_binding.cpp` — calls `SceneManager::find_by_tag`, returns Lua array of `NodeRef` userdata
- [ ] 1.20 Register `ctx.scene:has_any_with_tag(tag)` → boolean
- [ ] 1.21 Register `node:has_tag(tag)` on `NodeRef` metatable → boolean
- [ ] 1.22 Register `node:add_tag(tag)` on `NodeRef` metatable — enqueues SET_TAGS; warns if registry full
- [ ] 1.23 Register `node:remove_tag(tag)` on `NodeRef` metatable — enqueues SET_TAGS; no-op if tag not present
- [ ] 1.24 Register `node:get_tags()` on `NodeRef` metatable → Lua array of strings

### 1f. IPC Handlers

- [ ] 1.25 Add `scene.findByTag` handler to `src/runtime/ipc/handlers/scene_handlers.cpp` — args: `{tag: string}`, returns `{nodes: [{id, name}]}`
- [ ] 1.26 Add `scene.getNodeTags` handler — args: `{node_id: number}`, returns `{tags: string[]}`
- [ ] 1.27 Add `scene.setNodeTags` handler — args: `{node_id: number, tags: string[]}`, enqueues SET_TAGS replacing all tags; returns `{ok: bool}`

## 2. Testing

- [ ] 2.1 Unit test: `TagRegistry::get_or_register` — register 64 unique tags; verify each returns a distinct bit index 0..63
- [ ] 2.2 Unit test: `TagRegistry::get_or_register` at capacity — 65th tag returns 255, warning is logged, no crash
- [ ] 2.3 Unit test: `TagRegistry::find` — returns correct bit for registered tag, 255 for unknown tag
- [ ] 2.4 Unit test: `TagRegistry::clear` — all previously registered tags are gone, next_bit_ resets to 0
- [ ] 2.5 Unit test: `SceneManager::find_by_tag` — scene with 5 nodes, 3 tagged "enemy"; verify returns exactly those 3 handles
- [ ] 2.6 Unit test: `find_by_tag` with unregistered tag — returns empty array, no crash
- [ ] 2.7 Unit test: `node_has_tag` — verify true for tagged node, false for untagged node
- [ ] 2.8 Unit test: `node_add_tag` enqueues SET_TAGS; after command applied, `node_has_tag` returns true for new tag
- [ ] 2.9 Unit test: `node_remove_tag` enqueues SET_TAGS; after command applied, `node_has_tag` returns false
- [ ] 2.10 Unit test: `node_get_tags` — verify returns all and only the active tags for a node
- [ ] 2.11 Unit test: `.uzscene` parse with `tags = ["enemy", "damageable"]` — verify node has both bits set in `CTags`
- [ ] 2.12 Unit test: `.uzscene` parse with no `tags` field — verify `CTags` component absent or `bits` is all-zero
- [ ] 2.13 Unit test: `.uzscene` parse with duplicate tag in list — verify bit set only once (no double-registration)
- [ ] 2.14 Lua unit test: `ctx.scene:find_by_tag("enemy")` — scene with 3 enemy nodes; verify Lua array length is 3, each element is valid NodeRef
- [ ] 2.15 Lua unit test: `node:has_tag("enemy")` — verify true/false correctly
- [ ] 2.16 Lua unit test: `node:add_tag("stunned")` then next frame `node:has_tag("stunned")` — verify visible after command applies
- [ ] 2.17 Lua unit test: `node:get_tags()` — verify returns string array matching node's tags
- [ ] 2.18 Integration test: runtime `node:add_tag("boss")` during play — `ctx.scene:find_by_tag("boss")` returns the node next frame
- [ ] 2.19 Integration test: scene reload — TagRegistry cleared and rebuilt from new scene file; old tags from previous scene not present
- [ ] 2.20 Performance test: 1000-node scene, 100 nodes tagged "enemy"; `find_by_tag("enemy")` completes in < 0.1ms

## 3. Documentation

- [x] 3.1 `docs/specs/tags-groups.md` — full spec (written in planning phase)
- [ ] 3.2 `docs/specs/scene-system.md` — add `tags` field to `.uzscene` node block format table; add `SET_TAGS` to SceneCommand list
- [ ] 3.3 `docs/specs/lua-scripting.md` — add `ctx.scene:find_by_tag`, `ctx.scene:has_any_with_tag` to Scene Access table; add `node:has_tag`, `node:add_tag`, `node:remove_tag`, `node:get_tags` to NodeRef table
- [ ] 3.4 `docs/README.md` — `phase3-tags-groups` row in Rulebook Tasks table (done in planning phase)
- [ ] 3.5 Code comments in `tag_registry.h` — document 64-tag limit rationale (bitset<64> fits in one register) and safe-overflow behavior
- [ ] 3.6 Code comments in `scene_manager.h` — document that `find_by_tag` returns a snapshot (safe to modify scene during iteration via handle validation)
