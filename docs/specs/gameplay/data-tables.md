# Spec: Data Tables

**Status**: Planned
**Phase**: 2
**Task**: `phase2-data-tables`
**Depends on**: `phase1-scene-system`, `phase1-lua-scripting`, `phase1-editor-panels`, `phase2-ai-native-features`

---

## Overview

Data Tables provide a structured, typed, row-based system for defining game data that is authored by developers and consumed at runtime. Inspired by Unreal Engine's `UDataTable`, UzEngine's implementation is designed to be:

- **AI-friendly** — plain-text `.uztable` files that LLMs can read, generate, and modify
- **Editor-visual** — a Table Editor panel in VSCode shows rows and columns with inline editing
- **Hot-reloadable** — changes to `.uztable` files reflect in running game in ~100ms
- **Lua-accessible** — simple, ergonomic API: `ctx.tables:get("items", "sword").damage`
- **Type-safe** — columns have declared types; the engine validates on load

### Representative Use Cases

| Table | Row Key | Columns |
|-------|---------|---------|
| `items.uztable` | `sword_iron` | damage, weight, durability, slot, icon, mesh, price |
| `enemies.uztable` | `goblin` | health, attack, defense, speed, drops, xp_reward |
| `abilities.uztable` | `fireball` | mana_cost, cooldown, range, damage, vfx, sfx |
| `loot_tables.uztable` | `chest_common` | entries (array of {item_id, weight, quantity_min, quantity_max}) |
| `dialogue_events.uztable` | `npc_greet_01` | speaker, text_key, audio, conditions, next_event |
| `quests.uztable` | `find_the_key` | title_key, desc_key, objectives (array), rewards (array) |
| `skill_tree.uztable` | `slash` | class, level_req, cost, next_skills (array), icon |

---

## File Format: `.uztable`

### Structure

A `.uztable` file has two sections: a `[schema]` block that defines column types, and data rows as named `[row.<id>]` sections.

```ini
; items.uztable
[table]
name    = "Items"
version = 1

[schema]
id          = string   ; primary key (implicit, from section name)
display_name = string
damage      = int
weight      = float
durability  = int
slot        = enum(head, chest, legs, hands, feet, weapon, offhand, trinket)
price       = int
icon        = resource
mesh        = resource
tags        = string[]

[row.sword_iron]
display_name = "Iron Sword"
damage       = 15
weight       = 2.5
durability   = 100
slot         = weapon
price        = 50
icon         = "res://textures/ui/icons/sword_iron.png"
mesh         = "res://meshes/weapons/sword_iron.glb"
tags         = ["weapon", "melee", "iron"]

[row.sword_steel]
display_name = "Steel Sword"
damage       = 25
weight       = 2.8
durability   = 150
slot         = weapon
price        = 200
icon         = "res://textures/ui/icons/sword_steel.png"
mesh         = "res://meshes/weapons/sword_steel.glb"
tags         = ["weapon", "melee", "steel"]

[row.health_potion]
display_name = "Health Potion"
damage       = 0
weight       = 0.2
durability   = 1
slot         = trinket
price        = 25
icon         = "res://textures/ui/icons/potion_health.png"
mesh         = "res://meshes/items/potion_small.glb"
tags         = ["consumable", "healing"]
```

### Nested Rows (Loot Tables)

For rows with complex nested data, use arrays of inline objects:

```ini
; loot_tables.uztable
[table]
name    = "Loot Tables"
version = 1

[schema]
entries = object[]

[row.chest_common]
entries = [
    { item = "health_potion",  weight = 50, qty_min = 1, qty_max = 3 },
    { item = "sword_iron",     weight = 25, qty_min = 1, qty_max = 1 },
    { item = "gold_coins",     weight = 100, qty_min = 10, qty_max = 50 }
]

[row.boss_dragon]
entries = [
    { item = "sword_legendary", weight = 5,  qty_min = 1, qty_max = 1 },
    { item = "dragon_scale",    weight = 80, qty_min = 3, qty_max = 8 },
    { item = "gold_coins",      weight = 100, qty_min = 200, qty_max = 500 }
]
```

### Enemy Stats Example

