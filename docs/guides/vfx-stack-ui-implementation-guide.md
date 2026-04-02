<!-- doc-meta
title: VFX Stack UI Implementation Guide
description: Practical guide for implementing Niagara-style module stack UI in UzEngine
tags: vfx, ui, implementation, modules, stack
-->

# VFX Stack UI Implementation Guide

## What You Need to Change

Your current VFX viewport shows modules in a flat list. Niagara's design uses a **hierarchical tree** with stages as collapsible containers. Here's what to implement.

---

## 1. Stage-Based Organization (MANDATORY)

**Current state**: Modules displayed as flat list

**Target state**: Organized by stage

```
VFX Emitter
├── Particle Spawn (collapsible)
│   ├── ✓ Initialize Particle
│   ├── ✓ Shape Location
│   └── ⊕ Point Force
├── Particle Update (collapsible)
│   ├── ⊕ Drag
│   ├── ✓ Vortex Force
│   └── ⊕ Solve Forces and Velocity
├── Particle Render (collapsible)
│   └── ✓ Mesh Renderer
└── Emitter Properties (read-only)
```

### Implementation Steps

1. **Define stage categories**:
   ```cpp
   enum class VfxStage {
       EmitterSpawn,
       EmitterUpdate,
       ParticleSpawn,
       ParticleUpdate,
       ParticleRender
   };
   ```

2. **Group modules by stage** in your emitter struct:
   ```cpp
   struct VfxEmitter {
       TArray<VfxModule> emitterSpawnModules;
       TArray<VfxModule> emitterUpdateModules;
       TArray<VfxModule> particleSpawnModules;
       TArray<VfxModule> particleUpdateModules;
       TArray<VfxModule> particleRenderModules;
   };
   ```
   OR store in a single array with stage + index metadata:
   ```cpp
   struct VfxModule {
       VfxStage stage;
       int32 stageIndex;  // Position within this stage
       // ... other fields
   };
   ```

3. **Create stage group entries** in your UI tree:
   ```cpp
   struct VfxStackEntry {
       enum Type { Stage, Module, Property };
       Type type;
       VfxStage stage;          // only if type == Stage
       VfxModule* module;       // only if type == Module
       TArray<VfxStackEntry*> children;
       bool isExpanded = true;
   };
   ```

---

## 2. Per-Module Enable/Disable (CRITICAL)

**Current state**: No per-module enable/disable toggle

**Target state**: Each module has a checkbox

### Implementation

1. **Add enable flag to module**:
   ```cpp
   struct VfxModule {
       bool enabled = true;
       // ... other fields
   };
   ```

2. **Wire checkbox to toggle**:
   ```cpp
   void toggleModuleEnabled(VfxModule& module, bool enabled) {
       module.enabled = enabled;
       broadcastEmitterChanged();  // Trigger UI refresh
   }
   ```

3. **Skip disabled modules during compilation**:
   ```cpp
   // In your shader compilation or execution code
   for (const VfxModule& mod : stage.modules) {
       if (!mod.enabled) continue;  // Skip disabled modules
       // Compile or execute module
   }
   ```

4. **In the UI, show enable state**:
   - Disabled modules: grayed out text
   - Enabled modules: normal text
   - Checkbox: ✓ (enabled) or ⊕ (disabled/inactive)

---

## 3. Add/Remove Modules per Stage (THE "+" BUTTON)

**Current state**: Module list is static

**Target state**: "+" button to add modules to each stage

### Implementation

1. **Add "+" icon to stage header**:
   ```
   Particle Update [+]
   ```

2. **When clicked, show modal menu** with compatible modules:
   ```cpp
   void showAddModuleMenu(VfxStage stage) {
       // Show modules available for this stage
       TArray<ModuleTemplate> available = getModulesForStage(stage);
       showMenu(available);
   }
   ```

3. **User selects a module**:
   ```cpp
   void addModuleToStage(VfxStage stage, ModuleTemplate template) {
       VfxModule newModule = template.instantiate();
       emitter.getModules(stage).add(newModule);
       broadcastStructureChanged();  // UI refresh
   }
   ```

4. **Refresh the stage's module list** in the UI tree.

### Module Availability

Not all modules work in all stages:

```cpp
bool canAddModuleToStage(ModuleTemplate mod, VfxStage stage) {
    switch (mod.type) {
        case ModuleType::Spawn:    // Initialize Particle, Shape Location
            return stage == VfxStage::ParticleSpawn;
        case ModuleType::Force:    // Drag, Vortex Force, etc.
            return stage == VfxStage::ParticleUpdate;
        case ModuleType::Render:   // Mesh Renderer, Sprite, etc.
            return stage == VfxStage::ParticleRender;
        default: return false;
    }
}
```

---

## 4. Reorder Modules Within a Stage (DRAG/DROP)

**Current state**: Module order is fixed

**Target state**: Drag a module to reorder it within its stage

### Implementation

1. **Make modules draggable**:
   ```cpp
   void onModuleDragStart(VfxModule& module) {
       dragSource = &module;
   }
   ```

