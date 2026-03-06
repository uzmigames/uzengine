# Audio Editor & Sound Designer — Tool Specification

> **Status**: Planned (Phase 2 / Phase 3)
> **Owner**: VSCode extension (WebView panels) + `uz_engine.lib` (XAudio2 backend)
> **Depends On**: `docs/specs/audio-system.md`, `docs/specs/scene-system.md`
> **Target Users**: Sound designers, game designers
> **Backend**: XAudio2 (Windows native), X3DAudio (spatial, Phase 3)

---

## 1. Overview & Goals

The Audio Editor is a suite of integrated tools inside VSCode that gives sound designers and game designers full control over audio assets, mixing, spatial placement, and adaptive music — without leaving the editor. It connects directly to the running `render.exe` process via the existing IPC/MCP channel, enabling real-time preview and parameter tweaking.

### Design Goals

| Goal | Description |
|------|-------------|
| **Immediate feedback** | Every fader move, loop point edit, or node connection is audible within one XAudio2 buffer cycle (~22ms at 44100 Hz) |
| **Non-destructive editing** | Source files are never modified; all changes write to `.uzcue`, `.uzmix`, or `.uzmusic` sidecar files |
| **Lua-first integration** | Every editor control maps to a named Lua API call; the API ref panel is always one click away |
| **Graceful degradation** | If XAudio2 is unavailable, all editor panels remain functional in preview-only mode (no audio output, no crash) |
| **AI-native formats** | File formats are plain-text INI-like; an AI agent can generate and modify them without GUI interaction |

### Scope Boundaries

This tool is NOT a full digital audio workstation (DAW). It does not perform:
- Multi-track recording or audio capture
- Non-linear audio arrangement beyond the Music Sequencer
- Plugin hosting (VST/AU)
- MIDI sequencing (Phase 4+ future)

### Integration with XAudio2 Bus Architecture

The 4-bus hierarchy defined in `audio-system.md` is the canonical routing model. All editor mixer strips map 1:1 to XAudio2 submix voices:

```
[SFX bus]    IXAudio2SubmixVoice  ──┐
[Music bus]  IXAudio2SubmixVoice  ──┤
[Voice bus]  IXAudio2SubmixVoice  ──┤──→ [Master] IXAudio2SubmixVoice ──→ IXAudio2MasteringVoice
[Ambient]    (routed via SFX bus) ──┘
```

Volume, mute, and solo operations on a strip call `IXAudio2SubmixVoice::SetVolume()` or `SetOutputMatrix()` directly via IPC handlers.

---

## 2. Audio Mixer

### 2.1 Layout Overview

The Audio Mixer is a VSCode WebView panel (`AudioMixerPanel`) rendered via React. It mirrors a hardware mixing console: vertical channel strips arranged left to right, with the Master bus always rightmost.

```
┌──────────────────────────────────────────────────────────────────────────────────────────┐
│  AUDIO MIXER                                      [Snapshots ▼] [New] [Delete]  [⚙ Prefs]│
├──────────────┬──────────────┬──────────────┬──────────────┬──────────────┬───────────────┤
│  SNAPSHOTS   │     SFX      │    MUSIC     │    VOICE     │   REVERB FX  │    MASTER     │
│ ─────────    │ ─────────    │ ─────────    │ ─────────    │ ─────────    │ ───────────   │
│ > Combat     │ [EQ Graph]   │ [EQ Graph]   │ [EQ Graph]   │ [Room ▼]     │               │
│   Menu       │  Lo  Mi  Hi  │  Lo  Mi  Hi  │  Lo  Mi  Hi  │  Size: 0.4   │               │
│   Underwater │ ─── ─── ───  │ ─── ─── ───  │ ─── ─── ───  │  Damp: 0.5   │               │
│   Cutscene   │ [Compressor] │ [Compressor] │ [Compressor] │  Mix:  0.3   │               │
│              │  Thr: -18dB  │  Thr: -12dB  │  Thr:  -6dB  │              │               │
│              │  Rat:   4:1  │  Rat:   2:1  │  Rat:   3:1  │  [Add Send]  │               │
│              │  Atk:  10ms  │  Atk:  20ms  │  Atk:   5ms  │              │               │
│              │  Rel: 100ms  │  Rel: 200ms  │  Rel:  80ms  │              │               │
│              │              │              │              │              │               │
│              │  Reverb Snd  │  Reverb Snd  │  Reverb Snd  │              │               │
│              │  [═══  0.2]  │  [═══  0.0]  │  [═══  0.4]  │              │               │
│              │              │              │              │              │               │
│              │  ┌── VU ──┐  │  ┌── VU ──┐  │  ┌── VU ──┐  │              │  ┌─ VU ─────┐ │
│              │  │▓▓▓▓░░░░│  │  │▓▓▓▓▓░░░│  │  │▓▓░░░░░░│  │              │  │▓▓▓▓▓▓░░░│ │
│              │  │▓▓▓▓░░░░│  │  │▓▓▓▓▓░░░│  │  │▓▓░░░░░░│  │              │  │▓▓▓▓▓▓░░░│ │
│              │  │▓▓▓▓░░░░│  │  │▓▓▓▓▓░░░│  │  │▓▓░░░░░░│  │              │  │▓▓▓▓▓▓░░░│ │
│              │  │▒▒▒▒░░░░│  │  │▒▒▒▒▒░░░│  │  │▒▒░░░░░░│  │              │  │▒▒▒▒▒▒░░░│ │
│              │  │░░░░░░░░│  │  │░░░░░░░░│  │  │░░░░░░░░│  │              │  │░░░░░░░░░│ │
│              │  └─L──R───┘  │  └─L──R───┘  │  └─L──R───┘  │              │  └─L──R─────┘ │
│              │              │              │              │              │               │
│              │  ┌─ FADER ─┐ │  ┌─ FADER ─┐ │  ┌─ FADER ─┐ │              │  ┌─ FADER ──┐ │
│              │  │    │    │ │  │    │    │ │  │    │    │ │              │  │     │    │ │
│              │  │    ●    │ │  │    ●    │ │  │    ●    │ │              │  │     ●    │ │
│              │  │         │ │  │         │ │  │         │ │              │  │          │ │
│              │  └─────────┘ │  └─────────┘ │  └─────────┘ │              │  └──────────┘ │
│              │  -6.0 dB     │  -6.0 dB     │   0.0 dB     │              │    0.0 dB     │
│              │  [M] [S]     │  [M] [S]     │  [M] [S]     │              │  [LIMITER ON] │
│              │  SFX         │  MUSIC        │  VOICE        │              │   MASTER      │
└──────────────┴──────────────┴──────────────┴──────────────┴──────────────┴───────────────┘
  Legend: ▓ = signal (green/yellow/red), ▒ = -6dB zone, ░ = headroom
```

### 2.2 Mixer Strip Components

Each bus strip contains the following controls, top to bottom:

#### EQ Section (3-Band Parametric)

| Band | Type | Default Freq | Range | Gain Range |
|------|------|-------------|-------|-----------|
| Low | Low-shelf | 80 Hz | 20–500 Hz | -18 to +12 dB |
| Mid | Peaking bell | 1000 Hz | 200–8000 Hz | -18 to +12 dB |
| High | High-shelf | 8000 Hz | 2000–20000 Hz | -18 to +12 dB |

The EQ graph is a mini frequency-response curve rendered as an SVG. Clicking the graph opens a full-size EQ editor with draggable band handles.

Implementation: EQ is applied as an XAudio2 effect chain entry using `XAPOFX1_METERING` for VU + a custom biquad filter effect (`IXAPOParameters`-derived). Alternatively, the master bus uses `ReverbI3DL2Master` from `XAPOFX.h` for the reverb send return.

#### Compressor Section

```
Threshold : -18.0 dB  [slider]
Ratio     :   4.0 : 1 [slider 1.0–20.0]
Attack    :  10.0 ms  [slider 1–200ms]
Release   : 100.0 ms  [slider 10–2000ms]
Gain      :   0.0 dB  [slider -6 to +12]
[●] Enabled
```

