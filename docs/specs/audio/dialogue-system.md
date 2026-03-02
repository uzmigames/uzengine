# Spec: Dialogue System

> **Status**: Planned (Phase 3)
> **Owner**: `game.dll` (DialogueManager, Lua bindings, EventBus integration)
> **Dependencies**: Audio System, Localization System, Event Bus, UI System
> **Core Concept**: Data-driven dialogue trees with branching choices, voice acting integration, and automatic subtitle timing
> **File Format**: `.uzdialogue` (INI-based, AI-friendly, human-readable)

---

## Overview

The dialogue system enables branching conversations with voice acting, automatic subtitle timing, and conditional branching. Dialogues are authored as `.uzdialogue` assets — plain-text INI files that define:

- **Lines**: Dialogue text, speaker name, associated audio clip
- **Choices**: Player-selectable branches with conditions
- **Voice Acting Pipeline**: Association between dialogue lines and localized audio clips
- **Subtitle Timing**: Word-level timestamps extracted from audio metadata
- **Localization**: Auto-routing to locale-specific audio variants (e.g., `_pt-BR.uzaudio`)
- **Callbacks**: Lua hooks for game state changes (quest updates, item acquisition, etc.)

**Three playback paths exist**:

| Path | Usage | Context |
|------|-------|---------|
| **Play Mode** | Full dialogue playback with subtitles, choices, voice audio | Game running with UI |
| **Editor Preview** | Test dialogue trees, preview audio, inspect branching | VSCode editor via IPC |
| **Cutscenes** | Cinematics with fixed dialogue + camera animations | Lua-driven sequences |

---

## Architecture

```
DialogueManager (singleton in GameContext)
│
├── DialogueRegistry
│   ├── .uzdialogue asset loading + parsing
│   ├── Cache of loaded DialogueTree structs
│   └── Localization variant resolution (en → pt-BR routing)
│
├── DialoguePlayer (state machine for active dialogue)
│   ├── current_line_id
│   ├── current_choice_index
│   ├── line_audio_voice (XAudio2 voice handle)
│   ├── subtitle_component (UI binding)
│   └── State: Idle → Playing → AwaitingChoice → Finished
│
├── EventBus integration
│   └── DialogueEvent { type, speaker, line_id, choice_selected }
│
└── Lua bindings
    ├── ctx.dialogue:play(dialogue_id)
    ├── ctx.dialogue:pause/resume/stop
    ├── ctx.dialogue:select_choice(index)
    └── on_dialogue_start/on_line_complete callbacks
```

**Subsystems involved**:

- **Audio System**: Plays voice clip via XAudio2, handles voice bus ducking
- **Localization System**: Routes dialogue text + audio to locale (pt-BR variant selection)
- **UI System**: Renders subtitles, choice buttons, speaker name
- **Event Bus**: Emits DialogueEvent for quest systems, character reactions
- **ECS**: dialogue_player component tracks active dialogue state

---

## Asset Format: `.uzdialogue`

Dialogue files are INI format with structured sections:

### Basic Structure

```ini
[Dialogue]
id = npc_greeting
version = 1
speaker_default = Merchant
locale = en         ; base locale, overridable at play time

[Line_001]
speaker = Merchant
text = "Welcome, traveler. What brings you to my humble shop?"
audio = res://audio/dialogue/merchant_greeting.wav
duration_ms = 2500
next = Line_002

[Line_002]
speaker = Merchant
text = "I have rare items from across the realm."
audio = res://audio/dialogue/merchant_items.wav
duration_ms = 1800
next = Choice_001

[Choice_001]
text_option_1 = "What do you sell?"
text_option_2 = "Never mind."
target_line_1 = Line_003
target_line_2 = Dialogue_End

[Line_003]
speaker = Merchant
text = "Swords, armor, potions... you name it!"
audio = res://audio/dialogue/merchant_inventory.wav
duration_ms = 2200
next = Dialogue_End

[Dialogue_End]
type = end
```

### Detailed Field Reference

#### `[Dialogue]` Section

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `id` | string | required | Unique identifier (alphanumeric + `_`) |
| `version` | int | 1 | Format version (for backwards compatibility) |
| `speaker_default` | string | "" | Default speaker name if not specified per line |
| `locale` | string | "en" | Base locale for text (other locales loaded from `_locale` variants) |
| `duration_auto_scale` | float | 1.0 | Multiplier for auto-calculated line durations (reading speed adjustment) |