2. **On drop, reorder the array**:
   ```cpp
   void onModuleDropOnModule(
       VfxModule& sourceMod,
       VfxModule& targetMod,
       bool dropBefore) {
       
       VfxStage stage = sourceMod.stage;
       TArray<VfxModule>& modules = emitter.getModules(stage);
       
       int32 srcIdx = modules.indexOf(sourceMod);
       int32 dstIdx = modules.indexOf(targetMod);
       
       if (dropBefore) {
           if (srcIdx < dstIdx) dstIdx--;
       } else {
           if (srcIdx < dstIdx) dstIdx--;
           dstIdx++;
       }
       
       modules.removeAt(srcIdx);
       modules.insert(dstIdx, sourceMod);
       
       broadcastStructureChanged();
   }
   ```

3. **Constraints**:
   - Can ONLY reorder within the same stage
   - Cannot drag a "Particle Spawn" module to "Particle Update"
   - Visual feedback: show drop zone indicator

---

## 5. Module Properties Panel (WHEN SELECTED)

**Current state**: All parameters visible at once?

**Target state**: Click a module → properties panel opens/updates

### Implementation

1. **Select a module**:
   ```cpp
   void onModuleSelected(VfxModule& module) {
       selectedModule = &module;
       propertyPanel->populateFromModule(module);
       propertyPanel->show();
   }
   ```

2. **Property panel displays module parameters**:
   ```cpp
   void PropertyPanel::populateFromModule(VfxModule& mod) {
       clear();
       for (const Parameter& param : mod.parameters) {
           addSlider(param.name, param.minValue, param.maxValue, param.value);
       }
   }
   ```

3. **On property change, update emitter**:
   ```cpp
   void onPropertyChanged(VfxModule& mod, const string& paramName, float value) {
       mod.parameters[paramName] = value;
       broadcastDataChanged(mod);  // Trigger emitter update
   }
   ```

---

## 6. Collapse/Expand Stages

**Current state**: All modules always visible

**Target state**: Click stage header to collapse/expand its modules

### Implementation

1. **Track expansion state**:
   ```cpp
   struct VfxStackEntry {
       VfxStage stage;
       bool isExpanded = true;  // Default expanded
   };
   ```

2. **On stage header click**:
   ```cpp
   void onStageHeaderClicked(VfxStackEntry& stageEntry) {
       stageEntry.isExpanded = !stageEntry.isExpanded;
       propertyPanel->redraw();
   }
   ```

3. **Filter children based on expansion**:
   ```cpp
   void renderStageChildren(VfxStackEntry& stage) {
       if (!stage.isExpanded) return;  // Skip rendering children
       
       for (VfxStackEntry* child : stage.children) {
           renderModule(child);
       }
   }
   ```

---

## 7. Undo/Redo Integration

**Whenever user changes**:
- Enable/disable module
- Reorder module
- Add/remove module
- Change parameter

**Must call**:
```cpp
void onEmitterChanged() {
    // Record undo transaction
    markEmitterDirty();
    
    // Notify UI to refresh
    broadcastEmitterChanged();
}
```

---

## Practical UI Hierarchy

Here's the data structure your UI should build from:

```cpp
struct VfxStackView {
    struct StageEntry {
        VfxStage stage;
        bool isExpanded = true;
        TArray<ModuleEntry> modules;
        
        FString getDisplayName() const {
            switch (stage) {
                case VfxStage::ParticleSpawn:  return "Particle Spawn";
                case VfxStage::ParticleUpdate: return "Particle Update";
                case VfxStage::ParticleRender: return "Particle Render";
                default: return "Unknown";
            }
        }
    };
    
    struct ModuleEntry {
        VfxModule* module;
        bool isSelected = false;
        
        FString getDisplayName() const {
            return module->getDisplayName();
        }
        
        bool getIsEnabled() const {
            return module->enabled;
        }
    };
    
    TArray<StageEntry> stages;
};
```

---

## Testing Checklist

- [ ] Stage headers are collapsible
- [ ] All modules are grouped by their stage
- [ ] Module enable/disable checkbox works
- [ ] Clicking "+" shows available modules for that stage
- [ ] Adding a module updates the stack
- [ ] Removing a module updates the stack
- [ ] Dragging a module reorders it within its stage
- [ ] Dropping a module outside its stage is rejected
- [ ] Module parameters update in the properties panel
- [ ] Undo/redo works for all operations
- [ ] Disabled modules are visually grayed out

---

## Anti-Patterns to Avoid

1. **Flat list instead of stages** — Makes complex emitters unreadable
2. **Hardcoded module positions** — Breaks when reordering
3. **Module enable state stored in UI only** — Gets lost on reload
4. **No undo/redo** — Users lose work
5. **Drag/drop between stages** — Breaks execution order assumptions
6. **"Add Module" shows ALL modules** — Should be filtered by stage compatibility

---

## Next Steps

1. Define your `VfxStage` enum and storage strategy
2. Update your emitter struct to have stage-organized arrays
3. Create the stack tree view data structure
4. Build the collapsible stage UI
5. Wire up the enable/disable checkbox
6. Implement drag/drop within stages
7. Add the "+" button + module selection menu
8. Build the properties panel

See `docs/analysis/niagara-module-stack-ui-architecture.md` for the deep UE5 reference.

