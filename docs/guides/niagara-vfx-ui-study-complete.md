<!-- doc-meta
title: Niagara VFX UI Analysis — Complete Study
description: Full analysis of how UE5 Niagara organizes its module stack editor, with implementation guidance for UzEngine
tags: niagara, vfx, ui-architecture, study-complete
-->

# Niagara VFX UI Analysis — Complete Study

## Documents Created

This analysis extracted the architectural patterns from UE5 Niagara's module stack editor. Three detailed documents have been created:

### 1. **Architecture Analysis** (Reference)
📄 `docs/analysis/niagara-module-stack-ui-architecture.md`

**Purpose**: Deep dive into how Niagara organizes modules and stages

**Contents**:
- Hierarchical entry tree pattern (no switch statements)
- Execution category/subcategory naming system
- Module item representation
- Enable/disable implementation (lines 1373-1379 of source)
- Stage-based organization (script item groups)
- Module reordering via drag/drop
- Add/remove module interface
- Per-module properties panel
- Refresh signal pattern (observer)

**Read this if**: You need to understand the exact implementation details from UE5 source code.

**Key files referenced**:
- `NiagaraStackEntry.h` — base class pattern
- `NiagaraStackModuleItem.h` — enable/disable logic
- `NiagaraStackScriptItemGroup.h` — stage container pattern

---

### 2. **Patterns Quick Reference** (Cheat Sheet)
📄 `docs/analysis/vfx-stack-patterns-summary.md`

**Purpose**: Quick reference for the 8 key architectural patterns

**Contents**:
- Pattern 1: Hierarchical entry tree (virtual dispatch)
- Pattern 2: Execution context via FName constants
- Pattern 3: Per-module enable/disable state
- Pattern 4: Stage as script item group (container)
- Pattern 5: Drag/drop reordering with virtual zones
- Pattern 6: Change notification via delegates
- Pattern 7: Expansion state tracking
- Pattern 8: Module filtering by stage (add menu)

**Read this if**: You want a quick overview of what to implement without reading 50 KB of code.

**Recommended**: Print this or keep it visible while coding.

---

### 3. **Implementation Guide** (Action Plan)
📄 `docs/guides/vfx-stack-ui-implementation-guide.md`

**Purpose**: Step-by-step instructions to implement each feature

**Contents**:
1. Stage-based organization (groups modules into Spawn/Update/Render)
2. Per-module enable/disable checkbox
3. Add/remove modules per stage
4. Reorder modules via drag/drop
5. Module properties panel (when selected)
6. Collapse/expand stages
7. Undo/redo integration
8. Testing checklist
9. Anti-patterns to avoid
10. Next steps roadmap

**Read this if**: You're about to start coding and need a clear checklist.

**Output**: A complete working module stack UI matching Niagara.

---

### 4. **Apply to UzEngine** (Practical Adaptation)
📄 `docs/guides/applying-niagara-patterns-to-uzengine.md`

**Purpose**: Map Niagara patterns to UzEngine's C++ codebase

**Contents**:
- Current state analysis (what you have now)
- Recommended architecture change (data structure)
- Step 1: Define stages
- Step 2: Reorganize emitter storage
- Step 3: Create stack entry tree
- Step 4: Build tree from emitter
- Step 5: Render tree in UI
- Incremental implementation plan (Phase 1-5)
- Module enable/disable details
- Drag/drop reordering example
- Compliance checklist
- Files to update

**Read this if**: You're implementing this for UzEngine specifically.

**Includes**:
- Actual C++ code snippets (copy-paste ready)
- Two data structure options (flat vs staged)
- Helper functions (buildStackTree, renderStackPanel)

---

## Quick Start: The 3-Minute Version

### What Niagara Does

```
VFX Emitter Editor
├── Particle Spawn (stage)
│   ├── ✓ Initialize Particle (module, enabled)
│   ├── ✓ Shape Location (module, enabled)
│   └── ⊕ Point Force (module, disabled/inactive)
├── Particle Update (stage)
│   ├── ⊕ Drag (module, disabled)
│   ├── ✓ Vortex Force (module, enabled)
│   └── ✓ Solve Forces (module, enabled)
└── Particle Render (stage)
    └── ✓ Mesh Renderer (module, enabled)
```

**Key features**:
- Stages are collapsible containers
- Each module has an enable/disable checkbox (✓ or ⊕)
- Modules can be reordered within their stage (drag/drop)
- Click "+" on stage to add modules
- Click a module to edit its properties in a panel
- Disabled modules don't execute during compilation

### What You Need to Implement

1. **Data Structure**:
   - Add `VfxStage` enum (Spawn, Update, Render, etc.)
   - Add `bool enabled` field to VfxModule
   - Reorganize emitter storage by stage