Implementation: XAudio2 does not provide a built-in compressor; this uses a custom XAPO (Cross-platform Audio Processing Object) written as `UzCompressorXAPO : public CXAPOParametersBase`.

#### Reverb Send

A single rotary knob (0.0–1.0) controlling the wet/dry send amount routed to the Reverb FX aux bus. Implemented as `IXAudio2SubmixVoice::SetOutputMatrix()` with a fractional coefficient to the reverb return voice.

#### VU Meter

- Dual-channel (L/R) vertical bargraph
- Green zone: -∞ to -12 dBFS
- Yellow zone: -12 to -6 dBFS
- Red zone: -6 to 0 dBFS
- Peak hold: 2-second decay, click to reset
- RMS envelope displayed as a slightly darker shade behind the peak bar
- Clip indicator: red LED, latches until clicked
- Update rate: 60 Hz via WebSocket push from engine

#### Fader

- Vertical slider, range: -∞ to +6 dB (linear pixel, logarithmic dB)
- Double-click: reset to 0 dB
- Shift+drag: fine precision (0.1 dB steps)
- Right-click: type exact dB value
- Maps to `IXAudio2SubmixVoice::SetVolume()` in linear amplitude

#### Mute / Solo

- **[M] Mute**: Zeroes the bus volume. Other buses unaffected. Stacks with Solo.
- **[S] Solo**: Sets all other buses to volume 0. Only one solo active at a time (last-wins). Clicking solo again un-solos all.
- Both states persisted per mixer snapshot.

### 2.3 Master Bus Strip

The Master strip has additional controls:

```
[LIMITER]
  Ceiling  :  -0.3 dBFS  [slider]
  Lookahead:    5.0 ms   [fixed, non-editable]
  [●] Always Enabled      (cannot be disabled)

Output Gain:  0.0 dB     [fader]
```

The limiter is always active on Master to prevent clipping regardless of other fader positions. Implemented as `ReverbI3DL2Master` with `ReflectionsLevel = -10000` (reflections off) and peak limiting mode, or alternatively as a second `UzCompressorXAPO` with ratio=∞.

### 2.4 Reverb FX Aux Bus

The Reverb FX column is a send/return bus, not a direct-signal bus:

- **Room Preset**: Dropdown — SmallRoom, MediumRoom, LargeRoom, Hall, Cave, Underwater, Custom
- **Room Size**: 0.0–1.0 (maps to `XAUDIO2FX_REVERB_PARAMETERS::ReflectionsDelay` and `ReverbDelay`)
- **Damping**: 0.0–1.0 (maps to `HighEQGain` and `LowEQGain` curve)
- **Mix**: 0.0–1.0 (wet/dry ratio of the reverb return into Master)
- Implemented via `XAudio2CreateReverb()` as a submix voice effect chain

Room presets map to `XAUDIO2FX_REVERB_I3DL2_PRESET_*` constants:

| Preset Name | XAudio2 Constant |
|------------|-----------------|
| SmallRoom | `XAUDIO2FX_REVERB_I3DL2_PRESET_SMALLROOM` |
| MediumRoom | `XAUDIO2FX_REVERB_I3DL2_PRESET_MEDIUMROOM` |
| LargeRoom | `XAUDIO2FX_REVERB_I3DL2_PRESET_LARGEROOM` |
| Hall | `XAUDIO2FX_REVERB_I3DL2_PRESET_CONCERTHALL` |
| Cave | `XAUDIO2FX_REVERB_I3DL2_PRESET_CAVE` |
| Underwater | `XAUDIO2FX_REVERB_I3DL2_PRESET_UNDERWATER` |

### 2.5 Snapshot Panel

Snapshots save the complete mixer state (all fader positions, EQ, compressor, mute/solo, reverb send levels) to a `.uzmix` file.

```
┌─ Snapshots ──────────────┐
│ > ● Combat               │  ← active (bold)
│     Menu                 │
│     Underwater           │
│     Cutscene             │
│   [+ New Snapshot]       │
│   [Rename] [Delete]      │
│                          │
│  Crossfade: 1.5 s [═══]  │
│  [Apply Selected]        │
└──────────────────────────┘
```

- **Crossfade duration**: Linear volume interpolation over N seconds when switching snapshots
- **Apply Selected**: Sends `audio.apply_snapshot` IPC with the snapshot name
- **Active indicator**: Green dot marks the currently applied snapshot
- Snapshots are invoked from Lua via `ctx.audio:apply_snapshot("Combat", fade_time)` (see Section 7)

### 2.6 IPC Handlers (Mixer)

| Handler | Payload | Effect |
|---------|---------|--------|
| `audio.mixer.set_fader` | `{ bus, volume_db }` | `SetVolume()` on bus submix voice |
| `audio.mixer.set_mute` | `{ bus, muted }` | `SetVolume(0)` or restore |
| `audio.mixer.set_solo` | `{ bus, solo }` | Zero all other buses |
| `audio.mixer.set_eq` | `{ bus, band, freq, gain_db }` | Update biquad XAPO params |
| `audio.mixer.set_compressor` | `{ bus, threshold_db, ratio, attack_ms, release_ms }` | Update compressor XAPO params |
| `audio.mixer.set_reverb_send` | `{ bus, amount }` | `SetOutputMatrix()` to reverb return |
| `audio.mixer.apply_snapshot` | `{ name, fade_s }` | Interpolate all faders to snapshot values |
| `audio.mixer.save_snapshot` | `{ name }` | Serialize current mixer state to `.uzmix` |

---

## 3. Sound Cue Editor (Node-Based)

### 3.1 Overview

The Sound Cue Editor is a node graph for defining complex, data-driven sound behavior without Lua scripting. A `.uzcue` file describes a directed acyclic graph evaluated at playback time. The output node maps to one XAudio2 source voice request from the voice pool.

Use cases:
- **Footsteps**: Random selection from 6 surface-specific WAV variants
- **Weapon fire**: Random pitch + one-shot with tail reverb send
- **Ambience layer**: Sequential cycling of day/dusk/night ambience clips
- **Character voice**: Parameter-switched dialogue lines (hurt, idle, alert)

### 3.2 Node Types

#### Source Nodes (inputs)

