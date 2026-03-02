# Spec: Audio Snapshots

> **Status**: Planned (Phase 3)
> **Owner**: `game.dll` (SnapshotManager, SnapshotStack, Lua bindings)
> **Dependencies**: Audio System, Shared State System, Event Bus
> **Core Concept**: Pre-defined audio mix presets with smooth blending and stack-based state management
> **File Format**: `.uzsnapshot` (INI-based, defines volume/pitch/effect overrides per bus)

---

## Overview

Audio snapshots are named mixer presets that define:

- **Per-bus volume, pitch, and effect parameters**
- **Smooth blending** between multiple active snapshots (weighted average)
- **State stacking** — push/pop semantics for hierarchical mix control
- **Automatic triggering** — linked to game states (combat, pause, dialogue, underwater)

**Problem solved**: Without snapshots, managing audio mix across game states requires manual volume calls scattered throughout code. Snapshots centralize mix design.

**Three usage patterns exist**:

| Pattern | Use Case | Example |
|---------|----------|---------|
| **State-driven** | Automatic transition on GameState change | "combat" snapshot activates when `game.combat_active = true` |
| **Explicit push/pop** | Temporary audio changes (cutscene, menu) | `ctx.audio:push_snapshot("pause_menu")` → `pop_snapshot()` |
| **Blended mix** | Smooth blend between multiple snapshots | Exploration (base) + low HP (danger overlay) simultaneously |

---

## Architecture

```
GameState (Shared State System)
│
├─ game.combat_active = false
├─ game.player_health = 100
├─ game.location = "cave"
└─ [triggers automatic snapshot transitions]

SnapshotManager (singleton in GameContext)
│
├── SnapshotRegistry
│   ├── .uzsnapshot asset loading + parsing
│   ├── Cache of loaded Snapshot structs
│   └── State → Snapshot mapping (GameState change listener)
│
├── SnapshotStack (active snapshots)
│   ├── [Snapshot "ambient", weight=1.0]
│   ├── [Snapshot "danger", weight=0.5]    ← blending
│   ├── [Snapshot "dialogue", weight=1.0]
│   └── Recomputes blended mix each frame
│
├── MixOutput (computed blend)
│   ├── master_volume = 1.0
│   ├── sfx_volume = 0.7 (weighted average)
│   ├── music_volume = 0.8
│   ├── voice_volume = 1.0
│   ├── reverb = {...}  (per bus)
│   └── [applies to XAudio2 submixes each frame]
│
└── EventBus integration
    └── SnapshotEvent { type, snapshot_id, weight }
```

---

## Asset Format: `.uzsnapshot`

Snapshots define volume, pitch, and DSP effect parameters per audio bus.

### Basic Structure

```ini
[Snapshot]
id = ambient_default
version = 1
description = "Base ambient mix for normal gameplay"
blend_time_ms = 500          ; fade-in duration when activated

; Bus volumes (0.0 = silent, 1.0 = full)
[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.8
pitch = 1.0

[Bus.Music]
volume = 0.8
pitch = 1.0

[Bus.Voice]
volume = 1.0

; Optional: DSP effects per bus
[Bus.SFX.Reverb]
type = reverb
room_size = 0.1
damping = 0.3
wet = 0.2

[Bus.Music.Equalizer]
type = eq
low_gain = 0.0
mid_gain = 0.0
high_gain = 0.0
```

### Field Reference

#### `[Snapshot]` Section

| Field | Type | Default | Purpose |
|-------|------|---------|---------|
| `id` | string | required | Unique identifier (alphanumeric + `_`) |
| `version` | int | 1 | Format version |
| `description` | string | "" | Human-readable description (editor use) |
| `blend_time_ms` | int | 500 | Fade duration when pushed to stack |
| `auto_trigger_state` | string | "" | GameState expression (if true, auto-activate) |
| `priority` | int | 0 | Higher priority snapshots win when both active (0-100) |

#### `[Bus.*]` Section

Each bus can have volume and pitch overrides:

| Field | Type | Default | Range | Purpose |
|-------|------|---------|-------|---------|
| `volume` | float | 1.0 | 0.0-1.0 | Output level (0=mute) |
| `pitch` | float | 1.0 | 0.5-2.0 | Playback speed (1.0=normal) |

