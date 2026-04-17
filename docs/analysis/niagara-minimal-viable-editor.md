# Niagara Minimal-Viable In-Engine Visual Editor Analysis

**Date:** 2026-04-16  
**Focus:** What a minimal-but-complete Niagara-parity VFX visual editor needs, mapped to existing UzEngine foundation.

---

## Section 1: Niagara Editor Feature Map

### 1. Emitter Stack Panel (SNiagaraStack / UNiagaraStack)

**What it does:** Vertical scrolling panel showing 7 fixed stack entries (System Spawn, System Update, Emitter Spawn, Emitter Update, Particle Spawn, Particle Update, Render). Within each stack entry, users see draggable module cards (e.g., "InitialVelocity", "ColorOverLife"). Modules can be added (+ button), removed (X button), reordered (drag-within-stack), and toggled enabled/disabled (checkbox).

**Core data model:**
- `FNiagaraStackEntry`: struct with ID, display name, ownership (system vs emitter vs particle), module array
- Module array order = execution order; preserved across save
- `FNiagaraStackModuleData`: module reference, override status, enabled flag

**Niagara equivalent:** `UNiagaraStack::StackEntry` + `UNiagaraScript::ModuleScript` linked list (ordered by compile unit, maintained by `SNiagaraStack::RefreshStackView()`)

**What we need:** Stack entries already implicit in ParticleEmitter (fields like `velocityModule`, `colorModule` etc.). Need: explicit stack ordering UI + drag-to-reorder + add/remove context menu per stack entry.

---

### 2. Parameters Panel (SNiagaraParameterPanel / UNiagaraParameterDefinitions)

**What it does:** Two-pane UI. Left: user-defined parameter list (System-scope params like "NumEmitters", "SpawnRate"; Emitter-scope like "Color", "Lifetime"; Particle-scope like "Mass"). Right: inspector for selected parameter (name, type dropdown, default value editor, tooltip). Parameters are referenced in module expressions (e.g., `@System.SpawnRate` in spawn rate curve).

**Core data model:**
- `FNiagaraParameterDefinition`: array of {name, scope, type, default value, metadata}
- Parameters stored per-object (UNiagaraSystem, UNiagaraEmitter)
- Module expressions use parameter references (e.g., `SpawnRate: @System.MySpawnRate`)

**What we need:** Extend ParticleEmitter with explicit parameter dictionary. Add parameter reference resolver (string `@ParamName` → actual float/vec3/etc). Expose param UI in editor overlay (add/edit/delete params, see usage sites).

---

### 3. Curves Editor (SNiagaraCurveOverview / SCurveEditor)

**What it does:** Modal showing gradient curves for color-over-life, size-over-life, opacity-over-life. X-axis = normalized particle lifetime [0,1]. Left panel: curve list (Color, Size, Opacity). Right panel: editor canvas with keyframe dragging, gradient preview, context menu to add/remove keys. Numeric input fields for exact key times/values.

**Core data model:**
- `FRichCurve` + `FGradientStops`: time → value mapping with interpolation mode (linear, cubic, constant)
- Data stored in Material or Emitter definition
- Serialized as discrete keyframes, rendered as smooth curves in editor

**What we need:** Curves already exist in ParticleEmitter (colorOverLife, sizeOverLife as float arrays). Need: visual curve editor overlay (pan-aware canvas, clickable keyframe drag, numeric editors, real-time preview in viewport).

---

### 4. Timeline / Preview Controls (SNiagaraPreviewGrid / SNiagaraSystemEditor)

