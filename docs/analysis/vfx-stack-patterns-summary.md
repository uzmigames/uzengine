<!-- doc-meta
title: VFX Stack Architectural Patterns Summary
description: Quick reference for Niagara stack UI patterns
tags: vfx, patterns, architecture, reference
-->

# VFX Stack Architectural Patterns — Quick Reference

## Pattern 1: Hierarchical Entry Tree (Avoids Switch-on-Type)

**Problem**: How do you represent stages, modules, properties, etc. in a single tree without switch statements?

**Niagara solution**: Single base class `UNiagaraStackEntry` for ALL elements.

```
UNiagaraStackEntry (abstract base)
├── UNiagaraStackRoot
├── UNiagaraStackScriptItemGroup (= stage)
├── UNiagaraStackModuleItem (= module)
├── UNiagaraStackFunctionInput (= parameter)
└── UNiagaraStackRenderItemGroup (= render section)
```

Each subclass implements:
- `GetDisplayName()` — "Drag", "Particle Spawn", etc.
- `GetIsEnabled()` — is this entry interactive?
- `GetFilteredChildren()` — what child entries to show?
- `CanDrop() / Drop()` — handle drag/drop

**No switch statements** — just virtual dispatch.

**For UzEngine**: Create `VfxStackEntry` with the same pattern.

---

## Pattern 2: Execution Context via FName Constants

**Problem**: How do you identify which stage a module belongs to without hardcoding strings?

**Niagara solution**: Two immutable `FName` constants.

```cpp
// In UNiagaraStackEntry
FName ExecutionCategoryName;     // "Particle", "Emitter", "Render"
FName ExecutionSubcategoryName;  // "Spawn", "Update", "Event"

// Constant definitions
static const FName FExecutionCategoryNames::Particle;
static const FName FExecutionSubcategoryNames::Spawn;
```

**Actual stage assignment**:

```cpp
// Particle Spawn stage
Category = FExecutionCategoryNames::Particle
Subcategory = FExecutionSubcategoryNames::Spawn

// Particle Update stage
Category = FExecutionCategoryNames::Particle
Subcategory = FExecutionSubcategoryNames::Update

// Emitter Spawn stage
Category = FExecutionCategoryNames::Emitter
Subcategory = FExecutionSubcategoryNames::Spawn
```

**For UzEngine**: Use enums instead of FNames, but same principle.

```cpp
enum class VfxStage {
    EmitterSpawn,    // Category::Emitter + Subcategory::Spawn
    EmitterUpdate,   // Category::Emitter + Subcategory::Update
    ParticleSpawn,   // Category::Particle + Subcategory::Spawn
    ParticleUpdate,  // Category::Particle + Subcategory::Update
    ParticleRender   // Category::Render + Subcategory::Render
};
```

---

## Pattern 3: Enable/Disable State per Module

**Problem**: How does each module track if it's enabled independently?

**Niagara solution**: Single `bool bIsEnabled` field on `UNiagaraStackModuleItem`.

```cpp
class UNiagaraStackModuleItem : public UNiagaraStackItem {
    bool bIsEnabled;  // ← Stored here, not in UI
    
    bool GetIsEnabled() const override { return bIsEnabled; }
    
    void SetIsEnabledInternal(bool bInIsEnabled) override {
        FScopedTransaction txn("Enable/Disable Module");
        FNiagaraStackGraphUtilities::SetModuleIsEnabled(*FunctionCallNode, bInIsEnabled);
        bIsEnabled = bInIsEnabled;
        OnRequestFullRefreshDeferred().Broadcast();
    }
};
```

**Key behavior**:
1. User clicks checkbox
2. `SetIsEnabledInternal()` creates an undo transaction
3. The underlying `FunctionCallNode` is modified
4. Deferred refresh broadcast (UI updates asynchronously)
5. During compilation, disabled modules are skipped

**Storage location**: NOT in UI state, but in the underlying module data structure.

**For UzEngine**:
```cpp
struct VfxModule {
    bool enabled = true;  // ← Stored here
    
    void setEnabled(bool e) {
        enabled = e;
        notifyEmitterChanged();  // Trigger UI refresh
    }
};
```

---

## Pattern 4: Stage as Script Item Group (Container Pattern)

**Problem**: How do you represent a stage that contains multiple modules?

**Niagara solution**: `UNiagaraStackScriptItemGroup` represents a single stage.

```cpp
class UNiagaraStackScriptItemGroup : public UNiagaraStackItemGroup {
    ENiagaraScriptUsage ScriptUsage;  // ParticleSpawnScript, ParticleUpdateScript, etc.
    
    // Modules within this stage are populated from the script graph
    void RefreshChildrenInternal(
        const TArray<UNiagaraStackEntry*>& CurrentChildren,
        TArray<UNiagaraStackEntry*>& NewChildren,
        TArray<FStackIssue>& NewIssues) override {
        // Query script graph for modules with matching ScriptUsage
        // Create UNiagaraStackModuleItem for each
        // Add to NewChildren
    }
};
```

