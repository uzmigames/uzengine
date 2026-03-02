# Audio Guide — UzEngine

Complete guide to implementing audio in your UzEngine game: playback, 3D positioning, mixing, and integration with dialogue and snapshots.

> **Status**: Audio comes in Phase 2 (basic playback) and Phase 3 (3D audio, snapshots, dialogue).
> **Prerequisites**: Scene system (Phase 1), basic Lua scripting knowledge.

---

## Quick Start: Playing a Sound

The simplest way to play audio is via the context API in a Lua script:

```lua
-- Play a 2D sound effect
ctx.audio:play_2d("res://audio/button_click.wav", {
    volume = 0.8,
    pitch = 1.0,
    bus = "SFX"
})
```

## Audio in Scene Files

Add audio components directly to nodes in `.uzscene`:

```ini
[node id=1 type=Node3D name="DoorSlam" parent=0]
audio_source = "res://audio/door_slam.wav"
audio_volume = 0.9
audio_spatial = true
audio_min_dist = 1.0
audio_max_dist = 50.0
audio_bus = "SFX"
```

Then in a Lua script attached to the node:

```lua
function on_start()
    self:play_audio()  -- Play immediately
end
```

---

## 3D Audio: Spatial Positioning

Position sounds in 3D space using the `spatial` flag and distance attenuation:

```ini
[node id=5 type=Node3D name="BirdSong" parent=0]
position = [10.0, 5.0, 0.0]
audio_source = "res://audio/birds.wav"
audio_spatial = true
audio_min_dist = 5.0      ; sound starts fading at 5m
audio_max_dist = 100.0    ; silent beyond 100m
audio_loop = true
audio_play_on_start = true
```

The listener (usually the camera/player) determines what the player hears. Mark the camera node with:

```ini
[node id=2 type=Camera name="MainCamera" parent=0]
audio_listener = true     ; only one per scene
```

The engine automatically computes volume and panning based on:
- Distance between listener and sound source
- Angle relative to listener's forward direction
- Doppler effect (pitch shift if sound is moving)

---

## Audio Buses and Mixing

Four independent buses allow separate volume control:

| Bus | Default | Use |
|-----|---------|-----|
| Master | 100% | Global output |
| SFX | 80% | Effects and ambient |
| Music | 80% | Background music |
| Voice | 100% | Dialogue and announcements |

Assign a sound to a bus in `.uzscene`:

```ini
audio_bus = "SFX"    ; or "Music", "Voice", "Master"
```

Or in Lua, adjust bus volume dynamically:

```lua
-- Fade out music when combat starts
ctx.audio:set_bus_volume("Music", 0.5)

-- Later, restore
ctx.audio:set_bus_volume("Music", 0.8)
```

---

## Audio Events (Data-Driven Playback)

Instead of hardcoding which sound plays, use `.uzevent` assets to define sound variations and playback modes:

**File**: `assets/audio/events/footstep.uzevent`

```ini
[AudioEvent]
name = footstep_concrete
mode = random_no_repeat      ; randomly pick a variation
clips[] = res://audio/sfx/footstep_01.wav
clips[] = res://audio/sfx/footstep_02.wav
clips[] = res://audio/sfx/footstep_03.wav
pitch_min = 0.95
pitch_max = 1.05
volume_min = 0.8
volume_max = 1.0
bus = sfx
```

Trigger from Lua:

```lua
-- Play footstep on current node
ctx.audio:trigger("footstep_concrete")

-- Play at a specific position
ctx.audio:trigger_at("footstep_concrete", Vec3(10, 0, 0), 1.0)

-- Play and follow entity
ctx.audio:trigger_attached("footstep_concrete", entity_id)
```

Benefits:
- Designer can adjust pitch/volume variation without code changes
- Hot-reload: Change `.uzevent` file, sounds update immediately
- Reusable: Same event across multiple game locations

---

## Adaptive Music: Dynamic Stems

For music that changes based on game state (combat intensity, exploration mood), use stem-based music:

**File**: `assets/audio/music/combat_theme.uzmusic`

```ini
[MusicTrack]
name = combat_theme
bpm = 120
stems[] = res://audio/music/combat_base.uzaudio       ; always playing
stems[] = res://audio/music/combat_percussion.uzaudio ; drums (combat only)
stems[] = res://audio/music/combat_intensity.uzaudio  ; synth stabs (danger)
loop_bar = 8                                           ; sync loop point
transition_quantize = bar                             ; snap to bar boundary
```

Control from Lua:

```lua
function on_combat_started()
    ctx.audio:play_music("combat_theme")
    ctx.audio:set_stem_active("combat_theme", "combat_percussion", true)
end

function on_player_low_health()
    -- Add intensity layer
    ctx.audio:set_stem_active("combat_theme", "combat_intensity", true)
end

function on_combat_ended()
    -- Fade out to exploration
    ctx.audio:crossfade_music("combat_theme", "exploration_theme", 2.0)
end
```

---

## Audio Snapshots: Mix Presets

Snapshots are pre-defined mixer settings. Use them to quickly change audio character for different scenarios:

```lua
-- During dialogue, duck music and SFX
ctx.audio:push_snapshot("dialogue_active", 1.0, 300)  -- 300ms fade-in

-- Later, restore previous mix
ctx.audio:pop_snapshot()
```

Common snapshots:
- **ambient_default**: Base gameplay mix
- **combat_active**: Emphasized music, reduced SFX
- **dialogue_active**: SFX/Music ducked, voice clear
- **pause_menu**: Game audio muted (menu music on separate system)
- **underwater**: Muffled highs, long reverb

See [Audio Snapshots spec](../specs/audio-snapshots.md) for configuration details.

---

## Dialogue Integration

Dialogue automatically ducks SFX and music to keep voice clear:

```lua
ctx.dialogue:play("npc_greeting")
-- Engine automatically activates "dialogue_active" snapshot
-- When dialogue completes, snapshot is removed
```

For voice acting with subtitles:

```ini
[Line_001]
speaker = Merchant
text = "Welcome to my shop!"
audio = res://audio/dialogue/merchant_greeting_en.wav
duration_ms = 2000
```

The system routes voice audio to the Voice bus and displays word-level subtitles synchronized to the audio.

See [Dialogue System spec](../specs/dialogue-system.md) for full details.

---

## Occlusion and Obstruction

Sounds passing through walls are automatically filtered (muffled):

```toml
[audio]
occlusion_enabled = true
occlusion_update_hz = 10        ; check 10x per second
occlusion_max_volume_reduction_db = -20.0
```

The engine raycasts from listener to sound source through physics geometry. If blocked, a low-pass filter is applied to simulate muffling.

---

## Performance Tips

### Memory
- **Keep static SFX small**: ~100 KB per sound effect
- **Stream large files**: Music >2 MB is streamed from disk (Phase 3)
- **Voice clips cached**: Load dialogue early to avoid hitches

### CPU
- **Limit 3D sounds**: ~32 concurrent 3D audio sources
- **Batch occlusion checks**: Query every 100ms, not every frame
- **Use virtual voices**: Excess sounds tracked in RAM, resume when slot free

### Audio Quality
- **Export at 48 kHz**: Standard game audio sample rate
- **Use stereo**: Mono saves space but stereo feels better
- **Normalize to -3dB**: Headroom for mixing without clipping

---

## Debugging Audio

### Check Playing Voices

In VSCode editor (Phase 2):

```json
audio.stop_all {}
```

### List Active Snapshots

```lua
local stack = ctx.audio:get_snapshot_stack()
for i, snapshot in ipairs(stack) do
    print(i .. ": " .. snapshot.id .. " (" .. snapshot.weight .. ")")
end
```

### Test Playback

```lua
ctx.audio:play_2d("res://audio/test.wav", {volume = 0.5})
```

### Profiler

Monitor audio CPU and memory in the VSCode audio profiler panel (Phase 2+).

---

## Common Patterns

### Pattern: Footstep Sounds

```lua
-- In character controller script
function on_ground_contact()
    local surface = detect_surface_under_player()
    local event_name = "footstep_" .. surface  -- footstep_concrete, footstep_grass, etc.
    ctx.audio:trigger(event_name)
end
```

### Pattern: Dialogue with Background Music

```lua
function talk_to_npc(npc_id)
    -- Music ducks automatically with dialogue snapshot
    ctx.dialogue:play("npc_" .. npc_id .. "_greeting")

    ctx.events:subscribe("dialogue_completed", function(e)
        if e.dialogue_id == "npc_" .. npc_id .. "_greeting" then
            -- Dialogue finished, music returns to normal
            ctx.quests:complete("talk_to_npc")
        end
    end)
end
```

### Pattern: Combat Music Layer Control

```lua
function on_player_health_changed(health, max_health)
    local health_pct = health / max_health

    if health_pct > 0.5 then
        ctx.audio:set_stem_active("combat_theme", "combat_intensity", false)
    elseif health_pct < 0.25 then
        ctx.audio:set_stem_active("combat_theme", "combat_intensity", true)
    end
end
```

---

## Configuration

Audio settings in `.uzproj`:

```toml
[audio]
max_voices = 128
fallback_to_tts = false
occlusion_enabled = true
occlusion_update_hz = 10
default_bus_volume_sfx = 0.8
default_bus_volume_music = 0.8

[audio.memory]
static_clips_mb = 256
streaming_buffer_mb = 16

[audio.snapshots]
default_snapshot = "ambient_default"
```

---

## Related Specs

- [Audio System](../specs/audio-system.md) — Asset format, component API, hot-reload
- [Audio Snapshots](../specs/audio-snapshots.md) — Mix presets, blending, state-driven triggers
- [Dialogue System](../specs/dialogue-system.md) — Voice acting, subtitles, localization
- [Audio Events](../specs/audio-system.md#audio-event-system) — Data-driven sound variations
- [Lua Scripting](./lua-scripting-guide.md) — Context API, event subscription
- [Engine Audio Architecture](../engine/audio-system.md) — XAudio2 internals, voice pool, DSP chains
