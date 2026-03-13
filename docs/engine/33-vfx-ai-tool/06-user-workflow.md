# VFX AI Tool — User Experience & Workflow

> **Status**: Planned (Phase 3–4)
> **Source**: `editor/panels/vfx-ai-tool/`, VSCode UI integration
> **References**: VSCode webview API, UzEngine editor framework, IPC protocol

## Purpose

This document describes the complete user experience — how artists and designers interact with the VFX AI Tool from input to final saved effect. The workflow emphasizes speed, real-time iteration, and accessibility for users without VFX expertise.

## User Interface Layout

### Main VFX AI Tool Panel (Sidebar)

Located in VSCode left sidebar or Engine Mode toolbar. Upon opening:

```
┌─────────────────────────────────────────────────────────┐
│  VFX AI Tool                                    [↗ ⋯]  │
├─────────────────────────────────────────────────────────┤
│                                                          │
│  Create a VFX Effect                                    │
│  ═════════════════════════════════════════════════════  │
│                                                          │
│  ┌─ Choose Method ────────────────────────────────────┐ │
│  │ ◉ Describe (Natural Language)   ○ Use Template     │ │
│  └────────────────────────────────────────────────────┘ │
│                                                          │
│  ┌─ Description Input ────────────────────────────────┐ │
│  │ What kind of VFX would you like?                   │ │
│  │                                                    │ │
│  │ ┌──────────────────────────────────────────────┐  │ │
│  │ │ [Enter description or select template...]     │  │ │
│  │ │                                               │  │ │
│  │ │                                               │  │ │
│  │ └──────────────────────────────────────────────┘  │ │
│  └────────────────────────────────────────────────────┘ │
│                                                          │
│  ┌─ Configuration (Optional) ─────────────────────────┐ │
│  │ Intensity:        [████████░░] 1.0 (0.5 – 2.0)     │ │
│  │ Particle Count:   [██████░░░░] 500 (100 – 2000)    │ │
│  │ Duration:         [███████░░░] 3.0s (0.5 – 10s)    │ │
│  │ Style:            [Fire   ▼]                       │ │
│  └────────────────────────────────────────────────────┘ │
│                                                          │
│  ┌─────────────────────────────────────────────────────┐ │
│  │         [✨ Generate Effect]  [📋 Gallery]          │ │
│  └─────────────────────────────────────────────────────┘ │
│                                                          │
│ ┌─ Generation Progress ──────────────────────────────┐  │
│ │ Status: Generating shaders...              30%    │  │
│ │ ├─ [✓] Parsed description (1.2s)                  │  │
│ │ ├─ [⏳] Generating shader... (2.5s)               │  │
│ │ ├─ [ ] Generating texture...                      │  │
│ │ ├─ [ ] Generating mesh...                         │  │
│ │ └─ [ ] Validating...                              │  │
│ │ Time elapsed: 4.3s / Est. 12s remaining            │  │
│ └─────────────────────────────────────────────────────┘ │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

### Viewport Preview (Bottom Right)

Once generation completes, a preview panel shows:

```
┌────────────────────────────────────────────────┐
│ VFX Preview: bright_blue_explosion      [×]   │
├────────────────────────────────────────────────┤
│                                                │
│  ┌──────────────────────────────────────────┐ │
│  │                                          │ │
│  │          [Blue energy effect]            │ │
│  │          (Animating...)                  │ │
│  │                                          │ │
│  │          [Play] [⏸ Stop] [↻ Loop]        │ │
│  │          Speed: [█████░░░] 1.0x          │ │
│  │                                          │ │
│  │          Camera: [Free Orbit] [Top]      │ │
│  │                                          │ │
│  └──────────────────────────────────────────┘ │
│                                                │
│  ┌─ Effect Parameters ──────────────────────┐ │
│  │ Intensity:    [████████░░] 1.0           │ │
│  │ Color Tint:   [████ Blue] (custom)       │ │
│  │ Emit Rate:    [████████░░] 500/sec       │ │
│  │ Spawn Spread: [██████░░░░] 45°           │ │
│  │                                          │ │
│  │ [Feedback] "Make it bigger"              │ │
│  │ [Regenerate] [Save] [Export]             │ │
│  └──────────────────────────────────────────┘ │
└────────────────────────────────────────────────┘
```

## Step-by-Step Workflow

### Step 1: Open VFX AI Tool

**Action**: Click "VFX AI Tool" in VSCode sidebar or Engine Mode toolbar, or use command palette (`VFX AI: Create Effect`).

**Result**: Panel opens with input interface ready.

### Step 2a: Describe the Effect (Natural Language)

**Option A — Custom Description**:

User types natural language description:
```
"bright blue energy explosion with electric crackling,
 expanding shockwave, slow falling sparks"