2. **Tree Structure**:
   - Create `VfxStackEntry` (stage or module)
   - Build tree from emitter's stage-organized arrays
   - Render collapsible tree in UI

3. **Interactions**:
   - Checkbox click → toggle module.enabled
   - Stage click → collapse/expand
   - Drag module → reorder within stage
   - "+" click → show add module menu
   - Module click → populate properties panel

---

## Architecture Overview

```
┌─ UE5 Niagara ─────────────────┐
│  UNiagaraStackEntry (base)    │
│  ├─ UNiagaraStackRoot         │
│  ├─ UNiagaraStackScriptItemGroup (= stage)
│  ├─ UNiagaraStackModuleItem (= module)  ← has bIsEnabled
│  └─ UNiagaraStackFunctionInput (= property)
└───────────────────────────────┘
           ↓ patterns ↓
┌─ UzEngine Equivalent ──────────┐
│ VfxStackEntry (base struct)    │
│ ├─ type = Stage               │
│ ├─ type = Module              │
│ └─ isExpanded, getIsEnabled() │
└───────────────────────────────┘
           ↓ renders ↓
┌─ UI: SVfxModuleStackPanel ────┐
│ • Collapsible stages          │
│ • Enable/disable checkboxes   │
│ • Drag/drop reordering        │
│ • "+" button to add modules   │
│ • Properties panel on select  │
└───────────────────────────────┘
```

---

## Key Patterns (In Priority Order)

### MUST HAVE (Core Niagara Behavior)

1. **Stages (Containers)**
   - Modules grouped by stage
   - Stages are collapsible
   - Stage order is fixed (Spawn → Update → Render)

2. **Enable/Disable per Module**
   - Each module has `bool enabled`
   - Checkbox in UI toggles it
   - Disabled modules skipped during compilation

3. **Reorder Within Stage**
   - Drag/drop to reorder modules
   - Only within same stage (cannot move Spawn → Update)

### SHOULD HAVE (Polish)

4. **Add/Remove Modules**
   - "+" button to add modules to stage
   - Delete button to remove
   - Module list filtered by stage compatibility

5. **Module Properties**
   - Click module → properties panel updates
   - Edit parameters
   - Changes apply immediately or on confirm

6. **Undo/Redo**
   - Enable/disable: recordable
   - Reorder: recordable
   - Add/remove: recordable

### NICE TO HAVE (Optional)

7. **Expansion State Persistence**
   - Remember which stages are collapsed
   - Restore on reload

8. **Comments/Annotations**
   - Add notes to modules
   - Visual grouping of related modules

9. **Module Search/Filter**
   - Find modules by name
   - Filter by type

---

## Implementation Phases

### Phase 1: Foundation (4-6 hours)
- [ ] Add `VfxStage` enum
- [ ] Update `VfxEmitter` storage (stage-organized arrays)
- [ ] Add `enabled` field to `VfxModule`
- [ ] Create `VfxStackEntry` tree struct
- [ ] Write `buildStackTree()` function

### Phase 2: Rendering (3-4 hours)
- [ ] Replace flat module list with tree render
- [ ] Add stage collapsible headers
- [ ] Add enable/disable checkboxes
- [ ] Test collapse/expand per-stage
- [ ] Visual feedback (grayed out when disabled)

### Phase 3: Interactions (4-5 hours)
- [ ] Wire checkbox to toggle `module.enabled`
- [ ] Skip disabled modules in compilation
- [ ] Implement drag/drop reordering within stage
- [ ] Reject drops between stages (visual feedback)
- [ ] Test reordering works correctly

### Phase 4: Module Management (3-4 hours)
- [ ] Implement "+" button for add menu
- [ ] Filter available modules by stage
- [ ] Implement add module logic
- [ ] Implement remove module (delete key)
- [ ] Implement module cut/copy/paste (optional)

### Phase 5: Properties & Polish (3-4 hours)
- [ ] Click module to select and populate properties
- [ ] Edit module parameters
- [ ] Update emitter on property change
- [ ] Undo/redo for all operations
- [ ] Save/load enabled state with emitter

**Total estimated time**: 17-24 hours for full implementation.

---

## Files to Consult During Implementation

### For Design Decisions
- `docs/analysis/niagara-module-stack-ui-architecture.md`
- `docs/analysis/vfx-stack-patterns-summary.md`

### For Implementation Details
- `docs/guides/vfx-stack-ui-implementation-guide.md`
- `docs/guides/applying-niagara-patterns-to-uzengine.md`

### For Code Examples
- See **Applying to UzEngine** document for C++ snippets