```ini
; enemies.uztable
[table]
name    = "Enemies"
version = 1

[schema]
display_name  = string
health        = int
attack        = int
defense       = int
speed         = float
move_range    = float
xp_reward     = int
loot_table    = row_ref(loot_tables)
behavior_tree = resource
mesh          = resource
vfx_death     = resource

[row.goblin]
display_name  = "Goblin"
health        = 30
attack        = 5
defense       = 2
speed         = 4.5
move_range    = 8.0
xp_reward     = 10
loot_table    = "chest_common"
behavior_tree = "res://ai/goblin_patrol.uzbt"
mesh          = "res://meshes/enemies/goblin.glb"
vfx_death     = "res://vfx/death_poof.uzfx"

[row.orc_warrior]
display_name  = "Orc Warrior"
health        = 80
attack        = 12
defense       = 6
speed         = 3.5
move_range    = 6.0
xp_reward     = 35
loot_table    = "boss_dragon"
behavior_tree = "res://ai/orc_warrior.uzbt"
mesh          = "res://meshes/enemies/orc_warrior.glb"
vfx_death     = "res://vfx/death_heavy.uzfx"
```

---

## Column Type System

| Type | Description | Example |
|------|-------------|---------|
| `string` | UTF-8 text | `"Iron Sword"` |
| `int` | 32-bit integer | `15` |
| `float` | 32-bit float | `2.5` |
| `bool` | Boolean | `true` / `false` |
| `vec2` | 2D vector | `[1.0, 2.0]` |
| `vec3` | 3D vector | `[0.0, 1.0, 0.0]` |
| `color` | RGBA color | `[1.0, 0.5, 0.0, 1.0]` |
| `resource` | Asset path | `"res://meshes/sword.glb"` |
| `enum(a,b,c)` | Closed value set | `weapon` |
| `row_ref(table)` | Foreign key to another table | `"chest_common"` |
| `string[]` | Array of strings | `["tag1", "tag2"]` |
| `int[]` | Array of ints | `[10, 20, 30]` |
| `float[]` | Array of floats | `[0.1, 0.5, 1.0]` |
| `object[]` | Array of inline objects | `[{key=val}, ...]` |

---

## Lua API

### Loading and Accessing Tables

```lua
-- Load a table (cached after first call)
local items = ctx.tables:get_table("res://data/items.uztable")

-- Access a specific row by ID
local sword = ctx.tables:get_row("res://data/items.uztable", "sword_iron")
print(sword.damage)     -- 15
print(sword.slot)       -- "weapon"
print(sword.tags[1])    -- "weapon"

-- Shorthand (table registered by name)
local sword = ctx.tables.items["sword_iron"]
print(sword.damage)

-- Iterate all rows
for id, row in ctx.tables:rows("res://data/items.uztable") do
    print(id, row.display_name, row.price)
end

-- Query rows by column value
local weapons = ctx.tables:query("res://data/items.uztable", {
    slot = "weapon",
    damage = { gt = 10 }
})
for _, row in ipairs(weapons) do
    print(row.id, row.damage)
end

-- Follow a row_ref (foreign key)
local enemy   = ctx.tables.enemies["goblin"]
local loot    = ctx.tables.loot_tables[enemy.loot_table]
-- Roll a random loot entry
local entry   = ctx.tables:roll_loot(loot.entries)
print(entry.item, entry.qty_min, entry.qty_max)
```

### Registration (optional shorthand)

```lua
-- In game init — register tables by short name
ctx.tables:register("items",      "res://data/items.uztable")
ctx.tables:register("enemies",    "res://data/enemies.uztable")
ctx.tables:register("loot_tables","res://data/loot_tables.uztable")

-- After registration, shorthand works
local row = ctx.tables.items["sword_iron"]
```

### Hot-Reload

```lua
ctx.tables:on_reload("res://data/items.uztable", function(table)
    -- Called when the file changes on disk
    -- Re-bind references, refresh UI, etc.
    ctx:log("Items table reloaded: " .. #table .. " rows")
end)
```

---

## DataTableManager (C++ / game.dll)

