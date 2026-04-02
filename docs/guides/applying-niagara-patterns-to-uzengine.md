<!-- doc-meta
title: Applying Niagara Patterns to UzEngine VFX
description: Step-by-step guide to implement Niagara-style stack UI in UzEngine
tags: vfx, implementation, patterns, ui, module-stack
-->

# Applying Niagara Patterns to UzEngine VFX Stack

## TL;DR

Your VFX viewport needs to change from a **flat module list** to a **hierarchical stage-based tree**. Implement these 3 core patterns from Niagara:

1. **Hierarchical entry tree** — all UI elements inherit from a base entry class
2. **Stage containers** — modules grouped by stage (Spawn, Update, Render)
3. **Per-module enable/disable** — checkbox to toggle execution

---

## Current State Analysis

Looking at `runtime/demos/vfx_viewport/vfx_viewport_vfx.h`, you likely have:

```cpp
struct VfxEmitter {
    TArray<VfxModule> modules;  // ← All modules in one flat array
    // properties...
};

// UI renders all modules in a single list
for (const VfxModule& mod : emitter.modules) {
    renderModule(mod);
}
```

**Problems**:
- No visual grouping by stage
- Hard to navigate large emitters
- No per-module enable/disable
- Reordering requires manual array manipulation

---

## Recommended Architecture Change

### Step 1: Define Stages

Create an enum for the 5 core stages (same as Niagara):

```cpp
// File: runtime/engine/vfx/vfx_stage.h
enum class VfxStage : uint8 {
    EmitterSpawn  = 0,   // Emitter initialization
    EmitterUpdate = 1,   // Each emitter update frame
    ParticleSpawn = 2,   // Particle birth
    ParticleUpdate = 3,  // Particle physics/forces
    ParticleRender = 4   // Rendering
};

constexpr const char* VfxStageNames[] = {
    "Emitter Spawn",
    "Emitter Update",
    "Particle Spawn",
    "Particle Update",
    "Particle Render"
};

inline const char* getVfxStageName(VfxStage stage) {
    return VfxStageNames[static_cast<uint8>(stage)];
}
```

### Step 2: Reorganize Emitter Storage

Change from flat array to stage-organized arrays:

**OPTION A: Separate arrays (clearer, Niagara-like)**

```cpp
// File: runtime/engine/vfx/vfx_emitter.h
struct VfxEmitter {
    // Stage-organized modules
    TArray<VfxModule> emitterSpawnModules;
    TArray<VfxModule> emitterUpdateModules;
    TArray<VfxModule> particleSpawnModules;
    TArray<VfxModule> particleUpdateModules;
    TArray<VfxModule> particleRenderModules;
    
    // Helper to get modules for a stage
    TArray<VfxModule>& getModulesForStage(VfxStage stage) {
        switch (stage) {
            case VfxStage::EmitterSpawn:   return emitterSpawnModules;
            case VfxStage::EmitterUpdate:  return emitterUpdateModules;
            case VfxStage::ParticleSpawn:  return particleSpawnModules;
            case VfxStage::ParticleUpdate: return particleUpdateModules;
            case VfxStage::ParticleRender: return particleRenderModules;
        }
        static TArray<VfxModule> empty;
        return empty;
    }
    
    const TArray<VfxModule>& getModulesForStage(VfxStage stage) const {
        return const_cast<VfxEmitter*>(this)->getModulesForStage(stage);
    }
};
```

**OPTION B: Single array with metadata (more compact)**

```cpp
struct VfxModule {
    VfxStage stage;         // ← Which stage this module belongs to
    uint32 stageIndex;      // ← Position within the stage
    bool enabled = true;    // ← Enable/disable flag
    VfxModuleType type;
    // ... other fields
};

struct VfxEmitter {
    TArray<VfxModule> modules;  // All modules, sorted by stage + stageIndex
    
    TArray<VfxModule*> getModulesForStage(VfxStage stage) {
        TArray<VfxModule*> result;
        for (VfxModule& mod : modules) {
            if (mod.stage == stage) {
                result.push_back(&mod);
            }
        }
        return result;
    }
};
```

**Recommendation**: Use **Option A** (separate arrays) for clarity and to match Niagara's pattern. It makes the code easier to understand and iterate.

### Step 3: Create the Stack Entry Tree

Create a minimal tree structure for the UI:

```cpp
// File: runtime/engine/vfx/vfx_stack_entry.h
struct VfxStackEntry {
    enum Type {
        Stage,      // Stage header (e.g., "Particle Update")
        Module,     // A single module
        Property    // A module property (optional, for later)
    };
    
    Type type;
    VfxStage stage;           // Only if type == Stage
    VfxModule* modulePtr;     // Only if type == Module
    
    // Expansion state (per-stage)
    bool isExpanded = true;
    
    // Display text
    string getDisplayName() const {
        if (type == Type::Stage) {
            return VfxStageNames[static_cast<uint8>(stage)];
        } else if (type == Type::Module) {
            return modulePtr->getDisplayName();
        }
        return "Unknown";
    }
    
    // Enable state (for modules)
    bool getIsEnabled() const {
        if (type == Type::Module) {
            return modulePtr->enabled;
        }
        return true;  // Stages are always "enabled"
    }
    
    void setIsEnabled(bool e) {
        if (type == Type::Module) {
            modulePtr->enabled = e;
        }
    }
};
```

### Step 4: Build the Tree from Emitter

When loading an emitter, populate the tree:

```cpp
// File: vfx_viewport.cpp or similar
void buildStackTree(const VfxEmitter& emitter, TArray<VfxStackEntry>& outTree) {
    outTree.clear();
    
    // Create 5 stage groups
    const VfxStage stages[] = {
        VfxStage::EmitterSpawn,
        VfxStage::EmitterUpdate,
        VfxStage::ParticleSpawn,
        VfxStage::ParticleUpdate,
        VfxStage::ParticleRender
    };
    
    for (VfxStage stage : stages) {
        // Add stage header
        VfxStackEntry stageEntry;
        stageEntry.type = VfxStackEntry::Type::Stage;
        stageEntry.stage = stage;
        stageEntry.isExpanded = true;
        outTree.push_back(stageEntry);
        
        // Add modules for this stage
        const auto& modules = emitter.getModulesForStage(stage);
        for (const VfxModule& mod : modules) {
            VfxStackEntry moduleEntry;
            moduleEntry.type = VfxStackEntry::Type::Module;
            moduleEntry.modulePtr = const_cast<VfxModule*>(&mod);
            outTree.push_back(moduleEntry);
        }
    }
}
```

### Step 5: Render the Tree in UI

Your viewport's module panel should render this tree:

```cpp
// Pseudo-code for the UI rendering loop
void renderStackPanel(const TArray<VfxStackEntry>& stack) {
    for (const VfxStackEntry& entry : stack) {
        if (entry.type == VfxStackEntry::Type::Stage) {
            // Render stage header with expand/collapse button
            ImGui::CollapsingHeader(entry.getDisplayName().c_str(), 
                                   &entry.isExpanded);
            
            if (entry.isExpanded) {
                // Render modules under this stage (indented)
                ImGui::Indent();
                // (continue looping, but skip until next stage)
            }
        } 
        else if (entry.type == VfxStackEntry::Type::Module) {
            // Render module with checkbox
            bool enabled = entry.getIsEnabled();
            if (ImGui::Checkbox(entry.getDisplayName().c_str(), &enabled)) {
                entry.setIsEnabled(enabled);
                // TODO: Mark emitter dirty for recompilation
            }
        }
    }
}
```

Or with a proper tree widget (Imgui::TreeNode or custom):

```cpp
void renderStackTree(const TArray<VfxStackEntry>& stack) {
    for (auto& entry : stack) {
        if (entry.type == VfxStackEntry::Type::Stage) {
            if (ImGui::TreeNode(entry.getDisplayName().c_str())) {
                for (auto& child : stack) {
                    if (child.type == VfxStackEntry::Type::Module 
                        && child.modulePtr->stage == entry.stage) {
                        renderModuleEntry(child);
                    }
                }
                ImGui::TreePop();
            }
        }
    }
}

void renderModuleEntry(VfxStackEntry& entry) {
    bool enabled = entry.getIsEnabled();
    string checkboxLabel = (enabled ? "[✓] " : "[  ] ") + entry.getDisplayName();
    
    if (ImGui::Selectable(checkboxLabel.c_str())) {
        // User clicked module to select it
        selectedModule = entry.modulePtr;
    }
    
    // Handle click on checkbox
    if (ImGui::IsItemClicked(0) && ImGui::GetIO().MousePos.x < ImGui::GetCursorPos().x - 20) {
        entry.setIsEnabled(!enabled);
    }
}
```

---

## Incremental Implementation Plan

### Phase 1: Data Structure (1-2 hours)
1. Add `VfxStage` enum
2. Update `VfxEmitter` to use stage-organized storage
3. Update `VfxModule` to add `enabled` flag
4. Create migration code to convert old flat array to new staged arrays

### Phase 2: Stack Tree (1 hour)
1. Create `VfxStackEntry` struct
2. Write `buildStackTree()` function
3. Wire it to your emitter loading code

