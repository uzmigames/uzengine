<!-- doc-meta
title: VFX Stack UI — Quick Reference Card
description: One-page summary of Niagara stack UI architecture for UzEngine
tags: vfx, quick-reference, cheat-sheet
-->

# VFX Stack UI — Quick Reference Card

Print this or keep it visible while implementing.

---

## The 5 Stages (Fixed Order)

```cpp
enum class VfxStage {
    EmitterSpawn  = 0,    // Emitter init
    EmitterUpdate = 1,    // Each frame
    ParticleSpawn = 2,    // Birth
    ParticleUpdate = 3,   // Physics
    ParticleRender = 4    // Render
};
```

Stage order is immutable—always render in this order.

---

## Module Enable/Disable

```cpp
struct VfxModule {
    bool enabled = true;  // ← THE KEY FIELD
    VfxStage stage;
    // ... other fields
};

// When user toggles checkbox:
module.enabled = !module.enabled;
broadcastEmitterChanged();  // UI refresh

// During compilation:
if (!module.enabled) continue;  // Skip execution
```

**Storage**: In VfxModule, NOT in UI. Must persist.

---

## Tree Data Structure

```cpp
struct VfxStackEntry {
    enum { Stage, Module } type;
    
    // If Stage:
    VfxStage stage;
    bool isExpanded;
    
    // If Module:
    VfxModule* modulePtr;
    bool isSelected;
};

// Build from emitter:
void buildStackTree(const VfxEmitter& emitter, TArray<VfxStackEntry>& tree) {
    for (VfxStage stage : ALL_STAGES) {
        tree.add({ type: Stage, stage: stage, isExpanded: true });
        for (VfxModule& mod : emitter.getModulesForStage(stage)) {
            tree.add({ type: Module, modulePtr: &mod });
        }
    }
}
```

---

## UI Layout

```
[▼] Particle Spawn                [+] [Delete]
    [✓] Initialize Particle
    [✓] Shape Location
    [⊕] Point Force
    
[▼] Particle Update               [+] [Delete]
    [⊕] Drag
    [✓] Vortex Force
    
[▼] Particle Render               [+] [Delete]
    [✓] Mesh Renderer
```

- `[▼]` = collapsible stage header (click to collapse)
- `[✓]` = enabled module (checkbox)
- `[⊕]` = disabled module (grayed out)
- `[+]` = add module button
- `[Delete]` = remove button (or context menu)

---

## Key Interactions

| Action | Result |
|--------|--------|
| Click stage header | Collapse/expand that stage |
| Click checkbox on module | Toggle module.enabled |
| Drag module within stage | Reorder (update array indices) |
| Drag module to other stage | REJECT (show error) |
| Click "+" on stage | Show add menu (filtered modules) |
| Select a module | Populate properties panel |
| Click delete | Remove module from array |

---

## Validation Rules

| Rule | Why | Implementation |
|------|-----|-----------------|
| Modules can only reorder within their stage | Script dependency order | Check `source.stage == target.stage` before drop |
| Disabled modules skip execution | User can disable problematic modules | `if (!mod.enabled) continue;` in compile loop |
| Enable state must persist | Data loss on reload is bad | Save `module.enabled` to file |
| One stage per module | Module belongs to ONE stage | `VfxModule.stage` is immutable |

---

## Code Skeleton (C++)

```cpp
// Storage
struct VfxEmitter {
    TArray<VfxModule> emitterSpawnModules;
    TArray<VfxModule> emitterUpdateModules;
    TArray<VfxModule> particleSpawnModules;
    TArray<VfxModule> particleUpdateModules;
    TArray<VfxModule> particleRenderModules;
    
    auto& getModulesForStage(VfxStage s) {
        switch (s) {
            case ParticleSpawn:  return particleSpawnModules;
            case ParticleUpdate: return particleUpdateModules;
            // ...
        }
    }
};

struct VfxModule {
    bool enabled = true;    // ← ENABLE/DISABLE
    VfxModuleType type;
    TMap<String, float> parameters;
};

// UI Tree
struct VfxStackEntry {
    enum { Stage, Module } type;
    VfxStage stage;         // if type == Stage
    VfxModule* modulePtr;   // if type == Module
    bool isExpanded = true;
};

// Build
void buildStackTree(VfxEmitter& e, TArray<VfxStackEntry>& tree) {
    VfxStage stages[] = {VfxStage::EmitterSpawn, VfxStage::EmitterUpdate, 
                         VfxStage::ParticleSpawn, VfxStage::ParticleUpdate, 
                         VfxStage::ParticleRender};
    for (auto s : stages) {
        tree.push({Stage, .stage = s});
        for (auto& m : e.getModulesForStage(s)) {
            tree.push({Module, .modulePtr = &m});
        }
    }
}

// Render (ImGui pseudo-code)
for (auto& entry : tree) {
    if (entry.type == Stage) {
        if (ImGui::CollapsingHeader(getStageName(entry.stage))) {
            entry.isExpanded = true;
            // children rendered as indented items below
        } else {
            entry.isExpanded = false;
        }
    } else if (entry.type == Module) {
        bool en = entry.modulePtr->enabled;
        ImGui::Checkbox(entry.modulePtr->getName(), &en);
        if (en != entry.modulePtr->enabled) {
            entry.modulePtr->enabled = en;
            broadcastChange();
        }
    }
}

// Compilation
for (auto s : stages) {
    for (auto& m : e.getModulesForStage(s)) {
        if (!m.enabled) continue;  // ← SKIP DISABLED
        compileModule(m);
    }
}

// Reorder (drop handler)
void onModuleDropped(VfxModule& src, VfxModule& tgt, bool before) {
    if (src.stage != tgt.stage) return;  // REJECT
    
    auto& mods = emitter.getModulesForStage(src.stage);
    int si = indexOf(mods, src);
    int ti = indexOf(mods, tgt);
    if (!before && si < ti) ti--;
    
    mods.erase(si);
    mods.insert(ti, src);
}

// Add (menu selection)
void onAddModuleSelected(VfxStage stage, VfxModuleTemplate tmpl) {
    VfxModule mod = tmpl.instantiate();
    emitter.getModulesForStage(stage).push_back(mod);
    buildStackTree(emitter, tree);  // Rebuild
}

// Delete
void onDeleteModule(VfxModule& mod) {
    auto& mods = emitter.getModulesForStage(mod.stage);
    mods.erase(indexOf(mods, mod));
}
```

