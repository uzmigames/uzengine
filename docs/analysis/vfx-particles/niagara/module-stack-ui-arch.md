<!-- doc-meta
title: Niagara Module Stack UI Architecture
description: How UE5 Niagara organizes modules into stages and implements per-module enable/disable
tags: niagara, vfx, ui-architecture, stack, modules
-->

# Niagara Module Stack UI Architecture

## Overview

Niagara's module stack editor uses a **hierarchical entry tree** pattern where:

1. **Stages** are logical containers (Emitter Spawn, Emitter Update, Particle Spawn, Particle Update, Render)
2. **Modules** are leaf items within stages with independent enable/disable state
3. **Execution order** is determined by module position within a stage
4. **Drag/drop and reordering** is supported within and between compatible stages

This document extracts the UE5 architectural patterns for UzEngine to replicate.

## Key Pattern 1: Hierarchical Entry Tree

**Location**: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackEntry.h`

### Architecture

Niagara uses a single **UNiagaraStackEntry** base class that represents every UI element in the stack:

```cpp
// Base class for ALL stack elements (stages, modules, properties, etc.)
UCLASS(MinimalAPI)
class UNiagaraStackEntry : public UObject {
    // Execution context (which stage, which subscript)
    FName ExecutionCategoryName;      // "Emitter", "Particle", "System", "Render"
    FName ExecutionSubcategoryName;   // "Spawn", "Update", "Event", "SimulationStage"
    
    // Expansion and visibility
    bool GetCanExpand() const;
    bool GetCanExpandInOverview() const;
    bool GetIsExpanded() const;
    void SetIsExpanded(bool bInExpanded);
    
    // Parent-child hierarchy
    void GetFilteredChildren(TArray<UNiagaraStackEntry*>& OutFilteredChildren) const;
    void GetUnfilteredChildren(TArray<UNiagaraStackEntry*>& OutUnfilteredChildren) const;
    
    // Drag/drop operations
    FOnRequestDrop OnRequestDrop;  // Delegate for drop handling
    TOptional<FDropRequestResponse> CanDrop(const FDropRequest& DropRequest);
    TOptional<FDropRequestResponse> Drop(const FDropRequest& DropRequest);
    
    // Enable/disable state (per-entry)
    virtual bool GetIsEnabled() const;
};
```

**Key insight**: Every UI element—stage header, module, property group—is a `UNiagaraStackEntry` subclass. This avoids switch-on-type.

### Execution Category/Subcategory Names

Stages are identified by immutable name pairs:

```cpp
struct FExecutionCategoryNames {
    static const FName System;          // System-level scripts
    static const FName Emitter;         // Emitter-level scripts (stateful)
    static const FName StatelessEmitter; // Emitter-level scripts (stateless)
    static const FName Particle;        // Particle-level scripts
    static const FName Render;          // Renderer
};

struct FExecutionSubcategoryNames {
    static const FName Settings;
    static const FName Spawn;
    static const FName Update;
    static const FName Event;
    static const FName SimulationStage;
    static const FName Render;
};
```

**Usage**:
- A "Particle Spawn" stage = `Category=Particle`, `Subcategory=Spawn`
- A "Particle Update" stage = `Category=Particle`, `Subcategory=Update`
- A "Render" stage = `Category=Render`, `Subcategory=Render`

These are NOT strings—they are `FName` constants checked at initialization.

---

## Key Pattern 2: Module Representation

**Location**: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackModuleItem.h`

### Module Item Class

```cpp
UCLASS(MinimalAPI)
class UNiagaraStackModuleItem : public UNiagaraStackItem {
    // The underlying FunctionCall node in the script graph
    UNiagaraNodeFunctionCall* FunctionCallNode;
    
    // Enable/disable state (stored per-module, not per-property)
    bool bIsEnabled;
    
    // Module metadata
    FText GetDisplayName() const;       // "Drag", "Point Force", etc.
    UObject* GetDisplayedObject() const; // Returns FunctionCallNode
    FText GetTooltipText() const;
    bool SupportsChangeEnabled() const { return true; }
    
    // Per-module enable/disable
    virtual bool GetIsEnabled() const override;
    virtual void SetIsEnabledInternal(bool bInIsEnabled) override;
    
    // Module operations
    bool CanMoveAndDelete() const;
    bool CanDrag() const override { return CanMoveAndDelete(); }
    bool SupportsCut() const { return true; }
    bool SupportsCopy() const { return true; }
    bool SupportsPaste() const { return true; }
    bool SupportsDelete() const { return true; }
};
```