```

**Option B — Select Template**:

Click "Use Template" dropdown, choose from:
- Fire Ball
- Explosion (large)
- Magic Attack (blue)
- Smoke Cloud
- Electric Beam
- Holy Light
- Water Splash
- Ice Burst
- Custom

### Step 3: Configure (Optional)

Slider adjustments for generation parameters:

| Parameter | Range | Default | Purpose |
|-----------|-------|---------|---------|
| **Intensity** | 0.5–2.0 | 1.0 | Brightness/strength |
| **Particle Count** | 100–2000 | 500 | Emission rate scale |
| **Duration** | 0.5–10s | 3.0s | Particle lifespan |
| **Style** | Dropdown | Auto-detect | Fire, Electric, Magic, Water, Ice |

### Step 4: Generate

Click **[✨ Generate Effect]**.

**What happens**:

1. **Parsing** (1–2 sec) — Claude parses description into VFX parameters
2. **Shader Generation** (2–5 sec) — Claude generates HLSL, DXC compiles, validates
3. **Texture Generation** (2–5 sec) — ComfyUI generates particle textures (parallel)
4. **Mesh Generation** (15–20 sec) — TRELLIS.2 if needed (parallel, optional)
5. **Assembly** (1 sec) — Combines components into `.uzvfx` definition
6. **Import** (1–2 sec) — Textures/shaders imported to VFS, validated
7. **Preview** (instant) — Effect hot-loads into viewport

**Total time**: 6–12 sec (typical), 20–30 sec (with mesh generation)

**Progress feedback**: Real-time status bar shows which stage is active, elapsed time, and ETA.

### Step 5: Review in Viewport

Effect appears in preview panel, playing in a loop:

```
User sees:
  ✓ Live animation of generated effect
  ✓ Particle count and movement
  ✓ Colors and intensity
  ✓ Shader quality (no visual artifacts)
```

**Controls**:
- **Play/Stop**: Pause animation for inspection
- **Loop**: Loop animation continuously
- **Speed slider**: Slow down/speed up for analysis
- **Camera controls**: Orbit, zoom, look around

### Step 6: Iterate with Feedback

User can refine effect with natural language feedback:

```
Examples of feedback:
  • "Make it bigger"
  • "More blue, less purple"
  • "Slower falling particles"
  • "Add more glow"
  • "Less intense, more subtle"
  • "Add smoke"
  • "Faster spawning"
  • "More spread angle"