```cpp
// src/game/data_table_manager.h

struct DataTableColumn {
    std::string name;
    ColumnType  type;        // INT, FLOAT, STRING, BOOL, RESOURCE, ENUM, ROW_REF, ARRAY, OBJECT_ARRAY
    std::string enum_values; // comma-separated for ENUM type
    std::string ref_table;   // for ROW_REF type
    bool        required = false;
    std::string default_value;
};

struct DataTableRow {
    std::string              id;
    robin_hood::unordered_map<std::string, DataValue> fields;
};

struct DataTable {
    std::string                   path;
    std::string                   name;
    uint32_t                      version;
    std::vector<DataTableColumn>  schema;
    std::vector<DataTableRow>     rows;
    // Index for O(1) row lookup
    robin_hood::unordered_map<std::string, size_t> row_index;
};

class DataTableManager {
public:
    // Load from disk, parse, validate, cache
    const DataTable* load(const char* path);

    // Reload hot-changed file; notifies registered Lua callbacks
    void             reload(const char* path);

    // Row access
    const DataTableRow* get_row(const char* path, const char* row_id);

    // Schema validation result
    struct ValidationResult {
        bool ok;
        std::vector<std::string> errors; // "row.sword: 'damage' expected int, got string"
    };
    ValidationResult validate(const DataTable& table);

    // Query: simple predicate filter (returns indices)
    std::vector<size_t> query(const char* path, const QueryPredicate& pred);

private:
    robin_hood::unordered_map<std::string, DataTable> cache_;
    robin_hood::unordered_map<std::string, std::vector<LuaCallback>> reload_callbacks_;
};
```

### IPC Handlers (Editor Only)

```
tables.list          { "path": "res://data/" }
                     → [{ path, name, row_count }]

tables.get_schema    { "path": "..." }
                     → { columns: [{ name, type, ... }] }

tables.get_rows      { "path": "...", "offset": 0, "limit": 100 }
                     → { rows: [{ id, ...fields }], total: N }

tables.get_row       { "path": "...", "row_id": "sword_iron" }
                     → { id, ...fields }

tables.set_field     { "path": "...", "row_id": "sword_iron", "column": "damage", "value": 20 }
                     → { ok: true }

tables.add_row       { "path": "...", "row_id": "sword_new", "fields": {...} }
                     → { ok: true }

tables.delete_row    { "path": "...", "row_id": "sword_iron" }
                     → { ok: true }

tables.validate      { "path": "..." }
                     → { ok: bool, errors: [...] }
```

---

## Table Editor Panel (VSCode)

A dedicated `DataTable Editor` panel in the VSCode extension, accessible from the Asset Browser.

### Layout

```
┌─────────────────────────────────────────────────────────────────────┐
│  📊 items.uztable                          [+ Add Row] [Validate]   │
├──────────────┬───────────────┬────────┬──────────┬───────┬─────────┤
│  id          │ display_name  │ damage │ weight   │ slot  │ price   │
├──────────────┼───────────────┼────────┼──────────┼───────┼─────────┤
│ sword_iron   │ Iron Sword    │ 15     │ 2.5      │weapon │ 50      │
│ sword_steel  │ Steel Sword   │ 25     │ 2.8      │weapon │ 200     │
│ health_potion│ Health Potion │ 0      │ 0.2      │trinket│ 25      │
└──────────────┴───────────────┴────────┴──────────┴───────┴─────────┘
  [< Prev] Page 1/1 [Next >]                      Filter: [          ]
```

### Features

- **Inline editing** — click any cell to edit; types enforced (number fields reject non-numeric input)
- **Enum dropdowns** — enum columns show a dropdown with valid values
- **Resource pickers** — resource columns open the Asset Browser picker
- **Row operations** — Add Row (auto-generates schema-compliant empty row), Duplicate Row, Delete Row
- **Column sorting** — click header to sort by that column
- **Filtering** — filter bar searches across all string columns
- **Validation overlay** — invalid cells show red border with tooltip error
- **Undo/redo** — all edits go through the UndoStack (integrates with `phase1-undo-redo`)
- **Hot-reload sync** — file changes from disk update the table view automatically
- **CSV import/export** — import rows from CSV; export full table to CSV for spreadsheet editing
- **AI assist** — "Generate rows" button opens AI prompt (integrates with `phase2-ai-native-features`)

### Opening the Editor

- Double-click a `.uztable` file in the Asset Browser → opens Table Editor
- Right-click → "Open in Table Editor"
- Or opens as split view alongside the raw `.uztable` file

---

## AI Integration

Data Tables are a primary target for AI-assisted content generation.

### MCP Tool: `table_generate_rows`

```json
{
  "tool": "table_generate_rows",
  "table_path": "res://data/enemies.uztable",
  "prompt": "Generate 5 fantasy enemies for a forest dungeon. Include a basic goblin scout, an aggressive troll, a sneaky rogue elf, a large bear guardian, and a corrupted dryad boss.",
  "count": 5
}
```

The engine provides the existing schema and rows as context; the AI generates new rows conforming to the schema. Generated rows are previewed in the editor before being written to disk.

