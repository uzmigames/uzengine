## 1. Data Format & Core Types

- [ ] 1.1 Design and document `.uztable` INI format — `[table]` header, `[schema]` column definitions, `[row.<id>]` sections with typed values; support inline object arrays `[{key=val, ...}, ...]`
- [ ] 1.2 Create `src/game/data_value.h` — `DataValue` variant: `int32_t`, `float`, `std::string`, `bool`, `glm::vec3`, `glm::vec4` (color), resource path, `std::vector<DataValue>`, `object_array` (vector of flat string maps)
- [ ] 1.3 Create `src/game/data_table_parser.h/cpp` — tokenize `.uztable` INI files: read `[table]` header, `[schema]` column definitions, `[row.*]` sections; map keys to `DataValue` by column type declaration; handle string[], int[], float[], object[] literals
- [ ] 1.4 Implement `ColumnType` enum and `DataTableColumn` struct: name, ColumnType, enum_values (for ENUM), ref_table (for ROW_REF), required flag, default_value

## 2. DataTableManager

- [ ] 2.1 Create `src/game/data_table_manager.h/cpp` — `DataTableManager` singleton: `load(path)` → parse + validate + cache into `robin_hood::unordered_map<string, DataTable>`
- [ ] 2.2 Implement `get_row(path, row_id)` — O(1) row lookup via `row_index` map; return `nullptr` if not found (no crash)
- [ ] 2.3 Implement `reload(path)` — re-parse file, re-validate, swap cache entry, call registered Lua callbacks; total path from file-changed event to Lua callback <200ms
- [ ] 2.4 Register `.uztable` extension in `FileWatcher` dispatch table → calls `DataTableManager::reload()`
- [ ] 2.5 Implement `ValidationResult validate(const DataTable&)` — check required fields present, type correctness, enum values valid, row_ref targets exist (cross-table), duplicate row IDs

## 3. Query Engine

- [ ] 3.1 Create `src/game/data_table_query.h/cpp` — `QueryPredicate` struct: column name, operator (eq, ne, gt, gte, lt, lte, contains), value
- [ ] 3.2 Implement `DataTableManager::query(path, predicates)` — linear scan returning `vector<const DataTableRow*>` of matching rows; support multi-predicate AND

## 4. Lua Bindings

- [ ] 4.1 Register `ctx.tables` in `lua_binding.cpp` — Lua table with metatable `__index` so `ctx.tables.<registered_name>[row_id]` works as shorthand
- [ ] 4.2 Implement `ctx.tables:get_row(path, row_id)` → Lua table with all row fields; nested object arrays become Lua array-of-tables
- [ ] 4.3 Implement `ctx.tables:rows(path)` → Lua iterator (pairs-compatible) over all rows
- [ ] 4.4 Implement `ctx.tables:query(path, predicate_table)` → Lua array of row tables matching predicates
- [ ] 4.5 Implement `ctx.tables:register(name, path)` — register short name for path shorthand indexing
- [ ] 4.6 Implement `ctx.tables:on_reload(path, callback)` — register Lua function called after hot-reload with updated table
- [ ] 4.7 Implement `ctx.tables:roll_loot(entries_array)` — weighted random selection: sum weights, pick random, return selected entry table; utility for loot tables

## 5. IPC Handlers (Editor Only)

- [ ] 5.1 Create `src/runtime/ipc/handlers/table_handlers.cpp` under `#ifdef UZ_EDITOR`:
  - `tables.list { path }` → `[{ path, name, row_count }]` — list all .uztable files under a directory
  - `tables.get_schema { path }` → `{ columns: [{ name, type, enum_values, required }] }`
  - `tables.get_rows { path, offset, limit }` → `{ rows: [...], total }`
  - `tables.get_row { path, row_id }` → row fields object
  - `tables.set_field { path, row_id, column, value }` → `{ ok, error? }` — writes back to `.uztable` file
  - `tables.add_row { path, row_id, fields }` → `{ ok, error? }` — appends `[row.<id>]` section to file
  - `tables.delete_row { path, row_id }` → `{ ok }` — removes `[row.<id>]` section from file
  - `tables.validate { path }` → `{ ok, errors: [{ row_id, column, message }] }`