#### `[Line_*]` Section

Each line is a dialogue statement. Numbering is arbitrary (`Line_001`, `Line_100`, etc.) — graph is defined by `next` pointers.

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `speaker` | string | inherit from `[Dialogue]` | Character name displayed in UI |
| `text` | string | required | Dialogue text (substitutes locale keys: `{key}` → localized string) |
| `audio` | string | "" | Path to voice clip (`res://audio/dialogue/...`), optional |
| `duration_ms` | int | calculated | How long line displays (if no audio) or audio duration (if audio specified) |
| `next` | string | required | ID of next node (`Line_*`, `Choice_*`, or `Dialogue_End`) |
| `condition` | string | "" | Lua expression: `player.has_item("sword")` → only show if true |
| `on_start_callback` | string | "" | Lua function name: `on_merchant_greeting(dialogue_id)` |
| `on_complete_callback` | string | "" | Lua function name: `on_merchant_greeting_end(dialogue_id)` |
| `subtitle_speed_wpm` | int | 200 | Words-per-minute for subtitle reading (if no audio) |

#### `[Choice_*]` Section

Choice nodes present options to the player.

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `text_option_N` | string | required | Display text for Nth choice button |
| `target_line_N` | string | required | Node to jump to if Nth choice selected |
| `condition_N` | string | "" | Optional condition: only show this choice if true |
| `on_select_callback_N` | string | "" | Lua function: `on_chose_negotiation()` |
| `timeout_ms` | int | 0 | Auto-select first choice after timeout (0=no timeout) |

**Example with conditions**:

```ini
[Choice_BuyArmor]
text_option_1 = "I'll buy the armor."
target_line_1 = Line_PaymentQuery
condition_1 = player.gold >= 100

text_option_2 = "I can't afford that."
target_line_2 = Line_Refused

text_option_3 = "Never mind."
target_line_3 = Dialogue_End
```

If condition_1 fails, option 1 is hidden (not disabled — completely removed from UI).

---

## Voice Acting Pipeline

### Audio Clip Association

Each dialogue line optionally references an audio clip:

```ini
[Line_001]
text = "Welcome!"
audio = res://audio/dialogue/merchant_greeting_en.wav
duration_ms = 2500
```

The audio path uses `res://` protocol and imports to `.uzaudio` format (see Audio System spec).

### Localization-Aware Audio Routing

When a `.uzdialogue` is played in locale `pt-BR`, the system automatically routes to locale-specific audio:

**Rules (checked in order)**:

1. **Exact match**: `res://audio/dialogue/merchant_greeting_pt-BR.wav` → use directly
2. **Language fallback**: `res://audio/dialogue/merchant_greeting_pt.wav` → use Portuguese variant
3. **English fallback**: `res://audio/dialogue/merchant_greeting_en.wav` → use English
4. **No audio**: If none exist, display text-only (no voice)

**Storage convention**:

```
res://audio/dialogue/
├── merchant_greeting_en.wav          (English)
├── merchant_greeting_pt-BR.wav       (Portuguese Brazil)
├── merchant_greeting_es.wav          (Spanish)
├── merchant_greeting_fr.wav          (French)
└── merchant_greeting_ja.wav          (Japanese)
```

The dialogue spec references only the base variant (`_en.wav`); the system handles locale routing at runtime.

### Fallback to Text-to-Speech (Phase 4)

If voice acting is not available and TTS is enabled:

```toml
[dialogue]
fallback_to_tts = true
tts_engine = "Windows"    ; or "google_tts" for online
```

The system generates voice via TTS with locale-specific voice selection (e.g., `pt-BR female voice`).

**Phase 3 defers TTS** — voice acting is mandatory.

---

## Subtitle System

### Subtitle Timing

Subtitles are word-level timed cues extracted from audio metadata (or synthesized from text reading speed).

#### With Voice Audio

If `audio` is specified, `.uzaudio` metadata includes word timestamps (generated at import time):

```json
{
  "duration_ms": 2500,
  "words": [
    {"text": "Welcome", "start_ms": 100, "end_ms": 400},
    {"text": "traveler", "start_ms": 450, "end_ms": 750},
    {"text": "What brings you to my humble shop?", "start_ms": 800, "end_ms": 2500}
  ]
}
```

The UI renders words as they become "active" (start_ms ≤ current_time ≤ end_ms).

**Implementation**: Audio importer extracts word boundaries via speech recognition (Phase 3 uses pre-computed boundaries from voice acting house; Phase 4 adds real-time speech-to-text).

