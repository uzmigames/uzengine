# Proposal: phase2-data-tables

## Why

Game development heavily relies on structured, tabular data to define game entities: items, enemies, abilities, loot tables, quests, skill trees. Without a dedicated system, developers scatter this data across ad-hoc Lua tables, hardcoded constants, or JSON files with no schema enforcement, hot-reload, or editor tooling.

Unreal Engine's `UDataTable` is one of the most-used features by game teams for exactly this reason: it provides a visual, type-safe, AI-friendly structure for game content data that is authored separately from code. UzEngine needs an equivalent that fits its AI-native, plain-text, hot-reload philosophy.

The `.uztable` format extends the existing INI-like data-driven config infrastructure with a schema layer and typed rows. The VSCode Table Editor panel makes authoring visual. The Lua API makes consumption ergonomic. The MCP tool `table_generate_rows` lets AI agents populate entire databases from a single prompt.

## What Changes

- **`.uztable` format** — new file format: `[table]` header, `[schema]` column definitions with type system (int, float, string, bool, vec3, color, resource, enum, row_ref, string[], object[]), `[row.<id>]` typed data rows
- **`src/game/data_table_manager.h/cpp`** — `DataTableManager` class: parse `.uztable` INI files, validate schema on load, O(1) row lookup by ID, hot-reload dispatch, Lua callback notification
- **`src/game/data_table_parser.h/cpp`** — INI → `DataTable` deserialization (reuses existing INI tokenizer from data-driven-config)
- **`src/game/data_value.h`** — `DataValue` variant type (int, float, string, bool, vec3, color, resource, array, object_array)
- **`src/game/data_table_query.h/cpp`** — `QueryPredicate` evaluation for `ctx.tables:query()`
- **`src/runtime/ipc/handlers/table_handlers.cpp`** — IPC handlers for editor: `tables.list`, `tables.get_schema`, `tables.get_rows`, `tables.get_row`, `tables.set_field`, `tables.add_row`, `tables.delete_row`, `tables.validate` — all under `#ifdef UZ_EDITOR`
- **`src/game/lua_binding.cpp`** — register `ctx.tables` table with `get_row()`, `rows()`, `query()`, `register()`, `roll_loot()`, `on_reload()` — shorthand indexing `ctx.tables.<name>[row_id]`
- **`FileWatcher`** — register `.uztable` extension for hot-reload dispatch to `DataTableManager::reload()`
- **`vscode-uzengine/src/table-editor-panel.ts`** — VSCode webview panel: column header row, data grid, inline cell editing with type enforcement, Add/Delete/Duplicate Row, column sorting, row filter bar, validation overlay (red border + tooltip), CSV import/export, AI "Generate rows" button
- **`phase2-ai-native-features`** — add `table_generate_rows` and `table_query` MCP tools

## Impact

- Affected specs: `docs/specs/gameplay/data-tables.md` (new)
- Affected modules: `game/` (DataTableManager, DataValue, query), `runtime/ipc/` (table handlers), `vscode-uzengine/` (Table Editor panel), `phase2-ai-native-features` (MCP tools)
- Depends on: `phase1-scene-system`, `phase1-lua-scripting`, `phase1-editor-panels`, `phase1-undo-redo`, `phase2-ai-native-features`
- Breaking change: NO — purely additive new system and file format
- User benefit: Developers define game content (items, enemies, abilities) in a visual, type-safe grid editor inside VSCode. AI agents populate entire databases with one prompt via `table_generate_rows`. Hot-reload propagates balance changes to the running game in <200ms without recompile. `row_ref` foreign keys connect tables (loot table → item table) with validation.