---

## Testing Checklist

- [ ] Stage headers collapse/expand
- [ ] Modules render under correct stage
- [ ] Checkbox toggles module.enabled
- [ ] Disabled modules grayed out
- [ ] Reorder within stage works
- [ ] Reorder between stages is rejected
- [ ] "+" shows menu with filtered modules
- [ ] Adding module updates tree
- [ ] Deleting module updates tree
- [ ] Compile skips disabled modules
- [ ] Enable state saves/loads
- [ ] Undo/redo works

---

## Anti-Patterns (DON'T DO THESE)

❌ **Flat module list** — Unreadable for 20+ modules  
✅ Use stages

❌ **Enable state in UI only** — Lost on reload  
✅ Store in VfxModule struct

❌ **Allow drag between stages** — Breaks execution order  
✅ Validate drop within same stage

❌ **"Add Module" shows all modules** — Confusing for user  
✅ Filter by stage compatibility

❌ **No undo/redo** — User loses work  
✅ Record transactions for enable/disable/reorder

---

## Files to Modify

| File | Change | Why |
|------|--------|-----|
| `vfx_stage.h` | NEW: enum VfxStage | Define the 5 stages |
| `vfx_module.h` | ADD: bool enabled | Enable/disable state |
| `vfx_emitter.h` | CHANGE: storage layout | Group by stage |
| `vfx_stack_entry.h` | NEW: tree struct | UI representation |
| `vfx_viewport.cpp` | CHANGE: render loop | Tree render |
| compilation code | ADD: if (!mod.enabled) | Skip disabled |

---

## Success = This Output

```
[▼] Particle Spawn
    [✓] Initialize Particle      ← Can click checkbox
    [✓] Shape Location           ← Can drag to reorder
    [⊕] Point Force              ← Grayed = disabled
[+] Add module
[Spawn stage properties]

[▼] Particle Update
    [✓] Drag
    [✓] Vortex Force (SELECTED)  ← Blue highlight
    [✓] Solve Forces
[+] Add module

[Module Properties Panel]
Drag coefficient: [======] 0.25
Velocity ref: [Bound to velocity ▼]
```

If you see this in your UI, you're done.

---

## Reference Documents

- Deep dive: `docs/analysis/niagara-module-stack-ui-architecture.md`
- Quick patterns: `docs/analysis/vfx-stack-patterns-summary.md`
- Implementation guide: `docs/guides/vfx-stack-ui-implementation-guide.md`
- UzEngine-specific: `docs/guides/applying-niagara-patterns-to-uzengine.md`

---

## Key Numbers

| Metric | Value | Notes |
|--------|-------|-------|
| Stages | 5 | Fixed: Spawn, Update, Render (x2 scopes) |
| Enable/disable fields | 1 per module | `bool enabled` |
| Reorder constraints | Within-stage only | No cross-stage moves |
| Module types per stage | 3-15 typical | Hardcoded list per stage |
| Max modules per emitter | ~50 recommended | UI becomes unwieldy beyond this |
| Undo/redo transaction types | 4+ | Enable, disable, add, remove, reorder |

---

## Estimated Effort

| Phase | Hours | Output |
|-------|-------|--------|
| Data structure | 2 | VfxStage enum, storage |
| Tree building | 1 | buildStackTree() |
| UI rendering | 3 | Collapsible stages + checkboxes |
| Reordering | 2 | Drag/drop validation + move |
| Add/remove | 2 | Menu + add/delete logic |
| Properties | 2 | Parameter panel |
| Polish | 2 | Undo/redo, persistence |
| **TOTAL** | **14-16** | Full Niagara-compatible stack UI |

---

**Last updated**: 2026-04-01  
**Version**: 1.0  
**Status**: Ready for implementation