**What it does:** Horizontal scrubber bar at bottom of viewport. Shows current frame (display as "3.2s / 5.0s"). Controls: Play/Pause button, Frame-step forward/back (arrow keys), Speed slider (0.1x to 2.0x), Loop toggle, Restart button. Timeline shows particle lifespans (colored bars for each particle's lifetime).

**Core data model:**
- `FNiagaraSystemSimulationData`: frame counter, pause state, playback speed, loop flag
- Timeline visualization: array of particle spawn/death times, rendered as horizontal bars

**What we need:** Extend VfxSystem to expose playback state (current time, pause, speed, loop). Add timeline scrubber UI to overlay (numeric time display, clickable seek, speed multiplier).

---

### 5. Scratch Pad Scripts (UNiagaraScratchPadScripts / SNiagaraScratchPadScriptPanel)

**What it does:** Per-emitter user-authored script modules in Niagara's custom HLSL-like language. Users can write custom logic (e.g., "velocity += wind * dt"). Appears as editable module cards in the stack with syntax highlighting, compile errors inline.

**Core data model:**
- `UNiagaraScratchPadScript`: source code string, bytecode, parameter bindings
- Compiled to HLSL fragment, stitched into final shader
- Cache hash to detect changes and recompile

**What we need:** Keep as forward-compatible feature slot. For MVP, not required — VfxNodeGraph already provides expression-based customization. Mark as "future extension".

---

### 6. Module Pick Dialog (SNiagaraModuleAsset / SNiagaraModuleLibraryView)

**What it does:** Tree view of available modules, searchable by name/category. When user clicks "Add Module" in a stack entry, a modal/dropdown pops up. Modules organized by category (Physics, Lifetime, Render, Custom). Drag modules onto stack, or double-click to add.

**Core data model:**
- Registry of available modules (built-in + custom user modules)
- Each module has metadata: name, category, description, required parameters
- Modules are asset references (`.uasset` files in Niagara plugin or project)

**What we need:** VfxSnippetLibraryWidget already does this (tile grid, case-insensitive filter). Extend with categorization (Physics, Lifetime, Render tabs). Add double-click-to-insert behavior into stack.

---

### 7. Spawn / Burst Rate Editor

**What it does:** Specialized curve editor for spawn rates. Shows spawn rate over emitter lifetime (X-axis = emitter time, Y-axis = spawn rate in particles/sec). Can be constant or time-varying curve. Burst button to add one-time spawn events at specific times.

**Core data model:**
- `FNiagaraSpawnInfo`: base rate (float), per-frame random offset
- Curves for rate variation over time (optional)
- Burst events: array of {time, count}

**What we need:** ParticleEmitter already has spawnRate (single float). Need: multi-key spawn rate curve (time → rate) + burst editor (add time-stamped spawn counts). Reuse curves editor UI.

---

## Section 2: Minimal-Viable Feature Matrix (Ranked by Authoring Impact)

**Ordered by importance (most impactful first):**

1. **Stack Entry Module Add/Remove/Reorder** — Core authoring flow. Right-click stack entry → "Add Module", drag-to-reorder within stack, X to remove.
2. **Parameter Dictionary + References** — Enable user-defined controls (spawn rate, color, lifetime) at System/Emitter scope. Show parameter usage in module expressions.
3. **Curves Editor (Color-Over-Life, Size-Over-Life)** — Immediate visual feedback. Drag keyframes, preview in viewport in real-time.
4. **Timeline Scrubber + Playback Controls** — Essential for iterating (pause, seek, speed). "Play/Pause", "Restart", "Speed x0.5/x1.0/x2.0", seek bar.
5. **Module Picker Dialog (Searchable Category Tabs)** — Discover available modules. VfxSnippetLibrary + "Add Module" button → category tabs (Physics, Lifetime, Render, Custom).
6. **Spawn Rate Curve Editor** — Time-varying spawn rates + burst events. Special case of curves editor.
7. **Parameter Inspector Pane** — Edit parameter defaults, see usage count, delete unused. Simple list + edit form.
8. **Module Enable/Disable Toggle** — Checkbox per module for fast iteration. Recompile shader without removing module data.
9. **Undo/Redo Stack** — Track stack reorders, parameter edits, curve changes. Single undo history shared across all editors.
10. **Keyboard Shortcuts** — Ctrl+Z undo, Ctrl+Shift+Z redo, Delete remove module, Space play/pause, Arrow keys seek, Ctrl+C/V copy/paste curves.
11. **Inheritance / Override Indicators** — Show if a module/parameter overrides parent system. Visual marker (italic text, colored border).
12. **Module Search (In-Stack)** — Quick filter of visible modules in stack. Ctrl+F type to highlight matching modules.
13. **Burst Rate Editor** — Add one-time spawn events. Separate sub-editor for bursts (time + count).
14. **Thumbnail Refresh on Edit** — Update system thumbnail when user edits. Already have VfxThumbnailCapturer; trigger on save.
15. **Hot-Reload Stack Layout** — Auto-reload UI when .uzvfxgraph or emitter config changes on disk (for live dev iteration).

---

## Section 3: Data Model Deltas

**New structures needed beyond existing ParticleEmitter / VfxSystem:**

| New Struct | Purpose | Niagara Equivalent | Host File(s) |
|---|---|---|---|
| `VfxParameterDefinition` | User-defined parameter (name, scope, type, default) | `FNiagaraParameterDefinition` | `runtime/engine/vfx/parameters/parameter_definition.h` |
| `VfxStackEntry` | Stack layer (System Spawn / Update / Emitter Spawn / etc) with ordered module list | `UNiagaraStack::StackEntry` | `runtime/engine/vfx/stack/stack_entry.h` |
| `VfxModuleInstance` | Runtime instance of a module within a stack with override flags, enabled state | `FNiagaraStackModuleData` | `runtime/engine/vfx/modules/module_instance.h` |
| `VfxCurveKey` | Single keyframe (time, value, interpolation mode) | `FRichCurve::FRichCurveKey` | `runtime/engine/vfx/curves/curve_key.h` |
| `VfxCurveData` | Full curve (array of keys, min/max, interpolation settings) | `FRichCurve` | `runtime/engine/vfx/curves/curve_data.h` |
| `VfxBurstEvent` | One-time spawn burst (emitter time, particle count) | `FNiagaraSpawnInfo::Burst` | `runtime/engine/vfx/spawn/burst_event.h` |
| `VfxEditorState` | Transient editor UI state (selected module, expanded stack entries, timeline seek) | `SNiagaraEditorState` (transient, not saved) | `runtime/engine/vfx/editor/editor_state.h` |
| `VfxParameterBindings` | Map of {parameter name → resolved value/default} for module execution | `FNiagaraParameterStore` | `runtime/engine/vfx/parameters/parameter_bindings.h` |

**Extension points in existing files:**

- `ParticleEmitter`: add `parameters: HashMap<string, VfxParameterDefinition>`, `stackLayers: VfxStackEntry[7]` (one per stage)
- `VfxSystem`: add `parameters: HashMap<string, VfxParameterDefinition>` (system-scope)
- `VfxEditorOverlay`: add `currentEditor: EditorMode` (stack | curves | parameters | timeline), `undoStack: UndoCommand[]`

---

## Section 4: Interaction Patterns (Mouse/Keyboard)

### Mouse Conventions

| Action | Behavior |
|--------|----------|
| **Click "Add Module" button** | Open module picker dialog (searchable + category tabs). Click module → insert at end of stack entry. |
| **Right-click stack entry** | Context menu: "Add Module", "Collapse All", "Expand All", "Clear Stack". |
| **Right-click module card** | Context menu: "Remove", "Duplicate", "Move Up", "Move Down", "Disable" (if enabled) / "Enable" (if disabled). |
| **Drag module card within stack** | Reorder within that stack entry only. Ghost preview line shows insertion point. |
| **Click parameter name (parameters pane)** | Highlight + show inspector form on right. Fields: name, type dropdown, default value editor, delete button. |
| **Click curve key in curves editor** | Select key (highlight). Drag to move. Right-click context menu: "Delete", "Set Time/Value". |
| **Click empty curve canvas** | Add new key at click position. |
| **Drag timeline scrubber** | Seek emitter to that time. Update viewport preview live. |
| **Click "+" in timeline** | Add burst event at current seek time. |
| **Scroll curve editor (mouse wheel)** | Pan timeline horizontally (X-axis). Ctrl+scroll = zoom timeline. |
| **Scroll module list** | Pan stack vertically (fixed 4-entry viewport). |

### Keyboard Conventions

| Shortcut | Action |
|----------|--------|
| **Ctrl+Z** | Undo last editor change (module add/remove/reorder, param edit, curve key, burst). |
| **Ctrl+Shift+Z** | Redo. |
| **Delete** | Remove selected module or curve key. Prompt if removing non-empty stack entry. |
| **Space** | Play/Pause timeline. |
| **Left/Right Arrow** | Seek timeline ±0.1s (if timeline focused). |
| **Shift+Left/Right** | Seek ±1s. |
| **Ctrl+F** | Focus module search field in stack (filter visible modules). Esc to clear. |
| **Ctrl+C** | Copy selected curve (curves editor). |
| **Ctrl+V** | Paste curve to another channel. |
| **Escape** | Deselect, close picker dialogs, exit search mode. |
| **Tab** | Cycle between editor modes (Stack → Curves → Parameters → Timeline). |
| **1, 2, 3** | Quick-switch timeline speed (1x, 0.5x, 2.0x). |

### Right-Click Context Menus

**Stack Entry (e.g., "Particle Spawn"):**
- Add Module → [picker dialog]
- Collapse / Expand
- Clear All Modules

**Module Card (e.g., "InitialVelocity"):**
- Remove
- Duplicate
- Move Up / Move Down
- Disable (checkbox toggle via right-click OR single-click checkbox on card)
- View Expression (if VfxNodeGraph-based)

**Curve Editor (Canvas):**
- Add Key at Time X
- Delete All Keys
- Reset to Default

**Timeline Scrubber:**
- Add Burst Event
- Edit Burst Event (if right-click on existing burst)
- Delete Burst

---

## Section 5: Integration with Existing UzEngine Infrastructure

### Leverage Points

1. **VfxEditorOverlay (Phase 10.1):** Already renders text + sliders on top of viewport. Add new overlay modes: StackPanel, CurvesEditor, ParametersPane, TimelineBar.

2. **VfxNodeGraphWidget (Phase 10.4):** Pan/zoom + selection already works. Curves editor is a simplified version (1D curve canvas instead of 2D node graph).

3. **VfxSnippetLibraryWidget (Phase 9.4):** Tile grid with filter. Add "Category" tab bar + "Insert into Stack" button.

4. **VfxExpressionPinEditor (Phase 9.6):** Parse + validate DSL. Extend to handle parameter references (`@ParamName`).

5. **VfxDIRegistry (Phase 10.7):** Already threads parameters into HLSL. Parameter references in module expressions should resolve via this registry.

6. **UndoCommand pattern:** See phase 3.x for how undo works. Extend with StackReorderCommand, ModuleAddCommand, CurveKeyCommand, BurstEventCommand.

### Data Flow

```
User edits stack UI
  ↓ (via StackPanel.OnModuleAdded)
VfxStackEntry::Add(module)
  ↓ (via Editor save)
ParticleEmitter.stackLayers[stage].modules[index] = module
  ↓ (via HotReloadBridge)
VfxModuleCompositor stitches HLSL fragments
  ↓
Module HLSL emitted to final compute shader
  ↓
GPU simulation + viewport live preview

User edits parameter
  ↓
VfxParameterDefinition.defaultValue = newValue
  ↓ (via ParameterBindings resolver)
VfxDIRegistry injects into module expressions
  ↓
Curve keys reference parameters (@ParamName)
  ↓
GPU shader receives resolved values
```

---

## Key Constraints & Decisions

1. **No ImGui dependency.** All UI is TextRenderer + manual mouse hit-testing. Overlay is single-pass, canvas-based.
2. **In-viewport only.** No separate editor window. All editing happens in VfxEditorOverlay modal stack.
3. **No project asset files for modules.** Modules are snippets in library (VfxSnippetLibrary). No `.uasset` equivalent; keep data flat.
4. **Parameter scope is 2-tier.** System-scope (apply to all emitters) + Emitter-scope (per-emitter override). No Particle-scope for MVP (can add later).
5. **Stack layout is fixed.** 7 stages are hardcoded (System Spawn, System Update, Emitter Spawn, Emitter Update, Particle Spawn, Particle Update, Render). Within each, modules are ordered.
6. **Curves are time-normalized.** X-axis is always [0, 1] (particle lifetime) for color/size. Spawn rate uses emitter time.

---

## Estimated Scope (Effort Forecast)

- **Stack UI + Add/Remove/Reorder:** 2–3 days (overlay layout + mouse hit-testing + context menu)
- **Parameters Dictionary + References:** 1–2 days (data model + resolver + UI inspector)
- **Curves Editor (keyframe drag, canvas):** 2–3 days (canvas math + hit-testing + numeric editors)
- **Timeline Scrubber + Playback:** 1 day (simple slider + play state management)
- **Module Picker + Category Tabs:** 1 day (extend VfxSnippetLibraryWidget)
- **Undo/Redo:** 1–2 days (command pattern, history buffer)
- **Parameter Bindings Integration:** 1 day (extend VfxDIRegistry)
- **Testing + Polish:** 2–3 days

**Total: ~12–17 days for full MVP.**