### Phase 3: UI Rendering (2-3 hours)
1. Replace flat module list render with tree render
2. Add stage collapsible headers
3. Add module enable/disable checkboxes
4. Test collapse/expand per-stage

### Phase 4: Interaction (2-3 hours)
1. Implement module reordering via drag/drop within stage
2. Implement "+" button to add modules to stage
3. Implement module removal
4. Wire undo/redo for all operations

### Phase 5: Module Properties (1-2 hours)
1. When module selected, populate property panel
2. Link parameter changes back to emitter

---

## Module Enable/Disable Details

### Current Execution Path

Somewhere in your emitter compile/execute, you have code like:

```cpp
void compileEmitterModules(VfxEmitter& emitter) {
    for (VfxModule& module : emitter.modules) {
        compileModule(module);  // Generates shader code, CB layout, etc.
    }
}
```

### Updated Path

Add the `enabled` check:

```cpp
void compileEmitterModules(VfxEmitter& emitter) {
    for (VfxModule& module : emitter.modules) {
        if (!module.enabled) continue;  // ← Skip disabled modules
        compileModule(module);
    }
}
```

### Storage & Persistence

The `enabled` flag should be:
- **Stored in VfxModule** (not in UI state)
- **Persisted when saving emitter** (included in `.uzmat` or `.uzfx` file)
- **Loaded when opening emitter** (read from saved file)

```cpp
// When saving
void saveEmitter(const VfxEmitter& emitter, const string& filepath) {
    // ... serialize all emitter data ...
    for (const VfxModule& mod : emitter.modules) {
        file.write("enabled", mod.enabled);  // ← Save enabled state
    }
}

// When loading
void loadEmitter(VfxEmitter& emitter, const string& filepath) {
    // ... deserialize all emitter data ...
    for (VfxModule& mod : emitter.modules) {
        file.read("enabled", mod.enabled);  // ← Load enabled state
    }
}
```

---

## Drag/Drop Reordering Example

Once your UI renders modules as draggable items:

```cpp
void onModuleDropped(VfxModule& sourceMod, VfxModule& targetMod, bool dropBefore) {
    // Can only reorder within the same stage
    if (sourceMod.stage != targetMod.stage) {
        // TODO: Show error message "Can only reorder within the same stage"
        return;
    }
    
    VfxStage stage = sourceMod.stage;
    auto& modules = emitter.getModulesForStage(stage);
    
    // Find indices
    int srcIdx = -1, dstIdx = -1;
    for (int i = 0; i < modules.size(); i++) {
        if (&modules[i] == &sourceMod) srcIdx = i;
        if (&modules[i] == &targetMod) dstIdx = i;
    }
    
    if (srcIdx < 0 || dstIdx < 0) return;  // Should never happen
    
    // Reorder
    if (srcIdx < dstIdx) {
        dstIdx--;  // Adjust for removal
    }
    
    VfxModule tmp = modules[srcIdx];
    modules.erase(modules.begin() + srcIdx);
    modules.insert(modules.begin() + dstIdx, tmp);
    
    // Trigger UI refresh + recompile
    markEmitterDirty();
    broadcastStructureChanged();
}
```

---

## Checklist for Niagara Pattern Compliance

- [ ] Stages are visually grouped (stage headers)
- [ ] Stages are collapsible/expandable
- [ ] Each module has an enable/disable checkbox
- [ ] Disabled modules are skipped during compilation
- [ ] Module enable/disable state persists on save/load
- [ ] Modules can be reordered within their stage via drag/drop
- [ ] Cannot drag module between stages
- [ ] "+" button adds modules to stages
- [ ] Module properties panel works when selected
- [ ] Undo/redo works for all operations
- [ ] Stage order is fixed (Spawn → Update → Render)

---

## Files to Update

| File | Change |
|------|--------|
| `runtime/engine/vfx/vfx_stage.h` | NEW: Stage enum |
| `runtime/engine/vfx/vfx_module.h` | ADD: `enabled` flag, `stage` field |
| `runtime/engine/vfx/vfx_emitter.h` | CHANGE: Flat array → stage-organized arrays |
| `runtime/engine/vfx/vfx_stack_entry.h` | NEW: Tree entry struct |
| `runtime/demos/vfx_viewport/vfx_viewport.cpp` | CHANGE: Module list render → tree render |
| Compilation/execution code | ADD: `if (!mod.enabled) continue;` |

---

## References

- **Niagara Stack Entry Pattern**: `docs/analysis/niagara-module-stack-ui-architecture.md`
- **Quick Pattern Reference**: `docs/analysis/vfx-stack-patterns-summary.md`
- **Implementation Guide**: `docs/guides/vfx-stack-ui-implementation-guide.md`