#### Without Voice Audio

If no audio specified, subtitles advance based on text length and reading speed:

```
duration_ms = len(text) / subtitle_speed_wpm * 60,000
```

Default `subtitle_speed_wpm = 200` (typical reading pace). Adjustable per line or globally in `.uzproj`:

```toml
[dialogue]
default_subtitle_speed_wpm = 200
```

### Subtitle Component (UI)

Subtitles render via a UI subtitle component (updated each frame):

```cpp
// ECS Component
struct CDialogueSubtitle {
    string          dialogue_id;        // Currently playing dialogue
    string          line_id;            // Currently active line
    string          current_text;       // Full line text
    vector<Word>    words;              // Timed word array
    float           current_time_ms;    // Time since line start
    bool            is_active;          // true = display subtitles
    uint32_t        font_size;          // Adjustable subtitle size
};

struct Word {
    string start_ms;
    string end_ms;
    string text;
};
```

Lua API to query active subtitle:

```lua
local subtitle = ctx.dialogue:get_active_subtitle()
if subtitle then
    print(subtitle.line_id)    -- "Line_001"
    print(subtitle.text)       -- "Welcome, traveler..."
end
```

---

## Dialogue State Machine

### States and Transitions

```
Idle
  │
  ├─ ctx.dialogue:play(dialogue_id)
  ↓
Playing
  ├─ Audio playing OR reading timer active
  ├─ Subtitles updating
  ├─ Callbacks: on_start_callback fired
  │
  ├─ User pressed skip key (spacebar)
  ├─ Line completes (audio ends or timer expires)
  ↓
AwaitingChoice (if next node is Choice_*)
  ├─ Choice buttons displayed
  ├─ Waiting for ctx.dialogue:select_choice(index)
  ├─ Can timeout (auto-select if timeout_ms > 0)
  │
  ├─ User selects option (or auto-selected)
  ↓
Playing (next line)
  ├─ Jump to target_line_N
  │
  └─ Eventually reaches Dialogue_End or exit condition
  ↓
Idle
  ├─ Callbacks: all on_complete_callback fired
  ├─ Event emitted: DialogueEvent { type: completed }
```

### Concurrent Dialogue

Only one active dialogue per scene at a time. Starting a new dialogue cancels the active one:

```lua
ctx.dialogue:play("merchant_greeting")   -- Starts dialogue A
-- ... later
ctx.dialogue:play("guard_warning")       -- Cancels A, starts dialogue B
-- if guard_warning completes, DialogueEvent { completed, "guard_warning" } fires
```

---

## Lua API

### Play and Control

```lua
-- Start dialogue by ID
ctx.dialogue:play(dialogue_id)
-- Alias: ctx.dialogue:start(dialogue_id)

-- Pause/resume (keeps audio paused, subtitles frozen)
ctx.dialogue:pause()
ctx.dialogue:resume()

-- Stop current dialogue (triggers on_complete_callback, ends state machine)
ctx.dialogue:stop()

-- Skip current line (jump to next immediately)
ctx.dialogue:skip()

-- Select a choice (index is 1-based)
ctx.dialogue:select_choice(1)    -- Select first option
```

### Query State

```lua
-- Is dialogue active?
local is_active = ctx.dialogue:is_playing()

-- Get current dialogue/line ID
local dialogue_id = ctx.dialogue:get_dialogue_id()    -- "npc_greeting"
local line_id = ctx.dialogue:get_line_id()            -- "Line_003"

-- Get active subtitle (nil if no dialogue playing)
local subtitle = ctx.dialogue:get_active_subtitle()
if subtitle then
    print(subtitle.text)         -- "Welcome, traveler..."
    print(subtitle.line_id)      -- "Line_001"
end

-- Get available choices (table of strings, or nil if not in AwaitingChoice)
local choices = ctx.dialogue:get_choices()
if choices then
    for i, choice_text in ipairs(choices) do
        print(i .. ": " .. choice_text)
    end
end

-- Get dialogue duration (total play time for full tree, or current line)
local duration_ms = ctx.dialogue:get_line_duration()   -- ms
```

### Events and Callbacks

Lua functions in the dialogue node itself can hook into dialogue lifecycle:

```lua
-- Called when dialogue starts (any line)
function on_dialogue_start(dialogue_id)
    print("Dialogue started: " .. dialogue_id)
    ctx.audio:set_bus_volume("SFX", 0.5)  -- Duck SFX during dialogue
end

-- Called when a line completes (before advancing to next)
function on_line_complete(dialogue_id, line_id)
    print("Line completed: " .. line_id)
    if line_id == "Line_003" then
        player:trigger_quest_update("talk_to_merchant")
    end
end

-- Called when a choice is selected
function on_choice_selected(dialogue_id, choice_index)
    print("Selected choice: " .. choice_index)
end

-- Called when dialogue ends (all lines exhausted or stop called)
function on_dialogue_end(dialogue_id)
    print("Dialogue ended: " .. dialogue_id)
    ctx.audio:set_bus_volume("SFX", 1.0)  -- Restore SFX volume
end
```

These functions are registered per node or globally (convention: `assets/dialogue/callbacks.lua`).

---

## Branching and Conditions

### Simple Branching (No Conditions)

```ini
[Choice_001]
text_option_1 = "I'm buying."
target_line_1 = Line_BuyArmor

text_option_2 = "Never mind."
target_line_2 = Dialogue_End
```

Both options always shown.

### Conditional Branching

Conditions are Lua expressions evaluated at choice-display time:

```ini
[Choice_BuyArmor]
text_option_1 = "I'll buy the armor. (100 gold)"
target_line_1 = Line_PaymentSuccess
condition_1 = player.gold >= 100

text_option_2 = "I can't afford that."
target_line_2 = Line_CantAfford
```

If `player.gold < 100`, option 1 is hidden (not greyed out). The player only sees option 2.

### Conditional Lines

Lines can also have conditions — if condition fails, line is skipped:

```ini
[Line_GreetIfFirstTime]
text = "Ah, a new face in town!"
condition = not player:has_visited_before()
next = Line_Question

[Line_GreetIfReturning]
text = "Welcome back, friend!"
condition = player:has_visited_before()
next = Line_Question
```

Only one is shown based on condition evaluation.

### Condition Evaluation Context

Conditions are Lua expressions with access to:

- `player` — GameObject reference to player node
- `ctx` — Full context API (scene, saves, etc.)
- `game` — Global game state (custom per game)

```lua
-- Examples:
condition = player.health < 50
condition = ctx.saves:get("rescued_npc") == true
condition = game.day_of_week == "friday"
condition = player:get_inventory():has_item("key")
```

---

## Integration with Audio System

### Voice Bus Ducking

During dialogue, SFX and Music are automatically ducked (volume reduced):

```toml
[dialogue]
voice_ducking_enabled = true
sfx_duck_db = -6.0         ; reduce SFX by 6dB
music_duck_db = -12.0      ; reduce Music by 12dB
duck_fade_in_ms = 500      ; fade out duck when dialogue ends
```

Implementation:

1. `ctx.dialogue:play()` → AudioManager receives `DialogueStartedEvent`
2. Submix voices receive attenuation via XAPO limiter or submix volume
3. `ctx.dialogue:stop()` → AudioManager fades ducking out over 500ms

### Audio Clip Caching

Voice clips are loaded and cached during dialogue load (not at play time):

```cpp
// C++
DialogueManager::load(dialogue_id) {
    foreach line in dialogue.lines {
        if (line.audio) {
            audio_cache[line.audio] = audio_manager->load_clip(line.audio);
        }
    }
}
```

This avoids hitches when dialogue starts.

---

## Dialogue Variable Substitution

Text can include variable references that are substituted at play time:

```ini
[Line_001]
text = "Welcome, {player_name}! You have {gold} gold."

[Line_002]
text = "Your inventory has {item_count} items."
```

Substitution context (Lua):

```lua
ctx.dialogue:play("greeting", {
    player_name = player:get_name(),
    gold = player:get_gold(),
    item_count = player:get_inventory():count()
})
```

Any undefined variables remain as literal strings in subtitles (e.g., `{undefined}` stays as `{undefined}`).

---

## Dialogue Event System

Dialogue emits events via EventBus for external systems (quests, character state, etc.):

```cpp
// C++ EventBus
struct DialogueEvent {
    enum Type { Started, LineChanged, ChoiceSelected, Completed, Ended };

    Type            type;
    StringId        dialogue_id;
    StringId        line_id;           // only for LineChanged
    uint32_t        choice_index;      // only for ChoiceSelected (1-based)
    StringId        speaker;           // current speaker name
};
```

Lua listeners:

```lua
ctx.events:subscribe("dialogue_started", function(event)
    print("Dialogue started: " .. event.dialogue_id)
end)

ctx.events:subscribe("dialogue_completed", function(event)
    print("Dialogue completed: " .. event.dialogue_id)
    ctx.quests:update_quest("talk_to_npc")
end)
```

---

## IPC Handlers (Editor-Only)

For testing dialogues in VSCode without running the full game:

### `dialogue.play`

Start dialogue in editor preview mode.

```json
Request:
{
  "dialogue_id": "npc_greeting",
  "variables": {
    "player_name": "Hero"
  }
}

Response:
{
  "ok": true,
  "dialogue_id": "npc_greeting",
  "lines": 15,
  "first_line": "Line_001",
  "preview_mode": true
}

Error:
{
  "ok": false,
  "error": "Dialogue not found: npc_greeting"
}
```

### `dialogue.get_tree`

Fetch full dialogue tree structure for editor visualization.

```json
Request:
{
  "dialogue_id": "npc_greeting"
}

Response:
{
  "ok": true,
  "id": "npc_greeting",
  "lines": {
    "Line_001": {
      "speaker": "Merchant",
      "text": "Welcome, traveler...",
      "next": "Line_002",
      "is_choice": false,
      "audio": "res://audio/dialogue/merchant_greeting_en.wav"
    },
    "Choice_001": {
      "options": [
        {"text": "What do you sell?", "target": "Line_003"},
        {"text": "Never mind.", "target": "Dialogue_End"}
      ],
      "is_choice": true
    }
  }
}
```

### `dialogue.preview_line`

Preview audio and subtitles for a single line.

```json
Request:
{
  "dialogue_id": "npc_greeting",
  "line_id": "Line_001",
  "locale": "pt-BR"
}

Response:
{
  "ok": true,
  "speaker": "Merchant",
  "text": "Welcome, traveler...",
  "audio": "res://audio/dialogue/merchant_greeting_pt-BR.wav",
  "duration_ms": 2500,
  "words": [
    {"text": "Welcome", "start_ms": 100, "end_ms": 400},
    {"text": "traveler", "start_ms": 450, "end_ms": 750}
  ]
}
```

---

## Performance Notes

### Memory Usage

- **Per-dialogue**: ~5-10 KB (metadata, line graph structure)
- **Voice clips cached**: Up to 100 MB (configurable cache limit in `.uzproj`)
- **UI subtitles**: 1 component per scene, minimal overhead

### CPU Cost

- **Line advance**: <1ms (pointer chase through graph)
- **Choice evaluation**: ~0.5ms per condition (Lua eval overhead)
- **Audio playback**: Handled by XAudio2 (async, low CPU)

### Optimization Tips

- **Cache frequently-used dialogues** in startup scripts
- **Unload dialogue** after scene transition to free voice clips
- **Batch conditions** — evaluate once, cache results
- **Use event IDs** instead of string names in hot loops

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Dialogue not found | Warning logged, play_dialogue fails, returns error |
| Line not found | Skip to next_line (or Dialogue_End if missing) |
| Audio clip not found | Line plays text-only, no voice (warning logged) |
| Condition eval error | Log error, treat as false (safest fallback) |
| Invalid choice index | Ignored, dialogue stays in AwaitingChoice state |
| Audio hardware unavailable | Voice plays on Voice bus (muted), game continues |

---

## Localization Integration

### CSV String Mapping

Dialogue text uses localization keys for multilingual support:

```csv
dialogue.merchant.greeting_1,Welcome traveler,Bem-vindo viajante,Bienvenido viajero
dialogue.merchant.greeting_2,What brings you here?,O que te traz aqui?,¿Qué te trae aquí?
dialogue.choice.buy,I'll buy it,Vou comprar,Lo compraré
dialogue.choice.refuse,Never mind,Nunca importa,No importa
```

In `.uzdialogue`, reference these keys:

```ini
[Line_001]
text = {dialogue.merchant.greeting_1}
audio = res://audio/dialogue/merchant_greeting_en.wav

[Choice_001]
text_option_1 = {dialogue.choice.buy}
target_line_1 = Line_PaymentSuccess

text_option_2 = {dialogue.choice.refuse}
target_line_2 = Dialogue_End
```

At runtime, `ctx.locale:t()` resolves keys:

```cpp
// C++
string text = ctx.locale->translate("dialogue.merchant.greeting_1");
// → "Welcome traveler" (en) or "Bem-vindo viajante" (pt-BR)
```