**Key property**: Each module has a single `bIsEnabled` boolean that controls whether it executes.

### Enable/Disable Implementation

**File**: `NiagaraStackModuleItem.cpp:1373`

```cpp
void UNiagaraStackModuleItem::SetIsEnabledInternal(bool bInIsEnabled) {
    FScopedTransaction ScopedTransaction(LOCTEXT("EnableDisableModule", "Enable/Disable Module"));
    FNiagaraStackGraphUtilities::SetModuleIsEnabled(*FunctionCallNode, bInIsEnabled);
    bIsEnabled = bInIsEnabled;
    OnRequestFullRefreshDeferred().Broadcast();  // UI refresh
}
```

**Behavior**:
1. User clicks checkbox on a module
2. `SetIsEnabledInternal()` is called
3. A **transaction** is created (for undo/redo)
4. The FunctionCallNode's enable flag is updated in the script graph
5. A deferred refresh signal is broadcast to trigger UI update
6. The module does not execute during compilation if `bIsEnabled = false`

**Storage**: The enable state is **NOT** a checkbox on the module entry—it's stored in the `UNiagaraNodeFunctionCall` object in the compiled script graph.

---

## Key Pattern 3: Stage-Based Organization

**Location**: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/NiagaraStackScriptItemGroup.h`

### Stage Container (Script Item Group)

A stage is represented as a **UNiagaraStackScriptItemGroup**:

```cpp
UCLASS(MinimalAPI)
class UNiagaraStackScriptItemGroup : public UNiagaraStackItemGroup {
    // Script usage identifies the stage
    ENiagaraScriptUsage ScriptUsage;   // ParticleSpawnScript, ParticleUpdateScript, etc.
    FGuid ScriptUsageId;
    
    // Modules within this stage are children
    // RefreshChildrenInternal() populates them from the script graph
    virtual void RefreshChildrenInternal(
        const TArray<UNiagaraStackEntry*>& CurrentChildren,
        TArray<UNiagaraStackEntry*>& NewChildren,
        TArray<FStackIssue>& NewIssues) override;
    
    // Drag/drop within the stage
    virtual TOptional<FDropRequestResponse> CanDropInternal(
        const FDropRequest& DropRequest) override;
    virtual TOptional<FDropRequestResponse> DropInternal(
        const FDropRequest& DropRequest) override;
};
```

### Script Usage Enum

```cpp
enum class ENiagaraScriptUsage {
    Function,
    Module,
    DynamicInput,
    ParticleSpawnScript,    // ← "Particle Spawn" stage
    ParticleUpdateScript,   // ← "Particle Update" stage
    ParticleEventScript,
    ParticleGPUComputeScript,
    EmitterSpawnScript,     // ← "Emitter Spawn" stage
    EmitterUpdateScript,    // ← "Emitter Update" stage
    SystemSpawnScript,      // ← "System Spawn" stage
    SystemUpdateScript,     // ← "System Update" stage
};
```

**Mapping to UI**:
- `ParticleSpawnScript` → "Particle Spawn" stage header
- `ParticleUpdateScript` → "Particle Update" stage header
- `EmitterSpawnScript` → "Emitter Spawn" stage header
- etc.

### Stage Hierarchy

The root entry (`UNiagaraStackRoot`) creates stage groups dynamically:

**File**: `NiagaraStackRoot.h:46-48`

```cpp
void RefreshEmitterFullChildren(
    const TArray<UNiagaraStackEntry*>& CurrentChildren,
    TArray<UNiagaraStackEntry*>& NewChildren);