**Responsibilities**:
- Display stage header ("Particle Update")
- Populate module children from script
- Handle drag/drop reordering within stage
- Provide "add module" menu

**For UzEngine**:
```cpp
struct VfxStageEntry {
    VfxStage stage;
    TArray<VfxModule*> modules;  // Populated from emitter.particleUpdateModules, etc.
    bool isExpanded = true;
    
    void addModule(VfxModule* mod) {
        modules.push_back(mod);
        notifyStructureChanged();
    }
    
    void removeModule(int idx) {
        modules.erase(idx);
        notifyStructureChanged();
    }
    
    void reorderModule(int srcIdx, int dstIdx) {
        VfxModule* mod = modules[srcIdx];
        modules.erase(srcIdx);
        modules.insert(dstIdx, mod);
        notifyStructureChanged();
    }
};
```

---

## Pattern 5: Drag/Drop Reordering (Virtual Drop Zones)

**Problem**: How do you handle drag/drop to reorder modules within a stage?

**Niagara solution**: Virtual drop zone enum + request/response pattern.

```cpp
enum class EItemDropZone {
    Above,      // Drop above the target
    Below,      // Drop below the target
    Onto        // Drop onto (not used for modules)
};

struct FDropRequest {
    TSharedRef<const FDragDropOperation> DragDropOperation;
    EItemDropZone DropZone;
};

struct FDropRequestResponse {
    TOptional<EItemDropZone> DropZone;  // TOptional = "drop not allowed"
    FText DropMessage;                  // Tooltip message
};

// In UNiagaraStackEntry
TOptional<FDropRequestResponse> CanDrop(const FDropRequest& DropRequest);
TOptional<FDropRequestResponse> Drop(const FDropRequest& DropRequest);
```

**Process**:
1. User drags module A
2. Mouse hovers over module B
3. `B.CanDrop(A)` is called
4. If valid, `EItemDropZone::Above` or `::Below` is returned
5. Visual drop zone indicator shown
6. On drop, `B.Drop(A)` is called
7. Modules reordered in the array
8. UI refreshed

**For UzEngine**:
```cpp
bool canReorderModule(VfxModule& source, VfxModule& target) {
    // Can only reorder within same stage
    return source.stage == target.stage;
}

void reorderOnDrop(VfxModule& source, VfxModule& target, bool dropBefore) {
    VfxStageEntry& stage = getStage(source.stage);
    int srcIdx = stage.modules.indexOf(source);
    int dstIdx = stage.modules.indexOf(target);
    
    if (srcIdx < dstIdx && !dropBefore) dstIdx--;
    if (srcIdx > dstIdx && dropBefore) dstIdx++;
    
    stage.modules.erase(srcIdx);
    stage.modules.insert(dstIdx, source);
    notifyStructureChanged();
}
```

---

## Pattern 6: Change Notification via Delegates

**Problem**: How does the UI stay in sync with the underlying data?

**Niagara solution**: Multicast delegates broadcast on every change.

```cpp
class UNiagaraStackEntry {
    FOnExpansionChanged OnExpansionChanged;
    FOnStructureChanged OnStructureChanged;
    FOnDataObjectModified OnDataObjectModified;
    FOnRequestFullRefresh OnRequestFullRefresh;
    FOnRequestFullRefreshDeferred OnRequestFullRefreshDeferred;  // Batched
};
```

**Usage**:
```cpp
void SetIsEnabledInternal(bool bInIsEnabled) {
    bIsEnabled = bInIsEnabled;
    OnRequestFullRefreshDeferred().Broadcast();  // Async UI refresh
}

void DeleteModule() {
    removeFromGraph();
    OnStructureChanged.Broadcast(StructureChangedFlag_ChildrenChanged);
}

void ParameterChanged() {
    OnDataObjectModified.Broadcast({ModuleObject}, EDataObjectChange::Changed);
}
```

**UI subscribes**:
```cpp
StackEntry->OnStructureChanged().AddDynamic(this, &UMyWidget::OnStackStructureChanged);
StackEntry->OnDataObjectModified().AddDynamic(this, &UMyWidget::OnStackDataChanged);
```

**For UzEngine**: Same pattern with C++ std::function or custom delegates.

```cpp
struct VfxStackEntry {
    std::function<void()> onStructureChanged;
    std::function<void()> onDataChanged;
    std::function<void()> onExpandedChanged;
};

void VfxModule::setEnabled(bool e) {
    enabled = e;
    onDataChanged();  // Broadcast to UI
}
```