### MCP Tool: `table_query`

Allows AI agents to query game data:

```json
{
  "tool": "table_query",
  "table_path": "res://data/items.uztable",
  "filter": { "slot": "weapon", "damage": { "gt": 20 } },
  "sort_by": "damage",
  "sort_dir": "desc"
}
```

---

## CSV Import / Export

`.uztable` files can be round-tripped through CSV for bulk editing in spreadsheet tools.

### CSV Export

```
id,display_name,damage,weight,slot,price
sword_iron,Iron Sword,15,2.5,weapon,50
sword_steel,Steel Sword,25,2.8,weapon,200
health_potion,Health Potion,0,0.2,trinket,25
```

### Import Rules

- First row must match column names defined in `[schema]`
- Import merges: existing rows with matching ID are updated; new IDs are added
- `row_ref` and `resource` columns import as plain strings (no validation at import time)
- Import triggers full table re-validation after merge

---

## Validation Rules

The engine validates `.uztable` files on load and on every save. Errors are surfaced as VSCode diagnostics on the file line.

| Rule | Example Error |
|------|---------------|
| Required column missing | `row.goblin: required column 'health' is missing` |
| Type mismatch | `row.sword_iron: 'damage' expected int, got "fifteen"` |
| Enum value invalid | `row.sword_iron: 'slot' value "fist" not in enum(head,chest,...,weapon)` |
| Row ref not found | `row.goblin: loot_table "chest_rare" not found in loot_tables.uztable` |
| Duplicate row ID | `row id "sword_iron" defined twice` |
| Resource not found | `row.goblin: mesh "res://meshes/gob.glb" does not exist` *(warning only)* |

---

## Integration Points

| System | Integration |
|--------|-------------|
| `DataTableManager` | Parses, validates, caches, hot-reloads all `.uztable` files |
| `DataDrivenConfig` (phase1) | Shared INI parser — `.uztable` reuses the same parser with schema layer on top |
| `ResourceManager` | Asset registry tracks `.uztable` as assets; `res://` paths work normally |
| `LuaVM` | `ctx.tables` API registered in `lua_binding.cpp` |
| `FileWatcher` | `.uztable` extension registered for hot-reload dispatch |
| `UndoStack` (phase1-undo-redo) | `tables.set_field` / `tables.add_row` / `tables.delete_row` all create `EditorCommand` entries |
| `AssetBrowser` (phase1-asset-browser) | `.uztable` files shown with table icon; double-click opens Table Editor panel |
| `AI Native Features` (phase2) | `table_generate_rows` and `table_query` MCP tools |
| `Save System` (phase3) | Row IDs can be serialized in save files as stable foreign keys |
| `Localization` (phase3) | String columns can hold localization keys; `ctx.locale:t(row.title_key)` |

---

## File Layout

```
src/game/
├── data_table_manager.h/cpp   — DataTableManager: parse, validate, cache, hot-reload
├── data_table_parser.h/cpp    — INI → DataTable deserialization
├── data_value.h               — DataValue variant (int/float/string/bool/vec3/array/object)
└── data_table_query.h/cpp     — QueryPredicate evaluation

src/runtime/ipc/handlers/
└── table_handlers.cpp         — IPC handlers (editor-only, #ifdef UZ_EDITOR)

vscode-uzengine/src/
└── table-editor-panel.ts      — VSCode webview panel for the Table Editor UI
```

---

## Definition of Done

- [ ] `.uztable` INI format parses correctly for all column types
- [ ] `DataTableManager` validates schema on load and reports typed errors
- [ ] `ctx.tables:get_row()`, `ctx.tables:rows()`, `ctx.tables:query()` work in Lua
- [ ] `row_ref` foreign keys resolve across tables
- [ ] Hot-reload updates Lua-visible data within 200ms of file save
- [ ] VSCode Table Editor panel opens from Asset Browser double-click
- [ ] Inline cell editing writes changes back to `.uztable` file via IPC
- [ ] Add Row / Delete Row / Duplicate Row operations work with undo/redo
- [ ] CSV export generates valid CSV matching schema column order
- [ ] CSV import merges rows and re-validates
- [ ] Validation errors show as VSCode file diagnostics (red squiggle + tooltip)
- [ ] `table_generate_rows` MCP tool generates schema-conforming rows
- [ ] All Lua API functions documented in `docs/guides/data-tables-guide.md`