void RefreshEmitterSummaryChildren(...);
void RefreshEmitterStatelessChildren(...);
```

Each `RefreshXxxChildren()` function:
1. Iterates the script graph's simulation stages
2. For each script usage (Spawn, Update, etc.), creates or reuses a `UNiagaraStackScriptItemGroup`
3. Sets `ExecutionCategoryName` and `ExecutionSubcategoryName` on each group
4. Returns the ordered list of stage groups to display

---

## Key Pattern 4: Module Reordering (Within a Stage)

**File**: `NiagaraStackScriptItemGroup.cpp`

Modules within a stage can be reordered via drag/drop. The implementation:

1. **Can drop on target**: `CanDropEntriesOnTarget()` validates the drop zone
2. **Perform drop**: `DropEntriesOnTarget()` reorders the modules in the script graph

```cpp
// Pseudo-code from Niagara
void DropEntriesOnTarget(
    const UNiagaraStackEntry& TargetEntry,
    const FDropRequest& DropRequest) {
    
    // Get the source and target module nodes
    UNiagaraNodeFunctionCall* SourceModule = GetModuleNode(DropRequest.DraggedEntries[0]);
    UNiagaraNodeFunctionCall* TargetModule = GetModuleNode(TargetEntry);
    
    // Reorder in the script graph
    // This typically involves:
    // 1. Removing the source module from its current position
    // 2. Inserting it before/after the target module
    // 3. Updating the script graph's internal node list
    // 4. Recompiling to update execution order
}
```

**Constraints**:
- Modules can only be reordered WITHIN their stage (e.g., within "Particle Update")
- Cannot move a "Particle Spawn" module to "Particle Update"
- Cannot move a "Render" module to "Particle Update"

---

## Key Pattern 5: Adding/Removing Modules

### Add Module (The "+" Button)

**File**: `INiagaraStackItemGroupAddUtilities.h`

Each stage group has associated "add utilities" that present available modules:

```cpp
class INiagaraStackItemGroupAddUtilities {
    // Get the list of available modules for this stage
    virtual void CollectAddActions(
        FGraphActionListBuilderBase& OutActions,
        const UNiagaraStackEntry* StackEntry) const = 0;
    
    // Execute add action (user selected a module from the menu)
    virtual void ExecuteAction(
        const TSharedPtr<FNiagaraAction>& Action) = 0;
};
```

**Usage**:
1. User clicks "+" on "Particle Update" stage
2. The stage group's add utilities present a searchable menu of compatible modules
3. User selects "Drag" (or "Point Force", etc.)
4. `ExecuteAction()` is called
5. The module FunctionCallNode is added to the script graph at the stage's position
6. `RefreshChildrenInternal()` is called to update the UI tree

### Remove Module (Delete or Cut)

```cpp
class UNiagaraStackModuleItem {
    virtual bool SupportsDelete() const { return true; }
    virtual FText GetDeleteTransactionText() const;
    virtual void Delete() override;  // Removes the FunctionCallNode from the script graph
    