| Node | Description | Properties |
|------|-------------|-----------|
| `AudioFile` | Single audio clip | `clip` (res:// path), `volume`, `pitch` |
| `Random` | Pick one from N children at random | `weights[]` (optional, uniform if empty) |
| `Sequential` | Play children in round-robin order | `reset_on_stop` bool |
| `Looping` | Wrap a clip in a seamless loop | `loop_start_sample`, `loop_end_sample` |

#### Modifier Nodes (transform signal)

| Node | Description | Properties |
|------|-------------|-----------|
| `PitchRandomizer` | Apply random pitch offset | `min_semitones`, `max_semitones` |
| `VolumeRandomizer` | Apply random volume offset | `min_db`, `max_db` |
| `Delay` | Wait N ms before passing signal | `delay_ms` (0–5000) |
| `FadeIn` | Linear fade from 0 on start | `duration_ms` |
| `FadeOut` | Linear fade to 0 on end | `duration_ms` |

#### Control Nodes (branching)

| Node | Description | Properties |
|------|-------------|-----------|
| `Switch` | Route to one of N outputs by integer param | `param_name` (Lua ctx variable), `cases[]` |
| `Branch` | Route to one of two outputs by bool param | `param_name` (Lua ctx variable) |
| `Concatenate` | Play child A then child B sequentially | — |

#### Output Node

Every cue graph has exactly one `Output` node. It receives the final resolved clip and parameters, then requests a source voice from the pool.

```
Output properties:
  bus      : "SFX" | "Music" | "Voice" | "Master"
  spatial  : bool (true = 3D; false = 2D)
  priority : 0–7 (higher = less likely to be evicted from voice pool)
```

### 3.3 Visual Layout

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  SOUND CUE EDITOR: res://audio/cues/footstep.uzcue          [Play] [Save]  │
├─────────────────────────────────────────────────────────────────────────────┤
│  [+ AudioFile] [+ Random] [+ Sequential] [+ Switch] [+ Modifier ▼]         │
│ ─────────────────────────────────────────────────────────────────────────── │
│                                                                             │
│  ┌─────────────┐                                                            │
│  │  Random     │──────────────────────────────────────────────────────┐    │
│  │  weights:   │    ┌──────────────────┐                              │    │
│  │  [auto]     │◄───│  AudioFile       │                              │    │
│  │             │    │  clip: step1.wav │                              │    │
│  │             │    │  vol: 0.9        │                              │    │
│  │             │    └──────────────────┘                              │    │
│  │             │    ┌──────────────────┐                              │    │
│  │             │◄───│  AudioFile       │                              │    │
│  │             │    │  clip: step2.wav │                              │    │
│  │             │    │  vol: 0.85       │                              │    │
│  │             │    └──────────────────┘                              │    │
│  │             │    ┌──────────────────┐    ┌──────────────────────┐  │    │
│  │             │◄───│  AudioFile       │◄───│  PitchRandomizer     │  │    │
│  │             │    │  clip: step3.wav │    │  min: -2 semitones   │  │    │
│  │             │    │  vol: 1.0        │    │  max: +2 semitones   │  │    │
│  └─────────────┘    └──────────────────┘    └──────────────────────┘  │    │
│         │                                                               │    │
│         ▼                                                               │    │
│  ┌─────────────────┐                                                   │    │
│  │  VolumeRandom   │                                                   │    │
│  │  min: -3 dB     │                                                   │    │
│  │  max:  0 dB     │                                                   │    │
│  └─────────────────┘                                                   │    │
│         │                                                               │    │
│         ▼                                                               │    │
│  ┌─────────────────────────────────────────────────────────────────┐   │    │
│  │  OUTPUT                                                          │   │    │
│  │  bus: SFX   spatial: true   priority: 3                         │   │    │
│  └─────────────────────────────────────────────────────────────────┘   │    │
│                                                                         │    │
└─────────────────────────────────────────────────────────────────────────────┘
  Right-click node: Edit | Duplicate | Delete | Disconnect
  Drag from output pin to input pin to connect
```

### 3.4 `.uzcue` File Format

Sound cue graphs are stored as plain-text INI-like files. Nodes reference each other by ID. The graph is evaluated bottom-up from the Output node.

```ini
; res://audio/cues/footstep_concrete.uzcue
[uzcue version=1 name="FootstepConcrete"]

[node id=1 type=Random]
weights =           ; empty = uniform distribution

[node id=2 type=AudioFile parent=1]
clip   = res://audio/sfx/step_c1.wav
volume = 0.90
pitch  = 1.0

[node id=3 type=AudioFile parent=1]
clip   = res://audio/sfx/step_c2.wav
volume = 0.85
pitch  = 1.0

[node id=4 type=AudioFile parent=1]
clip   = res://audio/sfx/step_c3.wav
volume = 1.0
pitch  = 1.0

[node id=5 type=PitchRandomizer parent=4]
min_semitones = -2.0
max_semitones =  2.0

[node id=6 type=VolumeRandomizer parent=1]
min_db = -3.0
max_db =  0.0

[node id=7 type=Output parent=6]
bus      = SFX
spatial  = true
priority = 3
```

```ini
; res://audio/cues/weapon_pistol.uzcue
[uzcue version=1 name="WeaponPistol"]

[node id=1 type=Random]
weights = 0.5 0.3 0.2

[node id=2 type=AudioFile parent=1]
clip   = res://audio/sfx/pistol_a.wav
volume = 1.0
pitch  = 1.0

[node id=3 type=AudioFile parent=1]
clip   = res://audio/sfx/pistol_b.wav
volume = 0.95
pitch  = 1.0

[node id=4 type=AudioFile parent=1]
clip   = res://audio/sfx/pistol_c.wav
volume = 1.0
pitch  = 1.0

[node id=5 type=PitchRandomizer parent=1]
min_semitones = -1.0
max_semitones =  1.0

[node id=6 type=Output parent=5]
bus      = SFX
spatial  = true
priority = 5
```

### 3.5 Cue Evaluation at Runtime

```
AudioCueEvaluator::evaluate(cue_graph, params) -> ResolvedClip
  1. Start at Output node
  2. Walk graph upstream via parent pointers
  3. At Random nodes: pick child index using weighted RNG
  4. At Sequential nodes: increment ring counter (per-cue instance, thread-local)
  5. At Switch/Branch nodes: read param value from Lua ctx table
  6. At Modifier nodes: accumulate pitch/volume offsets
  7. Return { clip_id, final_volume, final_pitch, bus, spatial, priority }
  8. AudioEngine::play_cue() acquires voice from pool and submits buffer
```

No heap allocation during evaluation. All state is on the stack or in pre-allocated per-cue slot arrays.

### 3.6 IPC Handlers (Sound Cue Editor)

| Handler | Payload | Effect |
|---------|---------|--------|
| `audio.cue.preview` | `{ path, params: {} }` | Evaluate cue and play in editor |
| `audio.cue.get_graph` | `{ path }` | Return node graph JSON for WebView |
| `audio.cue.save_graph` | `{ path, nodes[] }` | Serialize graph to `.uzcue` |

---

## 4. 3D Audio Placement Tool

### 4.1 Overview

The 3D Audio Placement Tool integrates into the scene viewport and Inspector panel. It lets sound designers place `CAudioSource` components visually — dragging sources in 3D space, adjusting attenuation radii, and assigning reverb zones — all without writing Lua or editing `.uzscene` files manually.

This tool is active in Phase 3 (X3DAudio integration). In Phase 2, CAudioSource components can be placed but spatial rendering is not active.

### 4.2 Viewport Overlays

When the Audio Placement Tool is the active tool mode (toggled from the toolbar), the scene viewport renders audio gizmos:

#### CAudioSource Gizmo

```
                     ┌ max_dist circle ────────────────────────────────┐
                     │                                                   │
                     │         ┌ min_dist circle ─────────────┐         │
                     │         │                               │         │
                     │         │                ♪              │         │
                     │         │          [Speaker icon]        │         │
                     │         │                               │         │
                     │         └───────────────────────────────┘         │
                     │      (min_dist = 5m solid blue circle)            │
                     │                                                   │
                     └───────────────────────────────────────────────────┘
                          (max_dist = 20m dashed blue circle)
```

- **Inner circle** (solid blue): `min_dist` — full-volume zone, no attenuation
- **Outer circle** (dashed blue, 50% opacity): `max_dist` — silence beyond this radius
- **Directional cone** (orange wedge, directional sources only): inner/outer angle
- **Speaker icon**: 3D billboard always facing camera; yellow if playing, grey if stopped
- **Selection highlight**: Circles turn cyan when the node is selected in Scene Tree

Dragging the inner/outer circle handles adjusts `min_dist`/`max_dist` and writes to the `.uzscene` file immediately.

#### CAudioListener Gizmo

- Rendered as a headphone icon at the listener node's world position
- A translucent forward-facing cone shows the listener's hearing "field"
- Only one listener is shown; if multiple `CAudioListener` components exist, others render in red with a warning tooltip

#### Reverb Zone Gizmo

- Box zones: wireframe cube (green)
- Sphere zones: wireframe sphere (green)
- Zone label overlaid: preset name (e.g., "Cave")
- Overlapping zones shown in yellow with warning

### 4.3 Inspector Properties (CAudioSource)

When a node with `CAudioSource` is selected, the Inspector panel shows:

```
┌─ Audio Source ────────────────────────────────────────────────────┐
│  Sound Cue   [res://audio/cues/footstep.uzcue         ] [Browse] │
│              (or direct .uzaudio asset)                           │
│                                                                   │
│  Bus         [SFX          ▼]  Priority  [3  ▼]                  │
│                                                                   │
│  Volume      [════════●══════════════]  0.90                     │
│  Pitch       [═══════════●═══════════]  1.00                     │
│                                                                   │
│  ─── Spatial ──────────────────────────────────────────────────  │
│  [✓] Spatial (3D)   [✓] Occlusion Raycast                        │
│  Min Distance  [═══●══════════════════]   5.0 m                  │
│  Max Distance  [═══════════════●══════]  20.0 m                  │
│                                                                   │
│  ─── Directional Cone ─────────────────────────────────────────  │
│  [✓] Directional                                                  │
│  Inner Angle   [════════●═════════════]  30.0 °                  │
│  Outer Angle   [══════════════●═══════]  60.0 °                  │
│  Outer Volume  [●═════════════════════]   0.0 (silence outside)  │
│                                                                   │
│  ─── Playback ─────────────────────────────────────────────────  │
│  [✓] Loop    [✓] Play On Start                                   │
│  Trigger     [None ▼] (None / enter_zone / Lua callback)         │
│                                                                   │
│  ─── Reverb Zone ──────────────────────────────────────────────  │
│  Override    [None ▼] (None / or named Reverb Zone node)         │
│                                                                   │
│  [▶ Preview in Editor]   [■ Stop]                                │
└───────────────────────────────────────────────────────────────────┘
```

### 4.4 Directional Cone (X3DAudio)

Directional sound sources use `X3DAUDIO_EMITTER::InnerAngle` / `OuterAngle`:

```
InnerAngle: full source volume within this half-angle from forward vector
OuterAngle: beyond this half-angle, volume is OuterVolume (typically 0)
Between inner and outer: smooth cosine interpolation
```

The cone gizmo renders as an orange wedge in the scene viewport with draggable angle handles.

### 4.5 Reverb Zones

Reverb Zones are special node types (`ReverbZone`) placed as volumes in the scene. When the `CAudioListener` node enters a Reverb Zone trigger, the zone's reverb preset fades in over `fade_in_ms` milliseconds.

```ini
; In .uzscene
[node id=20 type=ReverbZone name="CaveEntrance" parent=1]
position    = [50.0, 0.0, -20.0]
shape       = sphere          ; sphere or box
radius      = 8.0             ; (for sphere)
; half_extents = [4.0, 3.0, 4.0]  ; (for box)
reverb_preset  = Cave
fade_in_ms     = 500
fade_out_ms    = 1000
blend_distance = 2.0          ; crossfade region at zone boundary (meters)
```

Reverb Zone evaluation runs on the audio thread per-frame:
1. Check listener position against all ReverbZone volumes
2. Compute blend weight if listener is in the `blend_distance` boundary region
3. Interpolate `XAUDIO2FX_REVERB_PARAMETERS` between current and target preset
4. Call `IXAudio2Voice::SetEffectParameters()` on the reverb submix voice

### 4.6 Physics-Based Occlusion

When `occlusion_raycast = true` on a `CAudioSource`, the engine fires a physics raycast (via Jolt) from the source position to the listener position once per frame. If the ray hits geometry, volume is attenuated by an `occlusion_factor`:

```
occlusion_factor = 1.0 - (hit_count * 0.3)    ; 0.3 per wall, clamped to [0, 1]
final_volume = source_volume * attenuation * occlusion_factor
```

This is a Phase 3 feature requiring the physics system to be initialized.

### 4.7 IPC Handlers (3D Placement Tool)

| Handler | Payload | Effect |
|---------|---------|--------|
| `audio.source.place` | `{ node_id, position }` | Set node world position |
| `audio.source.set_distances` | `{ node_id, min_dist, max_dist }` | Update CAudioSource in scene |
| `audio.source.set_cone` | `{ node_id, inner_angle, outer_angle }` | Update directional cone |
| `audio.zone.add` | `{ name, shape, position, radius, preset }` | Add ReverbZone node to scene |
| `audio.zone.remove` | `{ node_id }` | Remove ReverbZone from scene |

---

## 5. Music & Ambience Sequencer

### 5.1 Overview

The Music Sequencer is a horizontal timeline editor for designing adaptive music systems. It does not produce audio output itself — it generates a `.uzmusic` definition file consumed by the runtime `MusicSequencer` C++ class, which drives XAudio2 submix voice transitions.

The intensity model (0.0–1.0 float driven from Lua) automatically crossfades between music layers. This enables adaptive combat music, environmental ambience, and cinematic stinger playback.

### 5.2 Layout

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│  MUSIC SEQUENCER: res://audio/music/main_theme.uzmusic          [Play] [Save]      │
├────────────────────────────────────────────────────────────────────────────────────┤
│  BPM: [120 ▼]  Bars: [32]  Time Sig: [4/4 ▼]   [Loop Region: 8–24]               │
├───────────────┬──────────────────────────────────────────────────────────────────┤
│               │  1    2    3    4    5    6    7    8    ..  24  25  26  27  28  │
│               │  |    |    |    |    |    |    |    |    ..   |   |   |   |   | │
│  BASE         │  [═══════════════════════════ bass_loop.ogg ════════════════════] │
│  (always)     │                                                                  │
├───────────────┼──────────────────────────────────────────────────────────────────┤
│  MELODY       │       [══════════════════ melody_a.ogg ═══════════════════]      │
│  (intensity   │                                                                  │
│   > 0.3)      │                                                                  │
├───────────────┼──────────────────────────────────────────────────────────────────┤
│  COMBAT       │                             [═══ combat_strings.ogg ═══════════] │
│  (intensity   │                                                                  │
│   > 0.7)      │                                                                  │
├───────────────┼──────────────────────────────────────────────────────────────────┤
│  STINGER      │                        [▸ stinger_hit.wav]                       │
│  (one-shot)   │                                                                  │
├───────────────┼──────────────────────────────────────────────────────────────────┤
│  MARKERS      │  [Intro]    [▶ Loop Start]              [Loop End ◀]  [Outro]   │
│               │  x=0        x=8                          x=24          x=28     │
├───────────────┴──────────────────────────────────────────────────────────────────┤
│  Intensity:  [═══════════════●══════════════════]  0.65  [Simulate]              │
│  Preview:    [▶ Play From] [■ Stop] [⟳ Loop] [► Test Stinger]                  │
└────────────────────────────────────────────────────────────────────────────────────┘
```

### 5.3 Track (Layer) Types

| Type | Activation | XAudio2 Behavior | Use Case |
|------|-----------|-----------------|----------|
| `Base` | Always active (volume = 1.0) | Loops seamlessly on Music bus | Drum groove, bass, pad |
| `Additive` | Crossfades in as intensity crosses `threshold` | `SetVolume()` ramped over `crossfade_ms` | Melody, combat strings |
| `Stinger` | Triggered from Lua, plays once | One-shot voice, no loop | Hit confirmation, discovery |
| `Ambient` | Intensity-driven, but routes to SFX bus | Separate voice pool slot | Environment loops |

### 5.4 Intensity System

Intensity is a single float (0.0–1.0) that the game drives from Lua:

```lua
-- On entering combat:
ctx.audio:set_music_intensity(0.85)

-- On returning to exploration:
ctx.audio:set_music_intensity(0.2, 2.0)  -- fade over 2 seconds
```

The `MusicSequencer` evaluates intensity every audio frame and smoothly ramps each `Additive` track's volume:

```
for each Additive track T:
    target_vol = (intensity >= T.threshold) ? T.max_volume : 0.0
    current_vol = lerp(current_vol, target_vol, delta_time / T.crossfade_ms)
    T.voice->SetVolume(current_vol)
```

### 5.5 Section Markers

Section markers divide the timeline into named regions with transition rules:

| Marker | Bar | Type | Transition |
|--------|-----|------|-----------|
| `Intro` | 0 | one-shot | Auto-advance to Main after Intro clip ends |
| `Main` | 8 | loop_start | Loop between Main and LoopEnd |
| `LoopEnd` | 24 | loop_end | Jump back to Main |
| `Combat` | 16 | conditional | Jump here when `intensity > 0.8`, return to Main when `< 0.5` |
| `Outro` | 28 | one-shot | Play once on `ctx.audio:stop_music()` |

Section transitions are BPM-quantized: they happen at the next bar boundary to avoid rhythmic discontinuities.

### 5.6 `.uzmusic` File Format

```ini
; res://audio/music/main_theme.uzmusic
[uzmusic version=1 name="MainTheme"]
bpm      = 120
time_sig = 4/4
bars     = 32

; Tracks
[track id=1 type=Base name="Bass"]
clip          = res://audio/music/bass_loop.ogg
volume        = 1.0
bus           = Music
loop_start_bar = 8
loop_end_bar   = 24

[track id=2 type=Additive name="Melody"]
clip          = res://audio/music/melody_a.ogg
volume        = 0.85
bus           = Music
threshold     = 0.3
crossfade_ms  = 1500
loop_start_bar = 8
loop_end_bar   = 24

[track id=3 type=Additive name="CombatStrings"]
clip          = res://audio/music/combat_strings.ogg
volume        = 0.9
bus           = Music
threshold     = 0.7
crossfade_ms  = 800
loop_start_bar = 8
loop_end_bar   = 24

[track id=4 type=Stinger name="HitStinger"]
clip     = res://audio/music/stinger_hit.wav
volume   = 1.0
bus      = Music
bar      = 16             ; preferred bar offset for sync (if triggered off-beat)

; Section markers
[marker id=1 name="Intro" bar=0 type=one_shot]
next = Main

[marker id=2 name="Main" bar=8 type=loop_start]

[marker id=3 name="LoopEnd" bar=24 type=loop_end]
jump_to = Main

[marker id=4 name="Combat" bar=16 type=conditional]
enter_condition = intensity > 0.8
exit_condition  = intensity < 0.5
exit_jump_to    = Main

[marker id=5 name="Outro" bar=28 type=one_shot]
```

### 5.7 BPM Sync

All bar-boundary calculations use:

```
seconds_per_bar = (60.0 / bpm) * beats_per_bar
bar_sample      = bar_index * seconds_per_bar * sample_rate
```

When a section transition is pending, the sequencer schedules it via `IXAudio2SourceVoice::SubmitSourceBuffer()` with `PlayBegin` set to the exact sample offset of the next bar.

### 5.8 IPC Handlers (Music Sequencer)

| Handler | Payload | Effect |
|---------|---------|--------|
| `audio.music.set_intensity` | `{ value, fade_s }` | Ramp intensity over N seconds |
| `audio.music.trigger_stinger` | `{ track_id }` | Fire one-shot stinger voice |
| `audio.music.preview` | `{ path, from_bar }` | Play .uzmusic from given bar |
| `audio.music.stop` | `{ fade_s }` | Fade out all music tracks |
| `audio.music.save` | `{ path, tracks[], markers[] }` | Serialize to .uzmusic |

---

## 6. Waveform Editor (Light)

### 6.1 Overview

The Waveform Editor is a single-clip inspector for basic, non-destructive editing of a `.uzaudio` asset's import parameters. It does NOT modify source files or perform DSP processing. All settings write to an `.uzaudio` import settings block, which is applied at re-import time.

### 6.2 Layout

```
┌────────────────────────────────────────────────────────────────────────────────────┐
│  WAVEFORM EDITOR: res://audio/sfx/door_slam.wav               [Reimport] [Save]   │
├────────────────────────────────────────────────────────────────────────────────────┤
│  Duration: 1.23 s   Sample Rate: 44100 Hz   Channels: Stereo   Format: PCM 16-bit │
├────────────────────────────────────────────────────────────────────────────────────┤
│  ▼ WAVEFORM ──────────────────────────────────────────────────────────────────── │
│                                                                                   │
│   0ms      200ms     400ms     600ms     800ms    1000ms    1200ms               │
│    │         │         │         │         │         │         │                  │
│    ├[TRIM IN]─────────────────────────────────────────────[TRIM OUT]─────────┤   │
│    │         ┌────────────────────────────────────────────────────────┐       │   │
│  L │─────────│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│───────│   │
│  R │─────────│▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│───────│   │
│    │         └────────────────────────────────────────────────────────┘       │   │
│    │         [LOOP START]                              [LOOP END]              │   │
│                                                                                   │
│  [■ Stop] [▶ Preview]  [◄◄ Back 1s]  [Zoom: 100% ▼]  [Normalize]               │
│                                                                                   │
├───────────────────────────────────────────────────────────────────────────────────┤
│  ▼ IMPORT SETTINGS ───────────────────────────────────────────────────────────── │
│  Normalize       [ ] (peak normalize to 0 dBFS on import)                        │
│  Trim Silence    [ ] Threshold: -60 dB   (auto-trim leading/trailing silence)    │
│  Channels        [Stereo ▼]   (Stereo / Mono / As Source)                        │
│  Sample Rate     [44100 ▼]   (44100 / 48000 / 22050 / As Source)                │
│  Fade In         [═══●══════════════════════════════════]  0 ms   (0–5000)       │
│  Fade Out        [═════════════════════════════════●════]  0 ms   (0–5000)       │
│  Loop Point      Start: [      156 ] samples   End: [   54218 ] samples          │
│  Streaming       [ ] Force streaming (default: auto based on size > 2MB)         │
└───────────────────────────────────────────────────────────────────────────────────┘
```

### 6.3 Feature Descriptions

| Feature | Description | Non-Destructive? |
|---------|-------------|-----------------|
| Trim In/Out | Set playback start and end sample offsets in `.uzaudio` header | Yes |
| Loop Points | Set `loop_start_sample` / `loop_end_sample` in header | Yes |
| Normalize | Applied at reimport time — boosts PCM to 0 dBFS peak | Yes (source unchanged) |
| Fade In/Out | Linear envelope baked into PCM at reimport time | Yes (source unchanged) |
| Stereo→Mono | Downmix at reimport; halves file size for SFX | Yes (source unchanged) |
| Sample Rate | Resample at reimport | Yes (source unchanged) |

### 6.4 Loop Point Editor

For seamless looping, the waveform view shows the loop crossfade region:

```
... [loop body] ... [LOOP END] ... [crossfade tail] [LOOP START] ...
                       ↑                               ↑
                   loop_end_sample             loop_start_sample
```

Crossfade baking: At reimport, a short crossfade (default 50 samples) is applied at the loop boundary to eliminate clicks. The crossfade region is displayed as a highlighted zone in the waveform view.

Loop point accuracy aids: The waveform is displayed at full sample resolution near the loop handles, allowing sample-accurate placement for zero-crossing alignment.

### 6.5 Preview Playback

The `[▶ Preview]` button sends `audio.play` IPC with the current trim/loop settings. Looping preview honors the loop points in real time via `XAUDIO2_BUFFER::LoopBegin` and `LoopLength` fields.

---

## 7. Lua API Reference Panel

### 7.1 Overview

The Lua API Reference Panel is an embedded, searchable cheat sheet available in any audio editor panel via the `[Lua API]` toolbar button. It shows the full audio API with live parameter hints, collapsible examples, and a `[Copy]` button per snippet.

### 7.2 Layout

```
┌─ LUA AUDIO API ─────────────────────────────────────────────────────────────────┐
│  [Search: ________________________]                               [✕ Close]      │
├─────────────────────────────────────────────────────────────────────────────────┤
│  ▼ One-Shot Playback                                                             │
│                                                                                  │
│  ctx.audio:play(path, volume, pitch)                                             │
│    path   : string  "res://audio/sfx/click.wav"                                 │
│    volume : float   0.0 – 1.0  (default 1.0)                                   │
│    pitch  : float   0.5 – 2.0  (default 1.0)                                   │
│  Returns: voice_handle (integer)                                                 │
│  [Copy]                                                                          │
│                                                                                  │
│  ctx.audio:play_at(path, position, volume, max_distance)                         │
│    position     : Vec3   world position of sound source                          │
│    max_distance : float  meters before silence (default 100.0)                  │
│  Returns: voice_handle                                                           │
│  [Copy]                                                                          │
│                                                                                  │
│  ctx.audio:play_on_bus(path, bus_name, volume, pitch)                            │
│    bus_name : string  "Master" | "SFX" | "Music" | "Voice"                     │
│  Returns: voice_handle                                                           │
│  [Copy]                                                                          │
│                                                                                  │
│  ctx.audio:play_cue(cue_path, params)                                            │
│    cue_path : string  "res://audio/cues/footstep.uzcue"                         │
│    params   : table   { surface = "concrete", intensity = 0.8 }                 │
│  Returns: voice_handle                                                           │
│  [Copy]                                                                          │
│                                                                                  │
│  ▼ Playback Control                                                              │
│                                                                                  │
│  ctx.audio:stop(voice_handle)            -- stop specific voice                  │
│  ctx.audio:stop_all()                    -- stop all voices                      │
│  ctx.audio:stop_bus(bus_name)            -- stop all voices on a bus             │
│  ctx.audio:pause(voice_handle)                                                   │
│  ctx.audio:resume(voice_handle)                                                  │
│                                                                                  │
│  ▼ Bus Volume Control                                                            │
│                                                                                  │
│  ctx.audio:set_bus_volume("SFX", 0.8)                                           │
│  ctx.audio:set_bus_volume("Music", 0.5)                                          │
│  ctx.audio:set_bus_volume("Voice", 1.0)                                          │
│  ctx.audio:get_bus_volume("Music")       -- returns float                        │
│                                                                                  │
│  ▼ Adaptive Music                                                                │
│                                                                                  │
│  ctx.audio:set_music_intensity(value)         -- value: 0.0–1.0, instant        │
│  ctx.audio:set_music_intensity(value, fade_s) -- value: 0.0–1.0, fade over N s  │
│  ctx.audio:get_music_intensity()              -- returns float                   │
│  ctx.audio:trigger_stinger("HitStinger")      -- fire named stinger track        │
│  ctx.audio:stop_music(fade_s)                 -- fade out all music tracks       │
│                                                                                  │
│  ▼ Mixer Snapshots                                                               │
│                                                                                  │
│  ctx.audio:apply_snapshot("Combat")           -- instant switch                  │
│  ctx.audio:apply_snapshot("Menu", 1.5)        -- crossfade over 1.5 seconds      │
│  ctx.audio:get_snapshot()                     -- returns current snapshot name   │
│                                                                                  │
│  ▼ Node Audio Methods (CAudioSource required)                                    │
│                                                                                  │
│  self:play_audio()                                                               │
│  self:stop_audio()                                                               │
│  self:pause_audio()                                                              │
│  self:set_volume(0.8)       -- 0.0–1.0                                          │
│  self:set_pitch(1.2)        -- 0.5–2.0                                          │
│  self:is_audio_playing()    -- returns bool                                      │
│  self:get_audio_duration()  -- returns milliseconds (float)                     │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### 7.3 Full API Reference

#### One-Shot Playback

```lua
-- Basic 2D playback
local h = ctx.audio:play("res://audio/sfx/click.wav", 0.8, 1.0)

-- Positional 3D playback
local h = ctx.audio:play_at("res://audio/sfx/explosion.wav",
    Vec3(10.0, 0.0, -5.0), 1.0, 50.0)

-- Bus-routed playback
local h = ctx.audio:play_on_bus("res://audio/music/jingle.ogg", "Music", 0.7, 1.0)

-- Sound cue playback (with Switch/Branch parameters)
local h = ctx.audio:play_cue("res://audio/cues/footstep.uzcue", {
    surface = "concrete",   -- used by Switch node
    indoor  = true          -- used by Branch node
})
```

#### Playback Control

```lua
ctx.audio:stop(handle)             -- stop one voice
ctx.audio:stop_all()               -- stop all voices immediately
ctx.audio:stop_bus("SFX")          -- stop all SFX bus voices
ctx.audio:pause(handle)            -- pause voice; keeps buffer position
ctx.audio:resume(handle)           -- resume from paused position

-- Query voice state
local playing = ctx.audio:is_playing(handle)    -- bool
local pos_ms  = ctx.audio:get_position(handle)  -- milliseconds elapsed
```

#### Bus Control

```lua
ctx.audio:set_bus_volume("SFX",   0.8)   -- 0.0 = silent, 1.0 = full
ctx.audio:set_bus_volume("Music", 0.5)
ctx.audio:set_bus_volume("Voice", 1.0)
ctx.audio:set_bus_volume("Master", 0.9)

local vol = ctx.audio:get_bus_volume("SFX")  -- returns 0.0–1.0

ctx.audio:mute_bus("Music", true)   -- true = mute, false = unmute
ctx.audio:solo_bus("SFX", true)     -- solo (unmutes this, mutes others)
```

#### Adaptive Music

```lua
-- Set intensity (0.0 = calm, 1.0 = full combat)
ctx.audio:set_music_intensity(0.0)         -- instant
ctx.audio:set_music_intensity(0.85, 2.0)   -- 2-second fade

local v = ctx.audio:get_music_intensity()  -- current float value

-- Stingers
ctx.audio:trigger_stinger("HitStinger")

-- Sequence control
ctx.audio:stop_music()         -- instant stop
ctx.audio:stop_music(1.5)      -- fade out over 1.5 seconds
```

#### Mixer Snapshots

```lua
ctx.audio:apply_snapshot("Combat")        -- instant switch
ctx.audio:apply_snapshot("Underwater", 2.0) -- 2-second crossfade
local name = ctx.audio:get_snapshot()    -- "Combat"
```

#### Node Methods (requires `CAudioSource` component)

```lua
function Node:on_ready()
    self:play_audio()
end

function Node:on_trigger_exit()
    self:stop_audio()
end

self:set_volume(0.6)          -- per-voice volume override (multiplied with bus)
self:set_pitch(1.2)           -- pitch shift (0.5 = half speed, 2.0 = double)
local b = self:is_audio_playing()
local ms = self:get_audio_duration()  -- total clip duration in ms
```

---

## 8. File Formats

### 8.1 `.uzaudio` — Compiled Audio Asset

Binary format. Created by the import pipeline from `.wav`, `.ogg`, or `.mp3` sources. Contains the PCM data plus import metadata. Described in full in `docs/specs/audio-system.md`.

Import settings sidecar (plain text, lives next to source in `res://`):

```ini
; res://audio/sfx/door_slam.wav.import
[import version=1]
normalize     = false
trim_silence  = false
trim_threshold_db = -60.0
channels      = stereo       ; stereo | mono | source
sample_rate   = 44100        ; 44100 | 48000 | 22050 | source
fade_in_ms    = 0
fade_out_ms   = 0
loop          = false
loop_start_sample = 0
loop_end_sample   = 0        ; 0 = end of file
streaming     = auto         ; auto | always | never
```

### 8.2 `.uzcue` — Sound Cue Graph

Plain-text INI-like. Full format described in Section 3.4.

Key rules:
- Node IDs are stable integers; never reused within a file
- `parent` attribute on each node defines graph edges (not tree parent)
- The single `Output` node has no `parent` attribute (it is the root)
- `type=Random` with no `weights` uses uniform distribution
- Parameters used by `Switch`/`Branch` nodes are arbitrary string keys resolved from the Lua `params` table passed to `play_cue()`

### 8.3 `.uzmix` — Mixer Snapshot

Plain-text INI-like. Saved from the Snapshot Panel in the Audio Mixer.

```ini
; res://audio/snapshots/combat.uzmix
[uzmix version=1 name="Combat"]

[bus name=Master]
volume_db = 0.0
muted     = false

[bus name=SFX]
volume_db = -2.0
muted     = false
eq_low_hz    = 80
eq_low_gain  = -1.5
eq_mid_hz    = 1000
eq_mid_gain  = 1.0
eq_high_hz   = 8000
eq_high_gain = 0.0
compressor_threshold_db = -18.0
compressor_ratio        = 4.0
compressor_attack_ms    = 10.0
compressor_release_ms   = 100.0
reverb_send = 0.15

[bus name=Music]
volume_db = -6.0
muted     = false
eq_low_hz    = 80
eq_low_gain  = 2.0
eq_mid_hz    = 800
eq_mid_gain  = -1.0
eq_high_hz   = 8000
eq_high_gain = 0.0
compressor_threshold_db = -12.0
compressor_ratio        = 2.0
compressor_attack_ms    = 20.0
compressor_release_ms   = 200.0
reverb_send = 0.0

[bus name=Voice]
volume_db = 0.0
muted     = false
reverb_send = 0.3

[reverb]
preset    = MediumRoom
room_size = 0.45
damping   = 0.55
mix       = 0.25
```

### 8.4 `.uzmusic` — Adaptive Music Definition

Plain-text INI-like. Full format described in Section 5.6.

Key rules:
- Track IDs stable integers; order in file is display order in sequencer
- `loop_start_bar` / `loop_end_bar` are in musical bars (1-indexed from 0)
- Conditional markers use simple comparison expressions: `intensity > 0.7`, `intensity < 0.3`
- `next` and `jump_to` on markers reference other marker `name` values
- BPM and time signature are global; mixed time signatures are not supported

---

## 9. Import Pipeline

### 9.1 Supported Source Formats

| Format | Decoder | Typical Use |
|--------|---------|-------------|
| `.wav` | Windows built-in / stb_vorbis | SFX, master recordings |
| `.ogg` | stb_vorbis | Ambient loops, streaming music |
| `.mp3` | libmpg123 | Music tracks, voice dialogue |

All sources decode to interleaved PCM (16-bit integer or 32-bit float) at import time.

### 9.2 Import Decision Tree

```
Source file detected (file watcher or explicit reimport):
    │
    ├── Decode header to get duration / size
    │
    ├── decoded_size = frame_count × channels × bytes_per_sample
    │
    ├── if decoded_size < 2MB OR import setting streaming=never:
    │       → Decode fully to RAM
    │       → Write to .uzaudio with Streaming=0
    │       → Asset registry: type=decoded
    │
    └── if decoded_size >= 2MB OR import setting streaming=always:
            → Phase 3: store OGG compressed, stream from disk
            → Write to .uzaudio with Streaming=1 (Phase 3 only)
            → Phase 2: fall back to full decode with warning
```

### 9.3 Import Processor Pipeline (per asset)

```
1. Decode source (WAV / OGG / MP3) → interleaved float32 PCM
2. Apply import settings:
   a. Normalize:         peak_gain = 1.0 / max(abs(samples)); scale all samples
   b. Trim silence:      scan from start/end, trim samples below threshold_db
   c. Resample:          if target_rate != source_rate, use linear/sinc resampler
   d. Channel convert:   if mono requested, average L+R channels
   e. Fade in:           apply linear ramp over first fade_in_ms milliseconds
   f. Fade out:          apply linear ramp over last fade_out_ms milliseconds
   g. Loop crossfade:    if loop=true, blend tail into head over 50 samples
3. Write .uzaudio binary (header + processed PCM)
4. Update asset registry entry { path, last_modified, uzaudio_path }
5. Notify game.dll via GameContext callback: asset_hot_reloaded(clip_id)
```

### 9.4 Import Progress UI

Large files (>10s audio) show import progress in the Asset Browser status bar:

```
Importing: res://audio/music/main_theme.ogg  [══════════════░░░░░░]  68%
```

Import runs on a background thread. The editor remains responsive. Hot-reload applies when import completes.

### 9.5 Batch Import

Dragging multiple audio files into the Asset Browser triggers batch import. Each file processes sequentially (to avoid thread contention on the disk). Progress is shown as `N/M files imported`.

---

## 10. Implementation Notes

### 10.1 C++ Class Hierarchy

```
uz_engine.lib/audio/
├── audio_engine.h/.cpp          — AudioEngine: XAudio2 lifecycle, bus management
│     AudioEngine
│       ::Init()                 — XAudio2Create, CreateMasteringVoice, create 4 buses
│       ::Shutdown()             — StopAll, DestroyVoices, Release
│       ::Update(dt)             — per-frame: 3D DSP update, intensity ramp, VU metering
│       ::PlayCue(cue, params)   — evaluate .uzcue graph → acquire voice → submit buffer
│       ::Play2D(clip, bus, vol) — acquire voice → submit buffer → route to bus
│       ::Play3D(clip, pos, ...) — Play2D + SetOutputMatrix via X3DAudio
│       ::SetBusVolume(bus, vol) — IXAudio2SubmixVoice::SetVolume
│       ::ApplySnapshot(name, t) — interpolate all bus params toward snapshot state
│       ::SetMusicIntensity(v,t) — ramp m_intensity, MusicSequencer picks up next frame
│
├── audio_voice_pool.h/.cpp      — fixed 64-voice pool, priority eviction
│     AudioVoicePool
│       ::Init(xaudio2)          — CreateSourceVoice × 64 (fixed, no growth)
│       ::Acquire(fmt, priority) — find idle voice; evict lowest-priority if full
│       ::Release(voice_id)      — mark slot idle (called by voice callback OnStreamEnd)
│       ::SubmitBuffer(id, buf)  — SubmitSourceBuffer + Start
│
├── audio_bus.h/.cpp             — bus state, EQ/compressor XAPO parameters
│     AudioBus
│       ::SetVolume(db)
│       ::SetEQ(band, freq, gain)
│       ::SetCompressor(thr, ratio, atk, rel)
│       ::SetReverbSend(amount)
│       ::Mute(bool) / Solo(bool)
│       ::Serialize() / Deserialize()   — for .uzmix snapshots
│
├── audio_cue.h/.cpp             — .uzcue graph loader and evaluator
│     AudioCueGraph
│       ::Load(path)             — parse INI, build node array
│       ::Evaluate(params)       — stack-based traversal → ResolvedClip
│     ResolvedClip { clip_id, volume, pitch, bus, spatial, priority }
│
├── music_sequencer.h/.cpp       — adaptive music runtime
│     MusicSequencer
│       ::Load(path)             — parse .uzmusic
│       ::Play(from_bar)         — schedule all Base tracks, start loop
│       ::Update(dt, intensity)  — ramp Additive track volumes, evaluate markers
│       ::TriggerStinger(name)   — one-shot voice for stinger track
│       ::Stop(fade_s)           — fade all music voices
│
├── audio_clip.h/.cpp            — loaded .uzaudio asset (PCM buffer)
│     AudioClip
│       ::LoadFromFile(path)     — mmap or read .uzaudio binary
│       ::SwapBuffer(new_data)   — atomic buffer swap for hot-reload
│
├── spatial_audio.h/.cpp         — X3DAudio per-frame DSP (Phase 3)
│     SpatialAudioSystem
│       ::UpdateListener(pos, fwd, up)
│       ::UpdateEmitter(voice_id, pos)
│       ::Calculate(emitter, listener) → X3DAUDIO_DSP_SETTINGS
│
└── audio_import.h/.cpp          — source → .uzaudio import pipeline
      AudioImporter
        ::Import(src_path, settings) — decode + process + write .uzaudio
        ::DetectStreaming(path)      — auto-decide RAM vs streaming

game.dll / GameContext
├── audio callbacks in GameContext struct (C ABI):
│     void (*audio_play_2d)(const char* clip_id, float vol, float pitch, uint8_t bus);
│     void (*audio_play_3d)(const char* clip_id, float x, float y, float z,
│                           float vol, float max_dist, uint8_t bus);
│     void (*audio_stop)(uint32_t voice_handle);
│     void (*audio_stop_all)();
│     void (*audio_set_bus_volume)(const char* bus, float vol);
│     void (*audio_play_cue)(const char* cue_path, const char* params_json);
│     void (*audio_set_music_intensity)(float value, float fade_s);
│     void (*audio_apply_snapshot)(const char* name, float fade_s);
│
└── lua_binding.cpp
      — Registers ctx.audio Lua table with all API methods above
      — self:play_audio() / stop_audio() / set_volume() / etc.

VSCode Extension (TypeScript/React)
├── AudioMixerPanel.tsx          — React WebView, WebSocket VU meter updates
├── SoundCueEditor.tsx           — node graph canvas (React Flow or custom canvas)
├── MusicSequencerPanel.tsx      — horizontal timeline, BPM grid, layer lanes
├── WaveformEditor.tsx           — SVG waveform render, trim/loop handle drag
└── AudioApiRefPanel.tsx         — searchable Lua API cheat sheet
```

### 10.2 XAudio2 Effect Chain Setup

```cpp
// Per-bus effect chain: [EQ Biquad XAPO] → [Compressor XAPO] → submix
XAUDIO2_EFFECT_DESCRIPTOR effects[2] = {};
effects[0].pEffect         = eq_xapo;           // UzEqXAPO (3-band biquad)
effects[0].InitialState    = TRUE;
effects[0].OutputChannels  = 2;
effects[1].pEffect         = compressor_xapo;   // UzCompressorXAPO
effects[1].InitialState    = TRUE;
effects[1].OutputChannels  = 2;

XAUDIO2_EFFECT_CHAIN chain = { 2, effects };
m_buses[BUS_SFX]->SetEffectChain(&chain);
```

Effect parameter updates via `IXAudio2Voice::SetEffectParameters()` are lock-free and apply within one audio frame. No allocation at parameter update time.

### 10.3 VU Metering

VU meters use `IXAudio2SourceVoice::GetState()` called from the main thread at 60 Hz. Peak hold uses a per-channel max accumulator with a 2-second decay timer. RMS is computed over the last 50ms of PCM samples cached in a ring buffer per voice.

```cpp
struct VUMeterState {
    float peak_l, peak_r;          // current peak (linear)
    float rms_l, rms_r;            // RMS over last 50ms
    float held_peak_l, held_peak_r; // peak hold (2s decay)
    bool  clipped_l, clipped_r;    // latching clip indicator
};
```

The WebView receives VU data via a 60 Hz WebSocket push (JSON array, one entry per bus):

```json
{
  "type": "vu_update",
  "buses": [
    { "name": "SFX",   "peak_l": 0.62, "peak_r": 0.58, "rms_l": 0.41, "rms_r": 0.39, "clipped": false },
    { "name": "Music", "peak_l": 0.45, "peak_r": 0.45, "rms_l": 0.30, "rms_r": 0.30, "clipped": false },
    { "name": "Voice", "peak_l": 0.10, "peak_r": 0.10, "rms_l": 0.06, "rms_r": 0.06, "clipped": false },
    { "name": "Master","peak_l": 0.70, "peak_r": 0.68, "rms_l": 0.50, "rms_r": 0.49, "clipped": false }
  ]
}
```

### 10.4 React WebView Architecture

The VSCode extension hosts audio panels as WebView panels communicating over `vscode.postMessage`:

```
VSCode Extension Host (TypeScript)
    ↕ postMessage / onDidReceiveMessage
WebView (React)
    ↕ WebSocket (ws://localhost:PORT/audio)
render.exe IPC server
    ↕ function call (in-process)
XAudio2 audio engine (uz_engine.lib)
```

- **Fader drag**: React `onMouseMove` → `postMessage({type:'set_fader', bus, vol})` → extension host → WebSocket → IPC handler → `SetVolume()`
- **VU update**: XAudio2 metering thread → WebSocket push → React state update → SVG bargraph re-render

### 10.5 Thread Safety

| Thread | Responsibilities | Sync Mechanism |
|--------|-----------------|---------------|
| Main/Game thread | Lua calls, ECS update, voice acquire/release | Mutex on voice pool state |
| XAudio2 callback thread | PCM decode, buffer submission callbacks | Lock-free ring buffer |
| Import thread | Source decode → .uzaudio write | Separate arena, atomic swap on completion |
| VU metering thread | `GetState()` poll, RMS accumulation | Atomic reads of voice state |
| WebSocket thread | IPC message dispatch | Queue with mutex |

### 10.6 Testing Strategy

Audio testing uses Google Mock to implement `IXAudio2Mock` (a mock of the XAudio2 COM interface). This avoids requiring a real audio device in CI.

```cpp
// test_audio_bus.cpp
class MockXAudio2SubmixVoice : public IXAudio2SubmixVoice {
    MOCK_METHOD(HRESULT, SetVolume, (float Volume, UINT32 OperationSet), (override));
    MOCK_METHOD(void, GetVolume, (float* pVolume), (override));
    // ... all IXAudio2Voice methods
};

TEST(AudioBusTest, SetVolumeClampedToRange) {
    MockXAudio2SubmixVoice mock_voice;
    EXPECT_CALL(mock_voice, SetVolume(0.0f, XAUDIO2_COMMIT_NOW)).Times(1);
    AudioBus bus(&mock_voice);
    bus.SetVolume(-99.0f);  // should clamp to 0.0 (linear silence)
}
```

Coverage target: 95% of `uz_engine.lib/audio/` code paths. The mock interface enables unit testing of all bus operations, voice pool logic, and cue graph evaluation without audio hardware.

---

## 11. Phase Rollout

| Feature | Phase | Depends On |
|---------|-------|-----------|
| Audio Mixer WebView (faders, mute, solo) | Phase 2 | XAudio2 bus system |
| .uzmix snapshot save/load | Phase 2 | Audio Mixer |
| Waveform Editor (trim, loop, normalize) | Phase 2 | .uzaudio import pipeline |
| Lua API Reference Panel | Phase 2 | Lua audio API |
| Sound Cue Editor (node graph) | Phase 2 | .uzcue format |
| EQ + Compressor XAPO per bus | Phase 2 | AudioBus effect chain |
| VU metering (60 Hz WebSocket push) | Phase 2 | Audio Mixer |
| 3D Audio Placement Tool (gizmos) | Phase 3 | X3DAudio, Jolt (occlusion) |
| Reverb Zone placement | Phase 3 | X3DAudio reverb |
| Music Sequencer + .uzmusic | Phase 3 | MusicSequencer C++ class |
| Adaptive intensity system | Phase 3 | Music Sequencer |
| Streaming audio support | Phase 3 | .uzaudio streaming flag |

---

## 12. Open Questions & Decisions Deferred

| Question | Current Decision | Revisit When |
|----------|-----------------|-------------|
| EQ implementation: built-in XAPO vs custom biquad | Custom `UzEqXAPO` using `CXAPOBase` for full control | Phase 2 implementation start |
| Reverb: per-source vs shared aux bus | Shared Reverb FX aux bus with per-strip send amount | Phase 3 X3DAudio integration |
| Compressor: XAPO vs post-processing gain ride | Custom `UzCompressorXAPO` (simpler, no lookahead in Phase 2) | Phase 3 if quality insufficient |
| OGG streaming: stb_vorbis vs libvorbis | stb_vorbis (already used for import decode, single-header) | Phase 3 streaming implementation |
| Music intensity: per-track threshold vs curve | Per-track threshold scalar (simpler, predictable) | Phase 3 if designers need more control |
| Cross-platform audio (Linux/Mac) | OpenAL Soft abstraction layer (not specced here) | Phase 4+ when Linux port begins |