```

**User types feedback** in text box at bottom of preview panel.

**Click [Regenerate]** or press Enter.

**System reprocesses**:
- Claude parses feedback + current parameters
- Computes delta (what changed)
- **Regenerates only changed components** (fast path)
  - If feedback affects shader: regenerate shader only
  - If feedback affects color: regenerate texture only
  - Both: regenerate both (parallel)
  - Neither: just tweak parameters (instant)
- Updates preview in real-time (2–5 sec typical)

**Iteration loop continues** until user is satisfied.

### Step 7: Save Effect to Project

Once happy with preview, user clicks **[Save]**.

**Dialog**:
```
┌──────────────────────────────────────────┐
│  Save VFX Effect                          │
├──────────────────────────────────────────┤
│                                          │
│ Effect Name:                             │
│ [bright_blue_explosion_v1_________]      │
│                                          │
│ Save Location:                           │
│ [res://vfx/effects/________▼]            │
│                                          │
│ Tags (optional):                         │
│ [magic, energy, blue, explosion___]      │
│                                          │
│       [Cancel]  [Save]  [Save + Remix]   │
└──────────────────────────────────────────┘
```

**User enters**:
- Effect name (auto-generated, editable)
- Save location in project (defaults to `res://vfx/effects/`)
- Optional tags for gallery organization

**Click [Save]**.

**Result**:
- `.uzvfx` definition file saved
- Textures copied to project (deduplicated by hash)
- Shaders copied to project
- Registered in asset browser and editor
- **Effect is now a permanent project asset**, usable in scenes via ParticleSystem component

### Step 8: Advanced — Multi-Emitter Composition

For complex effects, user can combine multiple effects:

**Option A — From Description**:
```
"fire + smoke + sparks explosion"
```
System auto-creates 3 emitters (fire, smoke, sparks) with appropriate parameters.

**Option B — Remix Gallery**:
1. Open **[📋 Gallery]** tab
2. Browse previously generated effects
3. Select two effects: `fire_explosion` + `smoke_cloud`
4. Click **[Compose]**
5. System creates multi-emitter `.uzvfx` combining both
6. Adjust blend/overlap, save as new effect

## Advanced Use Cases

### Use Case 1: Rapid Prototyping

**Scenario**: Game designer needs 20 VFX for a new level in 2 hours.

1. Designer opens VFX AI Tool
2. For each effect (e.g., "fire trap", "electric gate", "magic barrier"):
   - Types description (30 sec)
   - Waits for generation (10 sec)
   - Reviews preview (20 sec)
   - One iteration feedback if needed (10 sec)
   - Saves (5 sec)
3. Total per effect: ~75 sec
4. 20 effects: ~25 minutes
5. Manual VFX authoring would take 10–20 hours

**Result**: 25x faster VFX creation via AI.

### Use Case 2: AI Chat Integration (Claude Code/Cursor)

Developer using Claude Code or Cursor AI can generate VFX in context:

```
Developer: "I'm building a fire trap. Generate a VFX effect
            for the fire trigger, and show me how to use it
            in Lua."

Claude Code:
  1. Generates VFX description: "bright orange fire trap,
     intense heat, vertical flames, looping"
  2. Calls VFX AI Tool via IPC
  3. Returns generated effect name: "fire_trap_v1"
  4. Shows Lua code example:
     ```lua
     local fire_trap = ctx.vfx:spawn("fire_trap_v1", position)
     fire_trap:set_parameter("intensity", 1.5)
     ```
```

**Result**: VFX generation integrated into AI coding workflow.

### Use Case 3: Batch Generation During Cooking

During asset cooking (`uzbuild`), system can batch-generate effects:

```bash
# Content authors list effects in a .json file:
{
  "effects": [
    { "name": "fire_trap", "description": "..." },
    { "name": "poison_gas", "description": "..." },
    { "name": "holy_burst", "description": "..." }
  ]
}

# Run:
$ uzbuild --vfx-cook effects.json

# Output:
Generated 3 effects in 35 seconds ($0.05 total cost)
Cached in .uzpak, ready for shipping build
```

## Gallery & Effect Management

### Gallery Panel

**Access**: [📋 Gallery] tab in VFX AI Tool panel.

```
┌────────────────────────────────────────────┐
│ VFX Gallery                   [Sort ▼] [⊕] │
├────────────────────────────────────────────┤
│                                            │
│  [Search: "fire" ______________]  [Clear]  │
│                                            │
│  Filters: [All] [My Generated] [Favorites] │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │ [Thumbnail]  fire_explosion_v1       │ │
│  │ Generated: 2026-03-12                │ │
│  │ Tags: fire, explosion, magic         │ │
│  │ Used: 3 scenes                       │ │
│  │ [Preview] [Edit] [Remix] [Delete]    │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ┌──────────────────────────────────────┐ │
│  │ [Thumbnail]  electric_beam_v2        │ │
│  │ Generated: 2026-03-10                │ │
│  │ Tags: electric, beam, magic          │ │
│  │ Used: 7 scenes                       │ │
│  │ [Preview] [Edit] [Remix] [Delete]    │ │
│  └──────────────────────────────────────┘ │
│                                            │
│  ... (20+ more effects)                    │
│                                            │
└────────────────────────────────────────────┘
```

**Actions**:
- **Preview**: Play effect in viewport
- **Edit**: Re-open in generation panel for tweaks
- **Remix**: Combine with another effect
- **Delete**: Remove from project
- **Export**: Export as standalone `.uzpak`

## Quality Assurance

### Automatic Validation

Before saving, system validates:

1. ✓ Shader compiles without errors (DXC)
2. ✓ All textures imported successfully
3. ✓ `.uzvfx` schema is valid JSON
4. ✓ Effect renders without visual artifacts in preview
5. ✓ Particle count is reasonable (won't crash)

If validation fails:
- Error message shown with details
- Suggestion to regenerate or adjust parameters
- Option to edit manually or retry

### Manual Inspection

User can manually inspect generated assets:

- **Shader**: [View HLSL] button shows generated source
- **Texture**: [View Texture] shows generated PNG
- **Definition**: [View JSON] shows `.uzvfx` structure

Enables power users to tweak manually if desired.

## Keyboard Shortcuts

| Shortcut | Action |
|----------|--------|
| **Ctrl+Shift+E** | Open VFX AI Tool panel |
| **Enter** | Generate or regenerate effect |
| **Ctrl+Shift+S** | Save current effect |
| **Ctrl+Z** | Undo effect generation (restore previous) |
| **R** | Regenerate with feedback |
| **P** | Play/pause preview |
| **L** | Toggle loop |
| **Space** | Play/pause (same as P) |

## Settings & Preferences

Located in VSCode Settings, section `uzengine.vfx-ai`:

```json
{
  "uzengine.vfx-ai.enabled": true,
  "uzengine.vfx-ai.comfyui_url": "http://localhost:8188",
  "uzengine.vfx-ai.trellis2_url": "http://localhost:8889",
  "uzengine.vfx-ai.auto_preview": true,
  "uzengine.vfx-ai.use_sonnet": true,
  "uzengine.vfx-ai.use_opus_threshold": 0.5,  // Complexity score for Opus fallback
  "uzengine.vfx-ai.max_generation_time": 60,  // Seconds, abort if exceeded
  "uzengine.vfx-ai.cache_textures": true,
  "uzengine.vfx-ai.fallback_to_procedural": true,
  "uzengine.vfx-ai.save_generated_code": true,  // Save shader/definition for inspection
}
```

**Defaults**: All services enabled, auto-preview on, Sonnet + Opus fallback.

## Limitations & Graceful Fallback

**No ComfyUI running**: Uses procedural fallback textures (pre-baked atlas).
**No TRELLIS.2**: Uses primitive meshes (spheres, cubes) for mesh effects.
**Claude API down**: Shows error, suggests manual editing or retry later.
**Generation timeout**: Aborts and offers manual retry.

**Invariant**: Effect always generates (with fallbacks), never crashes or fails silently.

## Related Documentation

- `docs/engine/33-vfx-ai-tool/01-overview.md` — Overall VFX AI system
- `docs/engine/33-vfx-ai-tool/02-shader-generation.md` — Shader generation
- `docs/engine/33-vfx-ai-tool/03-texture-generation.md` — Texture generation
- `docs/engine/40-editor-framework/13-vfx-panel/` — VFX editor panel details
- `docs/engine/25-vfx/` — VFX system, usage from Lua/C++
