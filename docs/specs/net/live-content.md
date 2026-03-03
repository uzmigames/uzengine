# Live Content Pipeline Specification

**Status**: Draft
**Phase**: 4
**Scope**: Everything that allows the game world to be expanded, rebalanced, and populated without rebuilding or redistributing the client binary.

---

## 1. Problem Statement (from production experience)

The biggest operational friction in a live MMORPG is the **rebuild/redistribution cycle**:

- Every new NPC type, spawn point, item, skill, or zone configuration that is baked into the client binary requires a full build, QA pass, and Steam update.
- A Steam update forces all players to re-download content before they can play.
- Content creators (designers, GMs) are blocked by the engineering release cycle.
- A significant portion of players never reach endgame — shipping endgame assets in the initial client is wasted bandwidth.

The UzEngine live content system eliminates this cycle for the vast majority of day-to-day content work. The principle: **the client binary changes only when engine code or compiled shaders change. All game data is live.**

---

## 2. What Is "Live" vs "Compiled"

| Category | Live (no rebuild needed) | Compiled (requires rebuild) |
|---|---|---|
| NPC definitions | Yes — API-served JSON/msgpack | No |
| Monster stats / AI config | Yes | No |
| Item definitions, stats, recipes | Yes | No |
| Skill / spell definitions | Yes (see spell-system.md) | No |
| Quest scripts | Yes — Lua, hot-reloaded from CDN | No |
| Zone layouts, spawn tables | Yes | No |
| World spawn points | Yes — placed by GMs in-game | No |
| UI icons (items, skills) | Yes — lazy-loaded from CDN | No |
| Localisation strings | Yes — CSV from CDN | No |
| Shader code | No — compiled to SPIR-V/DXIL | Yes |
| Engine C code | No | Yes |
| Physics collision meshes | No — part of client pak | Yes |
| Character/NPC skeletal meshes | No (base set) / Yes (DLC via pak) | Patch |

---

## 3. Content API Server

A lightweight HTTP/2 REST API (separate process from `uz_server` game shards) serves all game data. The game server and client both consume it.

### 3.1 Endpoints

```
GET  /v1/items/{id}              — single item definition
GET  /v1/items?page=N&limit=100 — paginated item catalogue
GET  /v1/monsters/{id}          — monster definition + AI config
GET  /v1/monsters/              — full catalogue
GET  /v1/npcs/{id}              — NPC definition
GET  /v1/skills/{id}            — skill/spell definition
GET  /v1/zones/{id}             — zone metadata + spawn tables
GET  /v1/zones/{id}/spawns      — spawn point list for zone
GET  /v1/quests/{id}            — quest definition + stages
GET  /v1/config/server          — global server tuning parameters
GET  /v1/config/client          — client-side feature flags

POST /v1/admin/spawns           — create/update spawn point (admin only)
POST /v1/admin/items            — create/update item definition
POST /v1/admin/skills           — create/update skill definition
POST /v1/admin/monsters         — create/update monster definition
POST /v1/admin/zones/{id}/reload — trigger server hot-reload for zone
```

All responses: `Content-Type: application/msgpack` (compact binary) or `application/json` (debug/dashboard). Clients request msgpack via `Accept` header.

### 3.2 Caching Strategy

```
Client-side cache:
  - ETag + If-None-Match on all GET requests
  - Max-Age per content type:
      items / monsters / skills: 3 600 s (1 hour)
      spawn tables:              300 s   (5 minutes)
      server config:             60 s
  - Local disk cache in {cache_dir}/content/ (LRU, max 512 MB)
  - In-memory hot cache: last 2 048 accessed records in a hash table

Server-side:
  - Content API fronted by read-through in-memory cache (all records at startup)
  - On POST/admin update: invalidate specific cache key, broadcast reload event
    to all connected uz_server shards via internal message bus
```

### 3.3 Startup Data Load

When `uz_server` starts:

```
1. Connect to Content API
2. Bulk-load all active catalogues into memory:
     items, monsters, npcs, skills, zones, quests
3. Build in-memory indices:
     item_by_id[uint32] → ItemDef*
     monster_by_id[uint32] → MonsterDef*
     skill_by_id[uint32] → SkillDef*
     zone_spawns[zone_id] → SpawnPoint[]
4. Subscribe to Content API event stream (SSE or WebSocket)
   for live updates (see §3.4)
5. Begin accepting player connections only after load completes
```