    virtual bool SupportsCut() const { return true; }
    virtual void RemoveForCut() override;  // Removes the node, leaves clipboard content
};
```

---

## Key Pattern 6: Per-Module Properties (The Property Panel)

When a module is selected, a **property panel** appears. This is handled by separate widget classes:

**File**: `NiagaraStackModuleItem.h:77-78`

```cpp
void GetParameterInputs(TArray<class UNiagaraStackFunctionInput*>& OutResult) const;
TArray<class UNiagaraStackFunctionInput*> GetInlineParameterInputs() const;
```

Each module can expose:
- **Inline parameters** — small controls displayed directly in the stack list (e.g., slider for "Force Magnitude")
- **Linked inputs** — connections to other variables (e.g., "Force Direction" from velocity)
- **Output collection** — what this module outputs to downstream modules

These are child entries of the module:

```
Module: "Drag"
├── Input: "Drag Coefficient" (slider, 0.0-1.0)
├── Input: "Velocity Reference" (linked input picker)
└── Output: Modified Velocity
```

Each input/output is itself a `UNiagaraStackEntry` subclass.

---

## Key Pattern 7: Drag/Drop Across Stages (NOT Supported in Base Niagara)

**Critical observation**: Standard Niagara **does not support** moving modules between stages via drag/drop at the UI level.

**Why**: Modules are tightly bound to their script usage. A ParticleSpawnScript module cannot execute in ParticleUpdateScript context because:
1. The script graph would be malformed
2. Variable read/write order would break
3. Compilation would fail

**If UzEngine needs cross-stage drag/drop**, the architecture would require:
1. Validation that the module is compatible with the target stage
2. Re-binding the module to a different script usage
3. Recompiling both source and target scripts
4. This is a significant undertaking not supported by the Niagara UI

---

## Key Pattern 8: Refresh Signals (Observer Pattern)

**Location**: `NiagaraStackEntry.h:104-110`

Each entry broadcasts change notifications:

```cpp
DECLARE_MULTICAST_DELEGATE(FOnExpansionChanged);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnStructureChanged, ENiagaraStructureChangedFlags);
DECLARE_MULTICAST_DELEGATE_TwoParams(FOnDataObjectModified, TArray<UObject*>, ENiagaraDataObjectChange);
DECLARE_MULTICAST_DELEGATE(FOnRequestFullRefresh);
DECLARE_MULTICAST_DELEGATE(FOnRequestFullRefreshDeferred);
```

**Usage**:
- When a module is enabled/disabled → `OnRequestFullRefreshDeferred()` broadcast
- When a module is added → `OnStructureChanged` broadcast
- When properties change → `OnDataObjectModified` broadcast
- The UI tree listens to these and updates accordingly

**Key**: Refreshes are **deferred** (batched) to avoid thrashing the UI during rapid changes.

---

## UzEngine Adaptation

### Recommended Architecture

1. **Entry Base Class** (`VfxStackEntry`)
   - Replaces Niagara's `UNiagaraStackEntry`
   - Stores execution context (stage, module index)
   - Implements hierarchy, expansion, enable/disable

2. **Module Wrapper** (`VfxModuleEntry`)
   - Replaces Niagara's `UNiagaraStackModuleItem`
   - Wraps the underlying emitter module struct
   - Handles enable/disable toggle
   - Stores `bIsEnabled` state

3. **Stage Container** (`VfxStageEntry`)
   - Replaces Niagara's `UNiagaraStackScriptItemGroup`
   - Represents "Particle Spawn", "Particle Update", etc.
   - Children are `VfxModuleEntry` instances
   - Handles module reordering via drag/drop

4. **UI Widget** (`SvfxStackPanel`)
   - Reads `VfxStackEntry` tree
   - Renders stages as collapsible sections
   - Renders modules with enable/disable checkbox
   - Wires drag/drop to reorder operations

5. **Property Panel** (`SvfxModuleProperties`)
   - Reads selected `VfxModuleEntry`
   - Displays module parameters
   - Inline controls + linked input pickers

### Key Differences from Niagara

| Aspect | Niagara | UzEngine |
|--------|---------|----------|
| Script system | Niagara VM + compiled script graphs | Custom compute module system |
| Module storage | `UNiagaraNodeFunctionCall` in UObject graph | Emitter struct with module array |
| Enable/disable | Stored in FunctionCallNode metadata | Module has `enabled: bool` field |
| Stage identification | `ENiagaraScriptUsage` enum + `FGuid` | Module index + stage enum |
| Add menu | Asset registry + script database | Hardcoded module list |
| Drag/drop | Reorders FunctionCallNodes in graph | Reorders module array indices |

---

## Implementation Checklist for UzEngine

- [ ] Create `VfxStackEntry` base class with:
  - [ ] Execution context (stage, module index)
  - [ ] Parent/child hierarchy
  - [ ] Expansion state tracking
  - [ ] Change notification delegates
  
- [ ] Create `VfxModuleEntry` with:
  - [ ] `bIsEnabled` field
  - [ ] Mapping to emitter.modules[index]
  - [ ] Enable/disable toggle implementation
  - [ ] Drag/drop support
  
- [ ] Create `VfxStageEntry` with:
  - [ ] Stage name (Spawn, Update, Render)
  - [ ] Module children population
  - [ ] Module reordering logic
  - [ ] Add module menu
  
- [ ] Create `SvfxStackPanel` widget:
  - [ ] Hierarchical tree display
  - [ ] Collapsible stage sections
  - [ ] Enable/disable checkboxes
  - [ ] Module drag/drop support
  
- [ ] Create `SvfxModuleProperties`:
  - [ ] Parameter display
  - [ ] Slider/picker controls
  - [ ] Update emitter on change
  - [ ] Undo/redo integration

---

## References

- **UE5 Niagara Stack**: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/Stack/`
- **Module Item**: `NiagaraStackModuleItem.h` (enable/disable implementation at line 1373)
- **Stage Container**: `NiagaraStackScriptItemGroup.h` (module grouping)
- **Root Structure**: `NiagaraStackRoot.h` (stage hierarchy creation)
- **Entry Base**: `NiagaraStackEntry.h` (hierarchical entry tree pattern)