- [ ] 5.2 Register all handlers in `IpcRegistry::setup()` under `#ifdef UZ_EDITOR`

## 6. Table Editor Panel (VSCode)

- [ ] 6.1 Create `vscode-uzengine/src/table-editor-panel.ts` — VSCode `WebviewPanel` singleton per `.uztable` file; opens via Asset Browser double-click or right-click context menu
- [ ] 6.2 Implement column header row: column names with type badge (int, float, string, enum, resource, row_ref)
- [ ] 6.3 Implement data grid: one row per `[row.*]` entry; cells render value with appropriate input widget (text input, number input, enum dropdown, resource path field)
- [ ] 6.4 Implement inline cell editing: on blur / Enter → call `tables.set_field` IPC → update file → show success or error state; number fields reject non-numeric input
- [ ] 6.5 Implement Add Row: prompt for row ID → validate unique → `tables.add_row` IPC → refresh grid
- [ ] 6.6 Implement Delete Row: confirm dialog → `tables.delete_row` IPC → remove from grid
- [ ] 6.7 Implement Duplicate Row: copy all fields to new row with auto-suffix (`_copy`)
- [ ] 6.8 Implement column sorting: click header → toggle asc/desc → sort grid client-side
- [ ] 6.9 Implement filter bar: text input → filter rows where any string column contains value
- [ ] 6.10 Implement validation overlay: call `tables.validate` IPC on open and after each edit → highlight invalid cells with red border + tooltip message
- [ ] 6.11 Wire undo/redo: `set_field`, `add_row`, `delete_row` calls create `EditorCommand` entries via `editor.push_undo_command` IPC (integrates with `phase1-undo-redo`)
- [ ] 6.12 Implement hot-reload sync: listen for `tables.reloaded` IPC event → re-fetch rows and refresh grid

## 7. CSV Import / Export

- [ ] 7.1 Implement CSV export: "Export CSV" button → call `tables.get_rows` for all rows → generate CSV with header from schema column order → trigger VSCode save dialog
- [ ] 7.2 Implement CSV import: "Import CSV" button → VSCode file picker → parse CSV → validate header columns match schema → call `tables.add_row` / `tables.set_field` for each row → show import summary (N rows added, N updated, N errors)

## 8. AI Integration

- [ ] 8.1 Add `table_generate_rows` MCP tool to `phase2-ai-native-features` spec and implementation: accept table_path + prompt + count → fetch schema → send to LLM with schema context → parse response → preview in Table Editor → write on user confirmation
- [ ] 8.2 Add `table_query` MCP tool: accept table_path + filter + sort → call `DataTableManager::query()` → return JSON rows

## 9. Testing

- [ ] 9.1 Unit tests: `data_table_parser` — valid INI parses all 14 column types correctly
- [ ] 9.2 Unit tests: `DataTableManager::validate()` — required field missing, type mismatch, enum value invalid, duplicate row ID, row_ref not found
- [ ] 9.3 Unit tests: `DataTableManager::get_row()` — O(1) lookup, missing row returns nullptr
- [ ] 9.4 Unit tests: `DataTableQuery` — eq/ne/gt/gte/lt/lte/contains predicates; multi-predicate AND
- [ ] 9.5 Unit tests: Lua API — `get_row`, `rows` iterator, `query`, `roll_loot` weighted distribution
- [ ] 9.6 Integration test: modify `.uztable` file → hot-reload fires within 200ms → Lua callback receives updated data
- [ ] 9.7 Integration test: `tables.set_field` IPC → file written → re-parsed correctly
- [ ] 9.8 Integration test: CSV export → re-import → row data matches original

## 10. Documentation

- [x] 10.1 `docs/specs/gameplay/data-tables.md` — full spec (done during planning)
- [ ] 10.2 `docs/guides/data-tables-guide.md` — practical guide: creating tables, Lua API usage, loot table patterns, enemy DB pattern, CSV workflow, AI generation
- [ ] 10.3 Update `docs/README.md` — add spec and task to index tables
- [ ] 10.4 Update `docs/engine/ecs-and-scene.md` or `docs/engine/scripting-system.md` — mention DataTableManager as game data layer