### For UE5 Reference
- UE5 source: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/`
  - `NiagaraStackEntry.h` — base class
  - `NiagaraStackModuleItem.h` — enable/disable (lines 49, 114-117, 1373)
  - `NiagaraStackScriptItemGroup.h` — stage container
  - `NiagaraStackRoot.h` — tree structure

---

## Data Flow Diagram

```
VfxEmitter (persistent data)
├── emitterSpawnModules[0..N]
│   └── VfxModule
│       ├── type: (Drag, Force, etc.)
│       ├── enabled: bool ← ENABLE/DISABLE FLAG
│       ├── parameters: { damping, ...}
│       └── ...
├── emitterUpdateModules[0..N]
├── particleSpawnModules[0..N]
├── particleUpdateModules[0..N]
└── particleRenderModules[0..N]
        ↓ buildStackTree()
VfxStackEntry[] (UI tree)
├── StageEntry (ParticleSpawn)
│   ├── ModuleEntry(ptr→VfxModule[0])
│   └── ModuleEntry(ptr→VfxModule[1])
├── StageEntry (ParticleUpdate)
│   ├── ModuleEntry(ptr→VfxModule[0])
│   ├── ModuleEntry(ptr→VfxModule[1])
│   └── ModuleEntry(ptr→VfxModule[2])
└── StageEntry (ParticleRender)
        ↓ renderStackPanel()
SVfxModuleStackPanel (UI widget)
├── [▼] Particle Spawn              ← Collapsible stage
│   ├─ [✓] Initialize Particle      ← Enabled checkbox
│   ├─ [✓] Shape Location           
│   └─ [⊕] Point Force              ← Disabled (grayed out)
├── [▼] Particle Update
│   ├─ [✓] Vortex Force
│   └─ [✓] Solve Forces
└── [▼] Particle Render
    └─ [✓] Mesh Renderer
```

---

## Validation Checklist

### Data Structure
- [ ] VfxStage enum defined with 5 stages
- [ ] VfxEmitter has stage-organized arrays or single array with stage metadata
- [ ] VfxModule has `bool enabled` field
- [ ] VfxStackEntry tree struct created

### UI Rendering
- [ ] Stages rendered as collapsible headers
- [ ] Modules rendered under their stage with proper indentation
- [ ] Enable/disable checkbox visible and working
- [ ] Disabled modules visually grayed out

### Interaction
- [ ] Checkbox click toggles module.enabled
- [ ] Stage header click collapses/expands
- [ ] Disabled modules skipped during compilation
- [ ] Drag/drop reorders modules within stage
- [ ] Drop between stages is rejected with visual feedback

### Module Management
- [ ] "+" button on stage shows add menu
- [ ] Available modules filtered by stage
- [ ] Adding module updates tree
- [ ] Deleting module updates tree

### Properties
- [ ] Click module populates properties panel
- [ ] Edit parameter updates module
- [ ] Changes applied immediately

### Polish
- [ ] Undo/redo works
- [ ] Enable/disable state persists on save/load
- [ ] Expansion state persists (optional)

---

## Common Pitfalls to Avoid

1. **Flat list instead of stages**
   - ❌ Bad: render all modules in one list
   - ✅ Good: group by stage with headers

2. **Enable state only in UI**
   - ❌ Bad: toggle is UI-only, lost on reload
   - ✅ Good: state stored in VfxModule, persisted

3. **Drag between stages allowed**
   - ❌ Bad: allow moving Spawn module to Update stage
   - ✅ Good: reject with error message

4. **No filtering in add menu**
   - ❌ Bad: show all modules for all stages
   - ✅ Good: filter by stage compatibility

5. **Properties don't update emitter**
   - ❌ Bad: changes in panel don't affect emitter
   - ✅ Good: property change triggers emitter update

---

## Success Criteria

Your implementation matches Niagara when:

1. **Visual organization**: Modules are grouped into visually distinct stages
2. **Enable/disable**: Each module has independent on/off state (checkbox)
3. **Reordering**: Modules can be reordered within their stage via drag/drop
4. **Module management**: "+" to add, delete to remove, filtered by stage
5. **Property editing**: Select module → edit parameters → changes apply
6. **Persistence**: Enabled/disabled state saves and loads correctly
7. **Undo/redo**: All operations are undoable

---

## Contact/Questions

If implementing and you hit issues:

1. **Check the implementation checklist** in `docs/guides/vfx-stack-ui-implementation-guide.md`
2. **Review the code examples** in `docs/guides/applying-niagara-patterns-to-uzengine.md`
3. **Reference the UE5 source** at `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/`
4. **Check the patterns summary** if you need a refresher on architecture

---

## Summary

Niagara's module stack editor uses:
1. **Hierarchical tree** (single base class, virtual dispatch)
2. **Stage containers** (organize modules logically)
3. **Per-module enable/disable** (independent toggles)
4. **Drag/drop reordering** (within stage only)
5. **Change notifications** (observers for UI sync)

This analysis provides the complete pattern reference and implementation guide to bring this design to UzEngine.