This mirrors the production pattern: all game data in memory, database is only a persistence backup.

### 3.4 Live Reload Events

The Content API pushes change events to subscribed game servers:

```json
{ "event": "content_updated", "type": "item",    "id": 1042, "version": 7 }
{ "event": "content_updated", "type": "monster",  "id": 88,   "version": 3 }
{ "event": "spawn_updated",   "zone_id": 5,       "version": 12 }
{ "event": "zone_reload",     "zone_id": 5 }
```

The game server re-fetches the changed record and updates its in-memory cache atomically (pointer swap). No server restart. No player interruption.

---

## 4. In-Memory Database Pattern

### 4.1 Rationale

Production lesson: SQLite as primary read path introduces latency and lock contention at scale. Solution: all read paths hit in-memory data structures; SQLite (or equivalent) is the **persistence backup only**, written via an async write queue.

```
Read path:   game logic → in-memory tables → O(1) hash lookup
Write path:  game logic → async queue → writer thread → SQLite
                                      → Content API cache invalidation
```

### 4.2 SpacetimeDB Consideration

[SpacetimeDB](https://github.com/clockworklabs/SpacetimeDB) is a candidate to replace the write-path SQLite backend. It promises:
- Embedded WASM game logic with direct table access (no ORM, no network hop)
- Automatic client subscription to table changes (replaces hand-written replication for persistent state)
- Claimed 1000× faster than SQLite for game workloads

**Decision deferred**: SpacetimeDB is not yet integrated. The content API and in-memory pattern described here is compatible with a future SpacetimeDB backend for persistence — the in-memory cache layer would remain; only the write-path changes.

### 4.3 Write Queue

```c
typedef struct {
    uint8_t  op;          // DB_OP_INSERT / DB_OP_UPDATE / DB_OP_DELETE
    uint8_t  table;       // TABLE_PLAYERS / TABLE_INVENTORY / TABLE_SPAWNS / …
    uint32_t record_id;
    uint8_t  data[DB_MAX_RECORD];
    uint32_t data_len;
} db_write_entry_t;

// Lock-free MPSC ring (game logic threads → writer thread)
// Capacity: 65 536 entries
// Writer thread drains at up to 10 000 writes/s; batches in 50 ms windows
```

Game logic code never blocks on a database write. If the ring fills, writes are coalesced (last-write-wins for updates; inserts are never dropped).

---

## 5. CDN Asset Lazy-Loading (Client)

### 5.1 Scope

UI assets that represent content (not engine assets) are loaded on demand from a CDN:

- Item icons (`icons/items/{item_id}.webp`)
- Skill icons (`icons/skills/{skill_id}.webp`)
- NPC portrait thumbnails (`portraits/npcs/{npc_id}.webp`)
- Quest artwork (`quests/{quest_id}/banner.webp`)
- Localisation overrides (`locale/{lang}/{key}.json`)

These are **never bundled in the Steam package**. This allows adding new items, skills, and quests with zero client update.

### 5.2 Why Not Game Mesh/Texture Assets

Lazy-loading 3D assets (meshes, textures) from CDN was evaluated and rejected:

- Streaming 3D assets introduces visible pop-in and unpredictable load times.
- Steam's content delivery pipeline (delta patches) makes it cheaper to ship updates for game assets than to maintain a private CDN for large binaries.
- Texture streaming from a private CDN at MMORPG scale has significant egress cost.
- Complexity of the streaming system (async GPU uploads, LOD transitions mid-load) far outweighs the benefit.

**Decision**: UI icons and data assets are CDN-lazy. Game world assets (meshes, textures, sounds) travel via Steam updates.

### 5.3 Client-Side Cache

```
{user_data}/cache/cdn/
  icons/items/{item_id}.webp     — cached indefinitely, invalidated by ETag
  icons/skills/{skill_id}.webp
  portraits/npcs/{npc_id}.webp
  …

Cache policy:
  - ETag-based validation on startup for all cached files
  - If ETag matches: serve from disk, no download
  - If ETag changed: download new version, replace
  - Max cache size: 2 GB (LRU eviction beyond limit)
  - Missing icon: show placeholder sprite while downloading async
```

Icons are loaded into a `VkImage`/`ID3D12Resource` texture atlas on demand, evicted when the inventory/skill panel that references them closes. Atlas slots are pooled (256×256 tiles in a 4096×4096 atlas = 256 slots).

### 5.4 Placeholder System

While an icon is being fetched:
1. A generic placeholder icon (bundled in the client pak) is shown immediately.
2. An async HTTP GET is queued.
3. On completion, the atlas slot is updated and the UI re-draws.

Zero blocking. Zero pop-in delay visible to the player (placeholder is always present).

---

## 6. Admin In-Game Commands

GMs can modify the live world without touching the dashboard or restarting the server. All admin commands are sent as authenticated RPCs on `CHANNEL_RELIABLE_ORD` from the admin client.

### 6.1 Command Categories

#### Spawn Management
```
/spawn create monster <monster_id> [count] [radius]
    — creates a persistent spawn point at admin's current position
    — count: simultaneous alive (default 1)
    — radius: random offset from anchor (default 0 = exact position)
    — saved to Content API immediately (survives server restart)

/spawn create resource <resource_id> [count] [respawn_s]
    — creates a resource node (ore, herb, etc.)

/spawn delete <spawn_uid>
    — removes spawn point, despawns alive entities

/spawn list [radius_m]
    — lists nearby spawn points with their UIDs

/spawn setcount <spawn_uid> <count>
    — updates alive count live
```

#### World / Zone
```
/zone reload <zone_id>
    — forces server to re-fetch zone config + spawn tables from Content API
    — active entities remain; new spawns use updated config

/zone setprop <zone_id> <key> <value>
    — updates a zone property (e.g. pvp_enabled, weather, music_track)
    — takes effect immediately
```

#### Content
```
/item give <player_name> <item_id> [quantity]
/item inspect <item_id>           — prints full ItemDef from memory
/monster inspect <monster_id>     — prints MonsterDef
/skill inspect <skill_id>         — prints SkillDef
```

#### Server
```
/server broadcast <message>       — global announcement to all players
/server kick <player_name> [reason]
/server ban <player_name> <duration_s> [reason]
/server reload content            — re-fetch all content catalogues
```

### 6.2 Security

- Admin commands require the connection to have `ROLE_ADMIN` flag set in the session (verified server-side against account DB).
- Admin RPCs are rate-limited: 60 commands/minute per admin.
- All admin commands are written to an immutable audit log (append-only file + Content API audit endpoint).
- The admin role is never auto-assigned; requires manual promotion in the account database.

### 6.3 Visual Placement Workflow

The intended GM workflow for populating a new zone:

1. GM enters the zone in-game with admin privileges.
2. Walks to desired spawn location.
3. `/spawn create monster 42 5 8` — 5 goblins with 8 m random scatter, saved immediately.
4. Observes the spawned entities; adjusts position/count as needed.
5. `/spawn setcount <uid> 10` — increases population without leaving the game.

No coordinate copy-pasting. No YAML editing. No server restart. The world state is visible immediately.

---

## 7. Content Data Formats

All content definitions use **msgpack** for transport and storage, with a **JSON schema** for human authoring in the admin dashboard. The schema is simple enough that non-engineers can write it.

### 7.1 ItemDef

```json
{
  "id": 1042,
  "version": 7,
  "name": "Iron Sword",
  "type": "weapon",
  "subtype": "sword_1h",
  "icon_url": "icons/items/1042.webp",
  "stats": {
    "atk_min": 10,
    "atk_max": 18,
    "crit_chance": 0.05,
    "weight": 3.2
  },
  "requirements": { "level": 5, "str": 12 },
  "rarity": "common",
  "max_stack": 1,
  "tradeable": true,
  "script": null
}
```

### 7.2 MonsterDef

```json
{
  "id": 88,
  "version": 3,
  "name": "Forest Goblin",
  "level": 8,
  "faction": "goblins",
  "mesh": "monsters/goblin_warrior",
  "stats": {
    "hp": 240, "atk_min": 14, "atk_max": 20,
    "def": 8,  "move_speed": 3.5, "aggro_range": 12.0
  },
  "ai_script": "scripts/ai/melee_aggro.lua",
  "loot_table": [
    { "item_id": 1042, "chance": 0.05, "qty_min": 1, "qty_max": 1 },
    { "item_id": 201,  "chance": 0.40, "qty_min": 1, "qty_max": 3 }
  ],
  "exp": 45,
  "respawn_s": 60
}
```

### 7.3 SpawnPoint

```json
{
  "uid": "sp_7f3a1b",
  "zone_id": 5,
  "type": "monster",
  "entity_id": 88,
  "position": { "x": 1234.5, "y": 0.0, "z": -567.8 },
  "count": 5,
  "radius": 8.0,
  "respawn_s": 60,
  "active": true,
  "created_by": "admin_gm01",
  "created_at": 1740000000
}
```

---

## 8. Quest Script Hot-Reload

Quest logic is implemented as Lua scripts served from the Content API (not bundled in the client pak). The server loads quest scripts at startup and reloads them on admin command or on content_updated event.

```
Content API:   GET /v1/quests/{id}/script  → Lua source (UTF-8)
Cache:         Server in-memory + ETag validation
Hot-reload:    /zone reload triggers re-fetch of all quest scripts for that zone
```

Quest scripts follow the same `on_start / on_update / on_complete / on_fail` lifecycle as gameplay Lua scripts (see `lua-scripting.md`), plus quest-specific hooks:

```lua
-- quests/001_first_quest.lua
return {
    on_accept = function(player, quest)
        quest:set_objective("kill_goblins", 0, 10)
        player:send_message("Hunt 10 goblins in the Forest of Shadows.")
    end,

    on_kill = function(player, quest, monster_id)
        if monster_id == 88 then  -- Forest Goblin
            quest:increment("kill_goblins")
        end
    end,

    on_complete = function(player, quest)
        player:give_exp(500)
        player:give_item(1042, 1)  -- Iron Sword
        player:send_message("Quest complete!")
    end,
}
```

Adding a new quest = write Lua + create QuestDef JSON via dashboard → POST to Content API → server loads it in < 1 s. Zero client involvement.

---

## 9. File Layout

```
src/engine/net/content/
├── content_api_client.h/.c     — HTTP/2 client for Content API
├── content_cache.h/.c          — in-memory catalogue tables + ETag cache
├── content_loader.h/.c         — bulk load on startup + live reload
└── content_formats.h           — ItemDef, MonsterDef, SpawnPoint, etc. structs

src/engine/net/admin/
├── admin_rpc.h/.c              — admin command RPC handlers
├── admin_auth.h/.c             — ROLE_ADMIN session check
└── admin_audit.h/.c            — append-only audit log

src/engine/renderer/ui/cdn/
├── cdn_icon_loader.h/.c        — async HTTP fetch → texture atlas slot
├── icon_atlas.h/.c             — 4096×4096 atlas, 256×256 tiles, LRU eviction
└── placeholder.h/.c            — fallback sprite while loading

uz_content_server/              — standalone Content API process
├── main.c
├── routes/
│   ├── items.c
│   ├── monsters.c
│   ├── skills.c
│   ├── zones.c
│   ├── quests.c
│   └── admin.c
├── db/
│   ├── sqlite_backend.c        — persistence write path
│   └── memory_tables.c         — in-memory read path
└── events/
    └── sse_broadcaster.c       — server-sent events to subscribed game shards

tests/net/content/
├── test_content_cache.c
├── test_content_loader.c
├── test_admin_rpc.c
└── test_cdn_loader.c
```

---

## 10. Definition of Done

- [ ] `uz_server` loads full content catalogue (10 000 items + 1 000 monsters) in < 5 s at startup.
- [ ] Live reload of a single item definition takes < 1 s end-to-end (dashboard save → server memory update).
- [ ] Admin `/spawn create` persists to Content API and survives server restart.
- [ ] Admin command audit log is append-only; entries cannot be modified or deleted via API.
- [ ] CDN icon loads async; placeholder shown immediately; atlas updated without frame drop.
- [ ] Quest Lua script hot-reload takes < 500 ms from `content_updated` event.
- [ ] Write queue drains 10 000 entries/s to SQLite; game logic thread never blocks.
- [ ] Content API responds to GET in < 10 ms P95 under 1 000 rps load.
- [ ] All tests pass. Coverage ≥ 95%.