---

## Hot-Reload

When a `.uzdialogue` asset is modified:

1. Editor watcher detects file change
2. DialogueManager re-parses `.uzdialogue`
3. In-play dialogues unaffected (continue with cached tree)
4. New dialogues use updated tree

If editor preview dialogue is active:

1. Asset reloads
2. Preview updates in real-time (tree visualization refreshed)
3. No interrupt to preview playback

---

## Testing

Dialogue system testing includes:

1. **Unit tests**: INI parsing, condition evaluation, graph traversal
2. **Integration tests**: Play dialogue → make choices → verify line sequence
3. **Localization tests**: Verify audio locale routing and subtitle timing
4. **UI tests**: Subtitles render correctly, choice buttons appear/hide based on conditions
5. **Performance tests**: Dialogue load time, condition eval overhead, audio handoff latency
6. **Manual tests**: Listen to full dialogue with voice acting, verify emotional tone

Coverage target: **95%** of dialogue engine code.

---

## Phase Breakdown

### Phase 3 (Current)

- INI `.uzdialogue` asset format and DialogueManager
- Graph-based dialogue tree (lines, choices, branching)
- Voice acting integration via audio clips + locale routing
- Subtitle timing from audio metadata (word-level)
- Lua API: play, pause, resume, skip, select_choice
- Condition expressions for lines and choices
- EventBus integration (DialogueEvent)
- Audio ducking (SFX/Music reduction during dialogue)
- Variable substitution in text
- IPC handlers (editor preview)

### Phase 4+ (Future)

- **Text-to-Speech fallback**: Auto-generate voice if clip missing
- **Character emotion tracking**: Facial expressions + voice modulation hints
- **Dialogue animation**: Lip-sync animation from audio (requires rigged characters)
- **Dialogue branching visualization**: DAG graph editor in VSCode extension
- **Branching conversation AI**: Dynamic dialogue generation via LLM (optional, for AI games)
- **Multiplayer dialogue**: Synchronized branching for co-op games
- **Dialogue save/load**: Persist dialogue state across sessions

---

## Related Docs

- [Audio System](./audio-system.md) — Voice playback, ducking, event triggers
- [Localization System](./localization.md) — Text and audio locale routing
- [Event Bus](./event-bus.md) — DialogueEvent subscription and emission
- [UI System](./ui-system.md) — Subtitle rendering, choice button components
- [Lua Scripting Guide](../guides/lua-scripting-guide.md) — Event hooks and context API

---

## Example: Complete Dialogue

**File**: `assets/dialogues/merchant_intro.uzdialogue`

```ini
[Dialogue]
id = merchant_intro
version = 1
speaker_default = Merchant
locale = en

[Line_001]
speaker = Merchant
text = {dialogue.merchant.welcome}
audio = res://audio/dialogue/merchant_welcome_en.wav
duration_ms = 2500
next = Line_002

[Line_002]
speaker = Merchant
text = {dialogue.merchant.inventory}
audio = res://audio/dialogue/merchant_inventory_en.wav
duration_ms = 1800
next = Choice_001

[Choice_001]
text_option_1 = {dialogue.choice.buy}
target_line_1 = Line_003
condition_1 = player.gold >= 100

text_option_2 = {dialogue.choice.cant_afford}
target_line_2 = Line_004

text_option_3 = {dialogue.choice.leave}
target_line_3 = Dialogue_End

[Line_003]
speaker = Merchant
text = {dialogue.merchant.purchase_thanks}
audio = res://audio/dialogue/merchant_thanks_en.wav
duration_ms = 2000
on_complete_callback = on_player_purchased_armor
next = Dialogue_End

[Line_004]
speaker = Merchant
text = {dialogue.merchant.come_back}
audio = res://audio/dialogue/merchant_comeback_en.wav
duration_ms = 1600
next = Dialogue_End

[Dialogue_End]
type = end
```

**Lua callback** (`assets/dialogue/callbacks.lua`):

```lua
function on_player_purchased_armor(dialogue_id)
    player:remove_item("gold", 100)
    player:add_item("iron_armor")
    ctx.quests:update("talk_to_merchant", "completed")
    ctx.events:emit("player_acquired_armor", { item = "iron_armor" })
end
```

**Play from script**:

```lua
-- In player interaction script
function on_player_interact_with_merchant()
    ctx.dialogue:play("merchant_intro", {
        player_name = player:get_name()
    })
end
```

