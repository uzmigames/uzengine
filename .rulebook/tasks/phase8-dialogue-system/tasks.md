## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-audio-event-system` is complete — `ctx.audio:trigger()` works
- [ ] 1.2 Confirm `phase2-ui-system` is complete — `ctx.ui` DOM manipulation and `uz-modal` widget work
- [ ] 1.3 Confirm `phase3-localization` is complete — `ctx.locale:t(key)` returns localized string
- [ ] 1.4 Confirm `phase0-data-driven-framework` spec is complete — understand `IAssetImporter`

## 2. .uzdialogue Format

- [ ] 2.1 Define `[meta]` block: `type = uzdialogue`, `version = 1`, `uuid`
- [ ] 2.2 Define `[node.<id>]` sections:
  - `type`: `line`, `choice`, `condition`, `goto`, `end`
  - `line` type: `speaker`, `text_key` (localization key), `audio_event` (optional), `duration_s` (0 = auto from audio), `next = <id>`
  - `choice` type: `prompt_key`, `options[] = <id>:<label_key>`, `condition_fn?` (Lua expression)
  - `condition` type: `lua_expr: string`, `true_next: <id>`, `false_next: <id>`
  - `goto` type: `target: <id>`
  - `end` type: optional `on_end_fn` Lua callback name
- [ ] 2.3 Define `[start]` section: `node = <id>` — entry point
- [ ] 2.4 Write example: 3-line conversation with 2-option branch and condition check

## 3. DialogueManager

- [ ] 3.1 Create `src/engine/dialogue/dialogue_manager.h/cpp`
- [ ] 3.2 Implement `DialogueManager::start(path, lua_state*)` — load dialogue, set current node to `[start].node`, begin playback
- [ ] 3.3 Implement `DialogueManager::update(dt)` — advance playback timer; auto-advance `line` nodes on duration elapsed
- [ ] 3.4 Implement `DialogueManager::advance()` — manually advance to `next` node (skip or continue)
- [ ] 3.5 Implement `DialogueManager::choose(option_index)` — for `choice` nodes; set current node to chosen option target
- [ ] 3.6 Implement `DialogueManager::skip()` — instantly complete current audio, advance to next
- [ ] 3.7 Implement `DialogueManager::stop()` — halt playback, clear UI, restore music volume
- [ ] 3.8 Implement node dispatch: `line` → show subtitle, trigger audio; `choice` → show choice UI; `condition` → evaluate Lua expression; `goto` → set node; `end` → call on_end_fn, stop

## 4. DialogueImporter

- [ ] 4.1 Create `src/engine/assets/dialogue_importer.h/cpp` — `DialogueImporter : IAssetImporter`
- [ ] 4.2 Implement `parse(path, ParsedDoc&)` → build `DialogueScript` (map of node_id → `DialogueNode`)
- [ ] 4.3 Implement `get(path) -> const DialogueScript*`
- [ ] 4.4 Implement hot-reload: `reimport(path)` updates registry; active dialogue restarts from beginning

## 5. Subtitle Rendering

- [ ] 5.1 Inject subtitle `uz-modal` element into `UIDocument` on `start()`:
  - `<uz-modal open id="uz-dialogue-modal"><div id="uz-subtitle"></div><div id="uz-choices"></div></uz-modal>`
- [ ] 5.2 On `line` node: set `#uz-subtitle` text to `ctx.locale:t(text_key)` + speaker name
- [ ] 5.3 On `choice` node: populate `#uz-choices` with `uz-button` per option; bind click to `choose(index)`
- [ ] 5.4 Subtitle fade-in animation: `ctx.ui:tween("uz-dialogue-modal", {opacity=1}, 0.15, "ease_out")`
- [ ] 5.5 On dialogue end: fade-out and remove modal

## 6. Voice Acting Integration

- [ ] 6.1 On `line` node: call `ctx.audio:trigger(audio_event)` to play voiced line
- [ ] 6.2 Compute `duration_s` from audio event if `duration_s == 0`: use voice handle duration
- [ ] 6.3 On `skip()`: call `voice_handle.stop()`
- [ ] 6.4 Subtitle timing: start duration timer when voice starts (account for playback delay)

## 7. Music Ducking

- [ ] 7.1 On `start()`: save current music bus volume, fade to `dialogue_duck_volume` (default 0.3)
- [ ] 7.2 On `stop()`: fade music bus back to saved volume
- [ ] 7.3 Config: `dialogue_duck_volume` and `dialogue_duck_fade_s` from `.uzproj` `[audio]` section

## 8. Lua API

- [ ] 8.1 Implement `ctx.dialogue:start(path)` — starts dialogue
- [ ] 8.2 Implement `ctx.dialogue:start(path, callbacks_table)` — optional: `{ on_end = fn, on_node = fn }`
- [ ] 8.3 Implement `ctx.dialogue:choose(index)` — player choice selection
- [ ] 8.4 Implement `ctx.dialogue:advance()` — manual line advance
- [ ] 8.5 Implement `ctx.dialogue:skip()` — skip current line audio
- [ ] 8.6 Implement `ctx.dialogue:stop()` — abort dialogue
- [ ] 8.7 Implement `ctx.dialogue:is_active() -> bool`
- [ ] 8.8 Lua condition expressions in `[node.condition]` evaluated via `lua_loadstring + lua_call`; errors → false

## 9. Testing

- [ ] 9.1 Unit test: parse 3-node `.uzdialogue` → `DialogueScript` with correct node graph
- [ ] 9.2 Unit test: `line` node auto-advances after `duration_s` of `update()` calls
- [ ] 9.3 Unit test: `choice` node — `choose(1)` advances to option 1 target
- [ ] 9.4 Unit test: `condition` node with `GameState` value — true/false branch selection
- [ ] 9.5 Unit test: `skip()` on line node — immediately advances
- [ ] 9.6 Integration test: start 2-line dialogue → subtitle shown → voice triggered → second line shown after duration
- [ ] 9.7 Integration test: music ducking — music bus volume reduced during dialogue, restored on end

## 10. Documentation

- [ ] 10.1 Create `docs/specs/dialogue-system.md` — full spec: `.uzdialogue` format, node types, Lua API, subtitle rendering
- [ ] 10.2 Update `docs/guides/lua-scripting-guide.md` — "Dialogue System" section with branching example
