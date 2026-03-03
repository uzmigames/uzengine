# Proposal: phase3-tags-groups

## Why

Phase 3 gameplay systems (physics, combat, AI) need to find groups of nodes by category — "all enemies", "all pickups", "all damageable objects" — without hardcoding node names or maintaining manual lists in scripts. Without tags, Lua scripts must either walk the entire scene tree (O(n) with string comparisons on every node) or maintain fragile arrays that break when nodes are added or removed. A tag system backed by a per-node `std::bitset<64>` component gives O(n) queries with single-instruction membership tests, decouples categorization from naming, and matches the mental model of Godot Groups — the most intuitive scene organization primitive for gameplay programmers and AI agents.

## What Changes

- **`src/engine/ecs/components.h`** — Add `CTags { std::bitset<64> bits }` ECS component
- **`src/engine/scene/tag_registry.h/cpp`** — New `TagRegistry` class: maps tag string → bit index (0..63), max 64 unique tags per scene; `get_or_register`, `find`, `name_of`, `count` methods
- **`src/engine/scene/scene_manager.h/cpp`** — Add tag query API: `find_by_tag`, `for_each_with_tag`, `has_any_with_tag`, `node_has_tag`, `node_add_tag`, `node_remove_tag`, `node_get_tags`
- **`src/engine/scene/scene_parser.cpp`** — Parse `tags = [...]` field in `.uzscene` node blocks; register tags in `TagRegistry`, set `CTags.bits` bits
- **`src/engine/ecs/scene_commands.h`** — Add `SET_TAGS` command variant for runtime tag mutation
- **`src/engine/scripting/lua_binding.h/cpp`** — Register `ctx.scene:find_by_tag`, `ctx.scene:has_any_with_tag`, `node:has_tag`, `node:add_tag`, `node:remove_tag`, `node:get_tags` on respective metatables
- **`src/runtime/ipc/handlers/scene_handlers.cpp`** — Add `scene.findByTag`, `scene.getNodeTags`, `scene.setNodeTags` IPC handlers
- **`docs/specs/tags-groups.md`** — Full spec (already written in planning phase)

## Impact

- Affected specs: `docs/specs/tags-groups.md` (new), `docs/specs/scene-system.md` (tags field in .uzscene format, SceneCommand::SET_TAGS), `docs/specs/lua-scripting.md` (ctx.scene and NodeRef tag methods)
- Affected modules: Engine ECS (`CTags` component), Engine scene (`TagRegistry`, `SceneManager`, `SceneParser`), Engine scripting (`lua_binding`), Runtime IPC (`scene_handlers`)
- Breaking change: NO — `tags` field in `.uzscene` is optional; existing scenes without it parse identically; all existing C++/Lua/IPC APIs unchanged
- User benefit: Lua scripts use `ctx.scene:find_by_tag("enemy")` instead of hand-maintained lists; AI agents can generate gameplay queries against semantic categories rather than node names; editor can display tag membership in Inspector; IPC enables AI to query scene topology by tag during live play sessions