#### `[Bus.*.Effect]` Sections

Each bus can have DSP effects applied:

**Reverb**:
```ini
[Bus.Music.Reverb]
type = reverb
room_size = 0.6        ; 0.0-1.0 (larger = more resonant)
damping = 0.5          ; 0.0-1.0 (higher = dull)
width = 1.0            ; 0.0-1.0 (stereo width)
wet_level = 0.3        ; 0.0-1.0 (reverb intensity)
dry_level = 1.0        ; 0.0-1.0 (direct signal)
```

**Equalizer** (3-band):
```ini
[Bus.SFX.EQ]
type = eq
low_freq = 200         ; Hz (center frequency)
low_gain = -3.0        ; dB
mid_freq = 1000
mid_gain = 0.0
high_freq = 5000
high_gain = 2.0
```

**Limiter** (prevent clipping):
```ini
[Bus.Master.Limiter]
type = limiter
threshold_db = -3.0    ; above this, compress aggressively
attack_ms = 1.0
release_ms = 100.0
```

---

## Predefined Snapshots

The engine ships with standard snapshots:

### 1. `ambient_default`

Base ambient mix for normal exploration/gameplay:

```ini
[Snapshot]
id = ambient_default
blend_time_ms = 1000
description = "Normal gameplay ambience"

[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.8

[Bus.Music]
volume = 0.8

[Bus.Voice]
volume = 1.0
```

### 2. `combat_active`

Music emphasizes, SFX slightly reduced for clarity:

```ini
[Snapshot]
id = combat_active
blend_time_ms = 300
description = "High-intensity combat mix"
auto_trigger_state = game.combat_active == true

[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.7
pitch = 1.05    ; slightly higher pitch (feels urgent)

[Bus.Music]
volume = 1.0    ; music louder in combat

[Bus.Voice]
volume = 1.0
```

### 3. `dialogue_active`

SFX and music ducked, voice clear:

```ini
[Snapshot]
id = dialogue_active
blend_time_ms = 300
description = "Dialogue in progress - reduce ambient"
auto_trigger_state = ctx.dialogue:is_playing()

[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.4    ; significantly reduced

[Bus.Music]
volume = 0.3    ; significantly reduced

[Bus.Voice]
volume = 1.0    ; voice front and center
```

### 4. `pause_menu`

Master volume reduced, menu music takes over:

```ini
[Snapshot]
id = pause_menu
blend_time_ms = 500
description = "Pause menu - mute game audio"

[Bus.Master]
volume = 0.3    ; all game sounds quiet

[Bus.SFX]
volume = 0.0    ; mute SFX

[Bus.Music]
volume = 0.0    ; mute game music

[Bus.Voice]
volume = 0.2    ; voice very quiet
```

### 5. `underwater`

Muffled highs, reverb, low clarity:

```ini
[Snapshot]
id = underwater
blend_time_ms = 800
description = "Underwater environment - muffled sound"

[Bus.Master]
volume = 0.8

[Bus.SFX]
volume = 0.6

[Bus.Music]
volume = 0.5
pitch = 0.95    ; slightly lower pitch (feels cold)

[Bus.Voice]
volume = 0.7

[Bus.SFX.Equalizer]
type = eq
low_freq = 100
low_gain = 0.0
mid_freq = 1000
mid_gain = -6.0   ; reduce mids (muffled)
high_freq = 5000
high_gain = -12.0 ; heavy HF reduction (very muffled)

[Bus.Master.Reverb]
type = reverb
room_size = 1.0
damping = 0.8
wet_level = 0.4
```

### 6. `menu_background`

Minimal, peaceful menu ambience:

```ini
[Snapshot]
id = menu_background
blend_time_ms = 1000
description = "Main menu - calm, welcoming"

[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.3    ; minimal UI clicks

[Bus.Music]
volume = 1.0    ; menu music featured

[Bus.Voice]
volume = 0.5    ; very quiet voice guide
```

---

## Snapshot Blending

### Stack-Based Model

The `SnapshotStack` maintains an ordered list of active snapshots:

```cpp
struct SnapshotStack {
    vector<SnapshotInstance> stack;
    float                    fade_speed = 0.5f;  // smoothness
};

struct SnapshotInstance {
    StringId  snapshot_id;
    float     weight;           // 0.0-1.0 (how much this mix influences output)
    float     target_weight;    // blend toward this
    uint32_t  blend_age_ms;     // time since activated (for fade curves)
};
```

### Blending Math

Per bus, compute **weighted average** of all active snapshots:

```
final_volume = Σ(snapshot.bus.volume × snapshot.weight) / Σ(snapshot.weight)
final_pitch  = Σ(snapshot.bus.pitch × snapshot.weight) / Σ(snapshot.weight)
```

**Example with 2 snapshots**:

```
Stack:
  [0] "ambient_default", weight = 1.0
  [1] "danger", weight = 0.3

Blending (SFX bus):
  ambient_default.sfx.volume = 0.8
  danger.sfx.volume = 0.4

  final_volume = (0.8 × 1.0 + 0.4 × 0.3) / (1.0 + 0.3)
               = (0.8 + 0.12) / 1.3
               = 0.92 / 1.3
               ≈ 0.71
```

### Blend Timing

When a snapshot is pushed, its weight fades in over `blend_time_ms`:

```
t = 0ms          → weight = 0.0
t = blend_time   → weight = target_weight
```

Exponential easing for smooth interpolation (not linear).

---

## Lua API: Stack Operations

### Push and Pop

```lua
-- Push snapshot onto stack (fades in over blend_time_ms)
ctx.audio:push_snapshot("dialogue_active")
ctx.audio:push_snapshot("dialogue_active", 0.8)  -- explicit weight (default 1.0)

-- Pop top snapshot (fades out)
ctx.audio:pop_snapshot()

-- Pop specific snapshot (removes from anywhere in stack)
ctx.audio:pop_snapshot("dialogue_active")

-- Clear entire stack (returns to base ambient)
ctx.audio:clear_snapshots()
```

### Query Stack

```lua
-- Is snapshot currently active (anywhere in stack)?
local is_active = ctx.audio:is_snapshot_active("combat_active")

-- Get all active snapshots
local stack = ctx.audio:get_snapshot_stack()
for i, snapshot in ipairs(stack) do
    print(i .. ": " .. snapshot.id .. " (weight=" .. snapshot.weight .. ")")
end

-- Get computed blend for a bus
local sfx_volume = ctx.audio:get_bus_volume("SFX")   -- blended result
```

### Weight Adjustment

Modify weight of active snapshot (for smooth transitions):

```lua
-- Transition weight over time
ctx.audio:set_snapshot_weight("danger", 0.0, 1000)  -- fade out over 1000ms
ctx.audio:set_snapshot_weight("danger", 1.0, 500)   -- fade in over 500ms
```

---

## State-Driven Snapshots (Auto-Trigger)

### GameState Expression

Snapshots can auto-activate when game state changes:

```ini
[Snapshot]
id = combat_active
auto_trigger_state = game.combat_active == true
priority = 50

[Snapshot]
id = low_health_warning
auto_trigger_state = player.health < 30
priority = 60

[Snapshot]
id = underwater
auto_trigger_state = player.location == "underwater_cave"
priority = 40
```

### State Change Handler

When `ctx.shared` (GameState) changes:

```cpp
// Pseudocode in SnapshotManager
void on_game_state_changed(const GameStateEvent& event) {
    foreach snapshot in snapshot_registry {
        if (snapshot.auto_trigger_state) {
            bool should_be_active = eval_lua(snapshot.auto_trigger_state);

            if (should_be_active && !stack.contains(snapshot.id)) {
                push_snapshot(snapshot.id);
            }
            else if (!should_be_active && stack.contains(snapshot.id)) {
                pop_snapshot(snapshot.id);
            }
        }
    }
}
```

### Example: Combat Mix

```lua
-- In combat loop or event handler
function on_combat_started()
    ctx.shared.combat_active = true
    -- SnapshotManager auto-detects this change
    -- Pushes "combat_active" snapshot automatically
end

function on_combat_ended()
    ctx.shared.combat_active = false
    -- SnapshotManager auto-pops "combat_active" snapshot
end
```

