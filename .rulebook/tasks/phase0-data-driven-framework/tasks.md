## 1. Research — Survey Existing Data Formats

- [ ] 1.1 Read `docs/specs/scene-system.md` — review `.uzscene` INI-like format conventions
- [ ] 1.2 Read `docs/specs/particle-system.md` — review `.uzfx` format structure
- [ ] 1.3 Read `docs/specs/navmesh.md` — review `.uznavmesh` format
- [ ] 1.4 Read `docs/specs/terrain-system.md` — review `.uzterrain` format
- [ ] 1.5 Read `docs/specs/asset-pipeline.md` — review existing importer interface
- [ ] 1.6 Read `docs/specs/lua-scripting.md` — review how scripts access assets via `ctx`
- [ ] 1.7 Review `.uzproj` TOML format — note where TOML vs. INI is chosen and why

## 2. Design — Shared Text Format Grammar

- [ ] 2.1 Define section syntax: `[section_name]` — case-insensitive, alphanumeric + underscore
- [ ] 2.2 Define key-value syntax: `key = value` — whitespace trimmed, inline `# comment` supported
- [ ] 2.3 Define array syntax: `key[] = item` — multiple lines for same key with `[]` suffix
- [ ] 2.4 Define multi-line string syntax: `key = """..."""` — triple-quote block
- [ ] 2.5 Define type coercion rules: bool (`true/false`), int, float, string, vec3 (`x,y,z`), color (`r,g,b,a`)
- [ ] 2.6 Define include directive: `@include "path/to/other.uzevent"` — flattened at parse time
- [ ] 2.7 Define escape sequences for string values

## 3. Design — [meta] Block Convention

- [ ] 3.1 Specify required `[meta]` fields: `type` (file type tag), `version` (integer), `uuid` (RFC 4122 string)
- [ ] 3.2 Specify optional `[meta]` fields: `author`, `created`, `modified`, `tags[]`
- [ ] 3.3 Define `type` tag registry — one string per file extension (e.g., `type = uzevent`)
- [ ] 3.4 Specify version migration protocol — `IAssetImporter::migrate(version, ParsedDoc&)` callback
- [ ] 3.5 Specify UUID generation policy — tool-generated at creation; never auto-assigned at runtime

## 4. Design — File Type Registry

- [ ] 4.1 Define full file type table: extension → `type` tag → importer class → spec reference
  - `.uzevent` → `uzevent` → `AudioEventImporter` → `audio-event-system.md`
  - `.uzphysmat` → `uzphysmat` → `PhysMaterialImporter` → `physics-materials.md`
  - `.uzentity` → `uzentity` → `EntityTemplateImporter` → `entity-templates.md`
  - `.uzflow` → `uzflow` → reserved for future visual scripting flow
  - `.uzbt` → `uzbt` → `BehaviorTreeImporter` → `behavior-trees.md`
  - `.uzfx` → `uzfx` → `ParticleImporter` → `particle-system.md`
  - `.uznavmesh` → `uznavmesh` → `NavmeshImporter` → `navmesh.md`
  - `.uzterrain` → `uzterrain` → `TerrainImporter` → `terrain-system.md`
- [ ] 4.2 Specify importer registration API: `AssetImporterRegistry::register(ext, IAssetImporter*)`

## 5. Design — IAssetImporter Interface

- [ ] 5.1 Define `IAssetImporter` pure-virtual interface in `src/engine/assets/asset_importer.h`
- [ ] 5.2 Specify `parse(path, ParsedDoc&) -> ImportResult` — fills doc from file
- [ ] 5.3 Specify `migrate(old_version, ParsedDoc&) -> bool` — upgrades doc to current schema
- [ ] 5.4 Specify `reimport(path) -> void` — incremental re-parse on hot-reload
- [ ] 5.5 Specify `get_dependencies(ParsedDoc&, vector<string>&)` — for source asset DB tracking
- [ ] 5.6 Specify `ImportResult` struct: `ok`, `error_message`, `error_line`, `error_column`

## 6. Design — Hot-Reload Protocol

- [ ] 6.1 Specify FileWatcher extension filter — all registered extensions subscribe automatically
- [ ] 6.2 Specify hot-reload dispatch flow: `FileWatcher` → `AssetImporterRegistry::on_file_changed(path)` → `reimport()` → subscriber notification
- [ ] 6.3 Specify subscriber interface: `IAssetReloadSubscriber::on_asset_reloaded(path, handle)`
- [ ] 6.4 Specify reload latency target: < 100ms from file write to subscriber notification
- [ ] 6.5 Specify error isolation: failed reimport logs error, keeps previous asset version in registry

## 7. Design — Lua Access Pattern

- [ ] 7.1 Specify `ctx.assets:load(path) -> userdata` — synchronous load, returns typed userdata
- [ ] 7.2 Specify `ctx.assets:reload(path)` — manual force-reimport from Lua
- [ ] 7.3 Specify `ctx.assets:on_reloaded(path, fn)` — Lua hot-reload callback registration
- [ ] 7.4 Specify asset userdata lifetime — tied to scene VM; released on `uz_game_stop`

## 8. Design — Error Reporting

- [ ] 8.1 Specify IPC message format: `asset.parse_error` → `{ path, line, column, message }`
- [ ] 8.2 Specify VSCode diagnostics integration — reuse `shader-diagnostics.ts` pattern for asset files
- [ ] 8.3 Specify console error output format for non-editor builds

## 9. Write Spec

- [ ] 9.1 Create `docs/specs/data-driven-framework.md` — full spec with all sections from steps 2–8
- [ ] 9.2 Add format grammar BNF/EBNF appendix
- [ ] 9.3 Add annotated example for each registered file type (`.uzevent`, `.uzphysmat`, `.uzentity`, `.uzbt`)
- [ ] 9.4 Update `docs/README.md` — add `data-driven-framework.md` to specs table
- [ ] 9.5 Update `docs/specs/asset-pipeline.md` — add reference to data-driven framework spec

## 10. Review

- [ ] 10.1 Verify `.uzscene` format is compatible with grammar (or document deliberate exceptions)
- [ ] 10.2 Verify all downstream task specs can express their data in this format
- [ ] 10.3 Confirm grammar is parseable by a 200-line C parser (no external dependencies)
- [ ] 10.4 Verify AI agents can generate valid files from the spec without ambiguity