---

## Pattern 7: Expansion State Tracking

**Problem**: How do you remember which stages are collapsed/expanded?

**Niagara solution**: `bool bIsExpanded` stored in `UNiagaraStackEntry`.

```cpp
class UNiagaraStackEntry {
    bool GetIsExpanded() const { return bIsExpanded; }
    void SetIsExpanded(bool bInExpanded) {
        bIsExpanded = bInExpanded;
        OnExpansionChanged().Broadcast();
    }
    
    bool IsSemanticChild() const { return false; }  // Affects indent
};
```

**Persistence**: Expansion state is saved in `UNiagaraStackEditorData`.

**For UzEngine**: Just use `bool isExpanded` on each stage entry.

```cpp
struct VfxStageEntry {
    bool isExpanded = true;
    
    void toggleExpanded() {
        isExpanded = !isExpanded;
        onExpandedChanged();
    }
};
```

---

## Pattern 8: Module Filtering by Stage (Add Module Menu)

**Problem**: When user clicks "+", how do you know which modules are valid for this stage?

**Niagara solution**: Hardcoded compatibility rules based on module type.

```cpp
class FScriptItemGroupAddUtilities : public INiagaraStackItemGroupAddUtilities {
    void CollectAddActions(
        FGraphActionListBuilderBase& OutActions,
        const UNiagaraStackEntry* StackEntry) const override {
        
        ENiagaraScriptUsage TargetUsage = GetScriptUsage();
        
        // Query module database for compatible modules
        for (const FAssetData& ModuleAsset : GetAvailableModules()) {
            UNiagaraScript* ModuleScript = Cast<UNiagaraScript>(ModuleAsset.GetAsset());
            
            // Check: is this module compatible with TargetUsage?
            if (IsModuleCompatible(ModuleScript, TargetUsage)) {
                OutActions.AddAction(CreateModuleAction(ModuleScript));
            }
        }
    }
};
```

**Compatibility rules** (hardcoded):
- Particle Spawn modules: Initialize Particle, Shape Location, Set Velocity, etc.
- Particle Update modules: Drag, Vortex Force, Point Force, Solve Forces, etc.
- Render modules: Mesh Renderer, Sprite Renderer, etc.

**For UzEngine**: Same approach.

```cpp
TArray<VfxModuleTemplate> getAvailableModulesForStage(VfxStage stage) {
    TArray<VfxModuleTemplate> available;
    
    switch (stage) {
        case VfxStage::ParticleSpawn:
            available = {
                VfxModuleTemplate::InitializeParticle,
                VfxModuleTemplate::ShapeLocation,
                VfxModuleTemplate::SetVelocity
            };
            break;
        case VfxStage::ParticleUpdate:
            available = {
                VfxModuleTemplate::Drag,
                VfxModuleTemplate::VortexForce,
                VfxModuleTemplate::PointForce,
                VfxModuleTemplate::SolveForces
            };
            break;
        case VfxStage::ParticleRender:
            available = {
                VfxModuleTemplate::MeshRenderer,
                VfxModuleTemplate::SpriteRenderer
            };
            break;
    }
    
    return available;
}
```

---

## Summary Table

| Aspect | Pattern | UE5 Class | UzEngine Equivalent |
|--------|---------|-----------|-------------------|
| **Tree structure** | Hierarchical entry base | `UNiagaraStackEntry` | `VfxStackEntry` |
| **Stage identification** | FName pair constants | `ExecutionCategoryName` + `ExecutionSubcategoryName` | `VfxStage` enum |
| **Stage container** | Script item group | `UNiagaraStackScriptItemGroup` | `VfxStageEntry` |
| **Module representation** | Stack module item | `UNiagaraStackModuleItem` | Module reference |
| **Enable/disable** | Single bool field | `bIsEnabled` | `module.enabled` |
| **Drag/drop reordering** | Drop zone enum | `EItemDropZone::Above/Below` | Drop zone enum |
| **Change notification** | Multicast delegates | `FOnStructureChanged` | Callback/observer |
| **Module filtering** | Hardcoded rules | `IsModuleCompatible()` | Switch on stage |
| **Expansion tracking** | Bool on entry | `bIsExpanded` | `stageEntry.isExpanded` |

---

## Files to Read for Deep Dive

1. **Entry base**: `NiagaraStackEntry.h` — virtual dispatch pattern
2. **Module item**: `NiagaraStackModuleItem.h` (lines 49, 114-117) — enable/disable
3. **Stage container**: `NiagaraStackScriptItemGroup.h` — module grouping
4. **Root structure**: `NiagaraStackRoot.h` — stage hierarchy creation
5. **Enable implementation**: `NiagaraStackModuleItem.cpp` (lines 1373-1379)