---

## Common Patterns

### Pattern 1: Menu with Blend

```lua
function on_pause_menu_open()
    ctx.audio:push_snapshot("pause_menu", 1.0, 500)  -- fade over 500ms
    ctx.ui:show_menu("pause")
end

function on_pause_menu_close()
    ctx.ui:hide_menu()
    ctx.audio:pop_snapshot()  -- fades out over default time
end
```

### Pattern 2: Layered Danger

```lua
function update_player_health(health)
    if health > 75 then
        ctx.audio:pop_snapshot("danger_medium")
        ctx.audio:pop_snapshot("danger_critical")
    elseif health > 50 then
        ctx.audio:push_snapshot("danger_medium", 0.5)
        ctx.audio:pop_snapshot("danger_critical")
    else
        ctx.audio:push_snapshot("danger_critical", 1.0)
    end
end
```

Stack now has base ambience + danger layers blending smoothly.

### Pattern 3: Underwater with Echo

```lua
function enter_underwater_cave()
    ctx.shared.player_location = "underwater_cave"
    -- "underwater" snapshot auto-activates
    -- After 2 seconds, add extra echo effect
    ctx:schedule_after(2000, function()
        ctx.audio:push_snapshot("underwater_echo", 0.6)
    end)
end

function exit_underwater_cave()
    ctx.shared.player_location = "surface"
    -- All underwater snapshots auto-deactivate
end
```

---

## Configuration File: `.uzproj`

Snapshot settings in project configuration:

```toml
[audio]
snapshots_enabled = true
snapshot_directory = "assets/audio/snapshots"
snapshot_default = "ambient_default"

; Snapshot blending
snapshot_blend_speed = 0.5              ; 0.0 (instant) to 1.0 (slow)
snapshot_crossfade_curve = "exponential" ; "linear" or "exponential"

; Priority system
snapshot_priority_enabled = true
snapshot_priority_mode = "highest"     ; "highest" or "sum"
```

---

## IPC Handlers (Editor-Only)

### `audio.list_snapshots`

List all available snapshots.

```json
Request:
{}

Response:
{
  "ok": true,
  "snapshots": [
    {
      "id": "ambient_default",
      "description": "Base ambient mix",
      "buses": ["Master", "SFX", "Music", "Voice"]
    },
    {
      "id": "combat_active",
      "description": "Combat mix",
      "auto_trigger": "game.combat_active == true",
      "priority": 50
    }
  ]
}
```

### `audio.preview_snapshot`

Preview how a snapshot sounds.

```json
Request:
{
  "snapshot_id": "underwater"
}

Response:
{
  "ok": true,
  "snapshot_id": "underwater",
  "mix": {
    "Master": {"volume": 0.8, "pitch": 1.0},
    "SFX": {"volume": 0.6, "pitch": 1.0},
    "Music": {"volume": 0.5, "pitch": 0.95},
    "Voice": {"volume": 0.7, "pitch": 1.0}
  }
}
```

### `audio.push_snapshot` / `audio.pop_snapshot`

Push/pop snapshots in editor for testing.

```json
Request (push):
{
  "snapshot_id": "combat_active",
  "weight": 1.0
}

Response:
{
  "ok": true,
  "stack": [
    {"id": "ambient_default", "weight": 1.0},
    {"id": "combat_active", "weight": 1.0}
  ]
}
```

---

## Performance Notes

### Memory Usage

- **Per-snapshot**: ~2 KB (metadata + bus parameters)
- **Stack overhead**: ~100 bytes per active snapshot
- **Blending computation**: <0.1ms per frame (CPU)

### Optimization Tips

- **Limit stack depth**: Rarely need >3 snapshots active simultaneously
- **Reuse snapshots**: Rather than push/pop frequently, adjust weights
- **Cache expressions**: Auto-trigger state expressions evaluated once per frame
- **Effect chains**: Keep to 1-2 effects per bus (diminishing returns beyond that)

---

## Hot-Reload

When a `.uzsnapshot` asset is modified:

1. Editor detects file change
2. SnapshotManager re-parses snapshot
3. If snapshot is active in stack, its effects update immediately
4. No pop/push required — smooth update in-place

---

## Error Handling

| Scenario | Behavior |
|----------|----------|
| Snapshot not found | Warning logged, push fails silently |
| Invalid bus name | Warning, effect ignored |
| Auto-trigger eval error | Logged as error, snapshot remains unchanged |
| Stack overflow (>10 snapshots) | Warning, push fails |
| Invalid blend time | Clamped to 0-5000ms range |

---

## Testing

Snapshot system testing includes:

1. **Unit tests**: Blending math, weight lerp, stack operations
2. **Integration tests**: Push/pop snapshots, verify audio bus volumes update
3. **State tests**: Auto-trigger expressions evaluated correctly
4. **Blending tests**: Multiple snapshots blend smoothly (no pops/cracks)
5. **Performance tests**: Blend computation cost, stack depth scaling
6. **Manual tests**: Listen to transitions, verify emotional intent

Coverage target: **95%** of snapshot engine code.

---

## Phase Breakdown

### Phase 3 (Current)

- `.uzsnapshot` INI asset format and SnapshotManager
- SnapshotStack with weighted blending
- Push/pop Lua API with smooth fade-in/fade-out
- Per-bus volume, pitch, and effect override support
- Auto-trigger expressions via GameState listener
- Predefined snapshots (ambient, combat, dialogue, pause, underwater, menu)
- Priority system for snapshot ordering
- IPC handlers (editor preview)
- Hot-reload support

### Phase 4+ (Future)

- **Snapshot sequencing**: Pre-defined sequences of snapshots (intro mix → tension → climax)
- **Conditional effects**: Apply effects only if hardware supports (fallback if not)
- **Audio visualization**: Spectrum analyzer, waveform preview in editor
- **Snapshot versioning**: A/B compare different mix variations
- **Snapshot export**: Save mix as separate file for reuse across projects

---

## Related Docs

- [Audio System](./audio-system.md) — Bus architecture, voice pool, XAudio2 integration
- [Dialogue System](./dialogue-system.md) — Auto-dialogue snapshot triggering example
- [Shared State System](./shared-state-system.md) — GameState object for auto-triggers
- [Event Bus](./event-bus.md) — SnapshotEvent emission and listening

---

## Example: Complete Snapshot Set

**File**: `assets/audio/snapshots/combat.uzsnapshot`

```ini
[Snapshot]
id = combat_active
version = 1
description = "High-intensity combat — music up, SFX down for clarity"
blend_time_ms = 300
priority = 75
auto_trigger_state = game.combat_active == true

[Bus.Master]
volume = 1.0
pitch = 1.0

[Bus.SFX]
volume = 0.65
pitch = 1.05

[Bus.Music]
volume = 1.0
pitch = 1.0

[Bus.Voice]
volume = 1.0
pitch = 1.0

; Optional: slight compression on master to prevent clipping
[Bus.Master.Limiter]
type = limiter
threshold_db = -3.0
attack_ms = 1.0
release_ms = 100.0
```

**File**: `assets/audio/snapshots/low_health.uzsnapshot`

```ini
[Snapshot]
id = low_health_warning
version = 1
description = "Player near death — urgent, high-frequency emphasis"
blend_time_ms = 250
priority = 80
auto_trigger_state = player.health < 30

[Bus.Master]
volume = 1.0

[Bus.SFX]
volume = 0.8
pitch = 1.1   ; higher pitch (more urgent)

[Bus.Music]
volume = 0.9

[Bus.Voice]
volume = 1.0

[Bus.Master.Equalizer]
type = eq
low_freq = 100
low_gain = -3.0
mid_freq = 1000
mid_gain = 0.0
high_freq = 5000
high_gain = 6.0   ; boost highs (feels urgent)
```

**Play in Lua**:

```lua
function on_player_health_changed(new_health)
    if new_health < 30 and not ctx.audio:is_snapshot_active("low_health_warning") then
        ctx.audio:push_snapshot("low_health_warning", 1.0, 250)
    elseif new_health >= 50 then
        ctx.audio:pop_snapshot("low_health_warning")
    end
end
```

