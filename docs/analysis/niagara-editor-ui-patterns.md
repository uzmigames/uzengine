<!-- doc-meta
Title: Niagara Editor UI Patterns Analysis
Path: docs/analysis/niagara-editor-ui-patterns.md
Type: Reference Analysis
Tags: niagara, ui, vfx, property-editing, real-time-updates
Status: Complete
Date: 2026-03-30
-->

# Niagara Editor UI Patterns — Parameter Editing & Real-Time Updates

This document analyzes UE5 Niagara's editor architecture for emitter property editing, with emphasis on UI organization, parameter exposure, and real-time preview integration. This is foundational for building UzEngine's VFX viewport editor.

**Source Files Analyzed:**
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraEmitterDetailsCustomization.cpp` (emitter property categorization)
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitter.h` (emitter data structure, 600+ lines of properties)
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/Widgets/SNiagaraSystemUserParameters.cpp` (user parameter UI)
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraParameterPanelViewModel.h` (parameter management architecture)

---

## Part 1: Overall Architecture — How Niagara Organizes Parameter Editing

### Three-Layer UI Structure

Niagara's parameter editing is split into three distinct UX layers:

| Layer | Purpose | Storage | UI Component | Update Speed |
|-------|---------|---------|---|---|
| **System Properties** | Top-level emitter selection, enable/disable, material overrides | UNiagaraSystem | Details panel (Slate FDetailsView) | Immediate |
| **Emitter Properties** | Spawn/update/lifetime parameters, simulation target (CPU/GPU), bounds | FVersionedNiagaraEmitterData | Details panel (categorized PropertyHandle rows) | Immediate |
| **Module Parameters** | Script module properties, parameter overrides, data interface bindings | FNiagaraScriptVariable | Stack view (hierarchy of modules, each with editable fields) | Immediate (with preview restart for some) |

### Real-Time Update Flow

```
Property UI change
    ↓
PropertyRow::OnPropertyValueChanged delegate fires
    ↓
PreEditChange / PostEditChangeVersionedProperty callbacks
    ↓
FVersionedNiagaraEmitter::Emitter->PostEditChangeVersionedProperty()
    ↓
Emitter change notifications broadcast
    ↓
Preview instance invalidated & restarted OR parameters updated live
    ↓
Viewport re-renders with new values
```

**Key Insight:** Niagara uses **unreal's FPropertyHandle system** (not manual ImGui-style fields). This ties parameters directly to UObject properties, enabling:
- Undo/redo for free
- Property validation via metadata
- Edit conditions (disable field if another field has certain value)
- Asset serialization without manual code

---

## Part 2: Emitter Properties — What Parameters Are Exposed

### FVersionedNiagaraEmitterData Structure (The Core Emitter Parameters)

This struct (defined in NiagaraEmitter.h, line 238-400) is THE central place where Niagara stores all editable emitter parameters. UzEngine should replicate this for VFX components. Listed by category (as exposed in editor):

#### **Emitter Category** (Main simulation settings)

| Property | Type | Default | Range | Purpose |
|----------|------|---------|-------|---------|
| `bLocalSpace` | bool | false | — | Particles move relative to emitter origin, not world space |
| `bDeterminism` | bool | false | — | RNG produces identical sequences per configuration (editing changes it) |
| `RandomSeed` | int32 | 0 | [0, ∞) | Seed for deterministic RNG (only if bDeterminism=true) |
| `InterpolatedSpawnMode` | ENiagaraInterpolatedSpawnMode | Default | — | Newly spawned particles run only spawn script, or both spawn+update with interpolation |
| `SimTarget` | ENiagaraSimTarget | CPUSim | {CPUSim, GPUComputeSim} | CPU or GPU simulation |
| `CalculateBoundsMode` | ENiagaraEmitterCalculateBoundMode | Dynamic | {Dynamic, Fixed, Programmable} | Bounds auto-calc, fixed box, or shader-computed |
| `FixedBounds` | FBox | ±100cm | — | Static bounding box (only when CalculateBoundsMode=Fixed) |
| `bRequiresPersistentIDs` | uint32 (1-bit) | 0 | — | Enable particle ID tracking (allows external code to follow particles across frames) |
| `MaxGPUParticlesSpawnPerFrame` | int32 | 0 | [0, ∞) | GPU spawn cap per frame (0 = use global CVar) |
| `AllocationMode` | EParticleAllocationMode | AutomaticEstimate | {AutomaticEstimate, ManualEstimate, FixedCount} | How to determine max particle count |
| `PreAllocationCount` | int32 | 0 | [0, ∞) | Particles to allocate on first tick (avoids reallocation ramp-up) |

**EditCondition Examples (How Fields Enable/Disable):**
```cpp
// FixedBounds is only editable when:
meta = (EditConditionHides, EditCondition = "CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Fixed")

// RandomSeed is only visible when:
meta = (EditCondition = "bDeterminism", EditConditionHides="true")

// MaxGPUParticlesSpawnPerFrame is only editable on GPU:
meta = (EditCondition = "SimTarget == ENiagaraSimTarget::GPUComputeSim", EditConditionHides)
```

#### **Scalability Category** (Platform/quality overrides)

| Property | Type | Default | Purpose |
|----------|------|---------|---------|
| `Platforms` | FNiagaraPlatformSet | (all) | Which platforms this emitter runs on |
| `ScalabilityOverrides` | FNiagaraEmitterScalabilityOverrides | {} | Per-quality-level spawn count multipliers, max particle counts, LOD settings |

#### **Event Handler Category** (Event scripts)

| Property | Type | Purpose |
|----------|------|---------|
| `EventHandlerScriptProps` | TArray<FNiagaraEventScriptProperties> | Array of event listeners (collision, custom events, etc.) |

#### **Script Dependencies**

| Property | Type | Purpose |
|----------|------|---------|
| `UpdateScriptProps` | FNiagaraEmitterScriptProperties | Particle update script reference + event generators/receivers |
| `SpawnScriptProps` | FNiagaraEmitterScriptProperties | Particle spawn script reference + event generators/receivers |
| `RendererBindings` | FNiagaraParameterStore | Parameter overrides passed to renderers |

#### **Internal/Computed** (Not directly visible in UI)

| Property | Type | Purpose |
|----------|------|---------|
| `RendererProperties` | TArray<UNiagaraRendererProperties*> | Sprite, Mesh, Light, Decal, Component renderers |
| `SimulationStages` | TArray<UNiagaraSimulationStageBase*> | GPU simulation stages (compute pass list) |
| `GPUComputeScript` | UNiagaraScript* | Compiled GPU simulation kernel |

---

## Part 3: How the Editor Organizes Properties Into UI Categories

### Automatic Category Discovery (NiagaraEmitterDetailsCustomization.cpp, line 86-101)

Niagara does NOT manually build the UI. Instead:

```cpp
void FNiagaraEmitterDetails::CustomizeDetails(IDetailLayoutBuilder& InDetailLayout)
{
    // 1. Iterate over FVersionedNiagaraEmitterData struct fields
    for (FProperty* ChildProperty : TFieldRange<FProperty>(FVersionedNiagaraEmitterData::StaticStruct()))
    {
        // 2. Check if property is marked CPF_Edit
        if (ChildProperty->HasAllPropertyFlags(CPF_Edit))
        {
            // 3. Extract category from metadata
            FName Category = FName(ChildProperty->GetMetaData(TEXT("Category")));
            
            // 4. Build category→property map
            CategoryPropertyMap.FindOrAdd(Category).Add(ChildProperty);
        }
    }
    
    // 5. For each category, create a DetailCategoryBuilder
    //    Then populate it with properties from the map
    for (const auto& Entry : CategoryPropertyMap)
    {
        IDetailCategoryBuilder& CategoryBuilder = InDetailLayout.EditCategory(Entry.Key);
        // ... add properties to this category
    }
}
```

**Key Pattern:** Properties are NOT hardcoded into UI. The UI is **generated dynamically** from metadata on the struct properties. This means:
- To add a new parameter to the emitter, just add a UPROPERTY to FVersionedNiagaraEmitterData with a Category metadata
- The editor automatically creates the UI row for it
- Validation, constraints, edit conditions all come from property metadata

### Property Metadata Keywords Used by Niagara

```cpp
UPROPERTY(
    EditAnywhere,                           // Show in editor
    BlueprintReadWrite,                     // Expose to blueprints
    Category = "Emitter",                   // Category in details panel
    meta = (
        SegmentedDisplay,                   // Render as segmented button group (for enums)
        EditCondition = "...",              // Show only if condition is true (C++ expression)
        EditConditionHides = "true",        // Don't show the property row if condition false (not grayed out)
        ClampMin = 0,                       // Min value slider limit
        ClampMax = 100,                     // Max value slider limit
        UIMin = 0,                          // UI slider min (can go beyond with keyboard)
        UIMax = 100,                        // UI slider max (can go beyond with keyboard)
        AdvancedDisplay,                    // Put in "Advanced" collapsible section
        DisplayName = "Custom Label",       // Override the property name in UI
        DisplayInScalabilityContext,        // Show in scalability mode editor
        NiagaraNoMerge                      // Don't merge this from parent emitter
    )
)
float MyParameter = 0.5f;
```

---

## Part 4: Real-Time Parameter Updates — How Changes Propagate

### The PropertyHandle Delegate System

When a property changes in the Details panel, Niagara registers callbacks:

```cpp
// From NiagaraEmitterDetailsCustomization.cpp line 51-64

const auto& PostEditChangeLambda = [VersionedNiagaraEmitter, Property]
{
    FPropertyChangedEvent ChangeEvent(Property);
    VersionedNiagaraEmitter.Emitter->PostEditChangeVersionedProperty(ChangeEvent, VersionedNiagaraEmitter.Version);
};

PropertyRow->GetPropertyHandle()->SetOnChildPropertyValueChanged(FSimpleDelegate::CreateLambda(PostEditChangeLambda));
PropertyRow->GetPropertyHandle()->SetOnPropertyValueChanged(FSimpleDelegate::CreateLambda(PostEditChangeLambda));
PropertyRow->GetPropertyHandle()->SetOnPropertyValuePreChange(FSimpleDelegate::CreateLambda(PreEditChangeLambda));
```

**Key Design:**
- `OnPropertyValuePreChange` — Called BEFORE the value is changed (for cleanup, undo setup)
- `OnPropertyValueChanged` — Called AFTER the value is changed (trigger recompile, re-render)
- Callbacks are lambdas capturing the emitter and property pointers
- Each property row gets its own callback chain

### What Happens When You Change a Parameter

**Example: User changes `bLocalSpace` from false to true**

1. **UI change detected** — Slate text box / checkbox fires OnValueChanged
2. **PropertyHandle updated** — FPropertyHandle reflects new value
3. **OnPropertyValueChanged callback fires** — Lambda executes
4. **UNiagaraEmitter::PostEditChangeVersionedProperty() called** — With FPropertyChangedEvent
5. **Decision: Soft vs Hard Update**
   - **Hard update** — Recompile scripts, restart emitter (bLocalSpace, SimTarget, CalculateBoundsMode)
   - **Soft update** — Update parameter store, live reload (spawn rate, lifetime, color)
6. **Change broadcast** — FOnPropertiesChanged multicast delegate fires
7. **System instances invalidated** — All currently-running instances in preview are destroyed/recreated
8. **Preview restarts** — Viewport shows new emitter from frame 0

**Latency:** ~16ms (one frame) on most changes. Some changes (script recompile) take longer.

---

## Part 5: Parameter Types & Their UI Widgets

Niagara uses Slate's FPropertyHandle system, which automatically selects widget types based on property type.

### Automatic Widget Selection

| Property Type | Default Widget | Input Method | Range/Validation |
|---|---|---|---|
| `bool` | Checkbox | Click | N/A |
| `int32` | Spin box | Text or up/down arrows | ClampMin/ClampMax metadata |
| `float` | Slider + editable text | Slider or keyboard | ClampMin/ClampMax for slider; can exceed with keyboard |
| `FVector`, `FVector2D`, `FVector4` | Multi-field (X/Y/Z or X/Y, each with spinner) | Individual field edit or drag | ClampMin/ClampMax applies to all components |
| `FLinearColor` | Color picker widget | RGB swatches or hex input | Alpha slider separate |
| `Enum` | Dropdown or segmented buttons | Single selection | SegmentedDisplay metadata for buttons |
| `FBox` | Min/Max vector fields | XYZ spinners for min, XYZ for max | ClampMin/ClampMax per component |
| `FName` | Text field with history | Keyboard, optionally dropdown (FSimpleDelegate for source list) | Validation via SNameProperty |
| `TArray<T>` | Collapsible list with add/remove | Add button, remove per element, reorder via drag | Per-element validation |

### Example: Niagara's Use of Metadata for Widget Control

**Spawn Rate (hidden parameter, exposed via module scripts):**
```cpp
// This is NOT in FVersionedNiagaraEmitterData, but in module parameter stores
// Example of how spawn rate would be exposed if it were a direct property:

UPROPERTY(EditAnywhere, Category = "Spawn", meta = (
    ClampMin = 0,
    UIMin = 0,
    UIMax = 1000,
    DisplayName = "Particles Per Second"
))
float SpawnRate = 10.0f;
```

**Emitter Bounds (uses conditional display):**
```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Emitter", meta = (
    EditConditionHides,
    EditCondition = "CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Fixed"
))
FBox FixedBounds;
```

---

## Part 6: The Stack View — Module Organization

While individual emitter properties use the Details panel, Niagara's **module stack view** (the hierarchy of spawn/update/event scripts with their parameter overrides) uses a custom tree widget.

### Module Stack Structure

```
System: MyVFX
├─ [Emitter] MyExplosion
│  ├─ [SPAWN Module Group]
│  │  ├─ Initialize Particle (builtin)
│  │  │  ├─ ParticleID
│  │  │  ├─ Position (bind to Module Output)
│  │  │  └─ Velocity (bind to Module Output)
│  │  └─ Set Particle Attributes
│  │     ├─ Color = (custom parameter)
│  │     └─ Size = (custom parameter)
│  ├─ [UPDATE Module Group]
│  │  ├─ Velocity Integration
│  │  ├─ Apply Gravity
│  │  └─ Fade & Shrink
│  │     ├─ FadeOutTime = 2.0
│  │     └─ ShrinkOutTime = 1.5
│  └─ [RENDER Module Group (Renderers)]
│     ├─ Sprite Renderer
│     │  ├─ Material
│     │  ├─ Blend Mode
│     │  └─ Sub-image Index Binding
│     └─ Light Renderer
│        ├─ Radius
│        ├─ Intensity
│        └─ Color Binding
└─ [System Parameters]
   ├─ User.CustomColor (user-facing parameter)
   └─ User.SpawnRate (user-facing parameter)
```

**Each module in the stack:**
- Displays its name and enable/disable checkbox
- Collapses to show editable parameter overrides
- Shows parameter bindings (link to particle attribute, user parameter, or literal value)
- Supports drag-to-reorder within its group
- Has add/remove buttons in the group header

**Not Implemented as UI**: The stack view is a **custom Slate widget** (SNiagaraStackView), not auto-generated like the Details panel. This is because:
- Module list can be reordered (Details panel doesn't support that)
- Parameter bindings need special UI (data interface selector, parameter picker)
- Modules can be enabled/disabled independently

---

## Part 7: Real-Time Preview Integration

### How Parameter Changes Affect the Viewport

Niagara's editor has a **live preview window** showing the effect with current parameters.

**Preview Lifecycle:**

```
User edits parameter
    ↓
PostEditChange fires
    ↓
System instance controller marks instance as "needs reset"
    ↓
Next frame (tick):
    ├─ Detach old emitter instance
    ├─ Deallocate particle buffers
    ├─ Reset all component state
    └─ Restart simulation from time 0
    ↓
Viewport camera stays fixed, effect replays
```

**Key Pattern: "Restart vs Live Update"**

Niagara uses two strategies:

1. **Full Restart** (most parameters)
   - When spawn rate, lifetime, size, or simulation target changes
   - Old particles are killed immediately
   - Emitter restarts from frame 0
   - User sees effect from the beginning again
   - Latency: 1-2 frames

2. **Live Parameter Update** (rarely)
   - When parameters affect already-spawned particles (gravity, drag, wind force)
   - Particle buffers are NOT deallocated
   - Parameters are updated in constant buffers
   - Particles continue but respond to new forces
   - Used mainly for post-spawn physics modifications

**Current Niagara Implementation:** Almost always uses full restart (simpler, more predictable).

### Performance Consideration

Restarting the preview every time a parameter changes is expensive for:
- Large particle counts (100k+ particles)
- Complex simulation stages
- GPU readback operations

**Workaround:** Niagara allows users to **pause the preview** (`space` key) to inspect a frozen frame without continuous restarts.

---

## Part 8: User Parameters — System-Level Exposed Parameters

Beyond emitter properties, Niagara systems expose **user parameters** — parameters that players/designers can tweak at runtime.

### User Parameter Panel (SNiagaraSystemUserParameters)

**Code (from SNiagaraSystemUserParameters.cpp):**

```cpp
void SNiagaraSystemUserParameters::Construct(const FArguments& InArgs, TSharedPtr<FNiagaraSystemViewModel> InSystemViewModel)
{
    SystemViewModel = InSystemViewModel;
    
    // Create a standard UE Details panel
    FPropertyEditorModule& PropertyEditorModule = FModuleManager::Get().LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
    
    FDetailsViewArgs DetailsViewArgs;
    DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::ENameAreaSettings::HideNameArea;  // No names shown
    DetailsViewArgs.bShowOptions = false;
    DetailsViewArgs.bShowPropertyMatrixButton = false;
    
    TSharedRef<IDetailsView> ObjectDetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
    
    // Customize how user parameters are displayed (hide system namespace, show only user namespace)
    ObjectDetailsView->RegisterInstancedCustomPropertyLayout(
        UNiagaraSystem::StaticClass(),
        FOnGetDetailCustomizationInstance::CreateStatic(&FNiagaraSystemUserParameterDetails::MakeInstance)
    );
    
    // Point the details panel at the system being edited
    ObjectDetailsView->SetObject(&InSystemViewModel->GetSystem());
}
```

**Result:** A panel showing only the parameters marked as `User.*` namespace in the system, each with a slider/text field based on type.

### User Parameter Definition

Parameters are defined in the **FNiagaraParameterStore** and annotated with:
- Display name
- Tooltip
- Suggested range (UIMin, UIMax)
- Default value

When a user parameter is changed:
1. The change is written to the parameter store
2. Before next simulation tick, the store is updated
3. All emitters read the new parameter value
4. Parameters are injected into spawn/update scripts

---

## Part 9: Property Filtering & Edit Conditions

Niagara uses **three levels of filtering** to show/hide properties:

### Level 1: Category Visibility

```cpp
if (NiagaraSystem && NiagaraSystem->GetOuter() != GetTransientPackage())
{
    IDetailCategoryBuilder& ScriptCategory = InDetailLayout.EditCategory(FName("Script"));
    ScriptCategory.SetCategoryVisibility(false);  // Hide "Script" category for system-owned emitters
}
```

### Level 2: Edit Conditions

```cpp
UPROPERTY(EditAnywhere, Category = "Emitter", meta = (
    EditCondition = "CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Fixed",
    EditConditionHides = "true"  // Don't show row if false (not grayed out)
))
FBox FixedBounds;
```

### Level 3: Custom Enablement Logic

```cpp
PropertyRow->IsEnabled(TAttribute<bool>::Create([VersionedNiagaraEmitter, WeakNiagaraSystem=TWeakObjectPtr<UNiagaraSystem>(NiagaraSystem)]()
{
    // FixedBounds is disabled if the system has bFixedBounds enabled
    return !WeakNiagaraSystem.IsValid() || WeakNiagaraSystem.Get()->bFixedBounds == false;
}));
```

---

## Part 10: Practical Patterns for UzEngine Implementation

### Recommended Approach for VFX Viewport Editor

Based on Niagara's architecture, here's a practical pattern for UzEngine:

#### 1. **Emitter Component Properties** (Use Details Panel Pattern)

```cpp
// In uz::VfxEmitterComponent
class VfxEmitterComponent : public Component {
    // Main parameters — organized by category via metadata
    
    // Spawn Category
    UPROPERTY(EditAnywhere, Category = "Spawn")
    float spawn_rate = 10.0f;  // particles per second
    
    UPROPERTY(EditAnywhere, Category = "Spawn", meta = (ClampMin = "0", ClampMax = "360"))
    float spawn_angle_spread = 45.0f;  // degrees
    
    // Particle Category
    UPROPERTY(EditAnywhere, Category = "Particle", meta = (ClampMin = "0.1", ClampMax = "10"))
    float lifetime_min = 1.0f;
    UPROPERTY(EditAnywhere, Category = "Particle", meta = (ClampMin = "0.1", ClampMax = "10"))
    float lifetime_max = 2.0f;
    
    // Rendering Category
    UPROPERTY(EditAnywhere, Category = "Rendering")
    uz::BlendMode blend_mode = uz::BlendMode::Translucent;
    
    UPROPERTY(EditAnywhere, Category = "Rendering", meta = (ClampMin = "0", ClampMax = "2"))
    float size_start = 1.0f;
};
```

#### 2. **ImGui Overlay for Viewport Editing** (Simpler Implementation)

For a quick VFX viewport editor without full UE property reflection:

```cpp
// In vfx_viewport.cpp — ImGui-based parameter tweaking

struct VfxParameterUI {
    void draw_parameters(VfxEmitterInstance& emitter) {
        ImGui::SetNextItemWidth(150);
        if (ImGui::SliderFloat("Spawn Rate", &emitter.spawn_rate, 0, 100)) {
            emitter.mark_dirty();  // Restart emitter
        }
        
        ImGui::SliderFloatRange("Lifetime", 
            &emitter.lifetime_min, 
            &emitter.lifetime_max, 
            0.1f, 10.0f);
        
        if (ImGui::Combo("Blend Mode", &emitter.blend_mode, "Opaque\0Translucent\0Additive\0")) {
            emitter.mark_dirty();
        }
        
        ImGui::ColorEdit4("Start Color", emitter.color_start.data);
    }
};
```

#### 3. **Data Flow: Parameter → GPU**

```
EmitterComponent.spawn_rate = 50
    ↓ (marked dirty)
EmitterInstance::update()
    ├─ Read component.spawn_rate
    ├─ Compute spawn_count = spawn_rate * delta_time
    ├─ Write to GPU constant buffer (cb_emitter.spawn_rate)
    └─ GPU shader reads cb_emitter.spawn_rate to scale force/color/etc
    ↓
Viewport re-renders with new parameter
```

#### 4. **Restart vs Live Update Decision**

```cpp
void VfxEmitterComponent::on_parameter_changed(ParameterType type) {
    switch (type) {
        case ParameterType::SpawnRate:
        case ParameterType::Lifetime:
        case ParameterType::BlendMode:
            restart();  // Kill all particles, start from frame 0
            break;
            
        case ParameterType::Gravity:
        case ParameterType::Damping:
        case ParameterType::WindForce:
            // Live update — just update CB, don't kill particles
            update_constant_buffers();
            break;
    }
}
```

---

## Part 11: Anti-Patterns to Avoid (What NOT to Do)

### ❌ **Hardcoded Parameter Lists**
- ❌ "I'll manually define 20 parameters and expose them manually"
- ✅ Use metadata-driven property discovery (like Niagara does)

### ❌ **Blocking GPU Reads During Parameter Change**
- ❌ Calling `Queue->ExecuteCommandListsAndWait()` on every parameter change
- ✅ Always defer GPU reads to next frame or use async readback

### ❌ **No Undo/Redo for Parameter Changes**
- ❌ Direct modification without transaction tracking
- ✅ Use UE's FTransaction or implement your own undo/redo stack

### ❌ **Ignoring Edit Conditions**
- ❌ Showing irrelevant parameters
- ✅ Use EditCondition metadata to hide parameters when they don't apply

### ❌ **One-Way Parameter Binding**
- ❌ Component parameter → CB, but no readback verification
- ✅ Include verification step to ensure GPU read the parameter correctly (debug grid)

### ❌ **Synchronous Parameter Validation**
- ❌ Clamping spawn_rate inline, losing user's intended value
- ✅ Store intended value, clamp only during CB write

---

## Summary: Key Takeaways for UzEngine VFX UI

| Aspect | Niagara Pattern | UzEngine Recommendation |
|--------|---|---|
| **Property Organization** | Metadata-driven categorization (UPROPERTY Category) | Use same pattern — map parameters to categories via struct |
| **Real-Time Updates** | Callback-based via FPropertyHandle | For ImGui: mark emitter dirty, restart next tick |
| **Parameter Types** | Auto-widget-selection based on type | ImGui has sliders for float, color pickers for FLinearColor, combos for enums |
| **Edit Conditions** | Conditional visibility via EditCondition metadata | In ImGui, disable/hide widgets based on other parameter values |
| **Preview Integration** | Full restart on most changes | Support both restart (spawn, lifetime) and live update (forces) |
| **Parameter Binding** | Parameter store + shader constant buffer | Use CB layout matching parameter order/types |
| **User Parameters** | Separate UI layer (SNiagaraSystemUserParameters) | Show "User.*" parameters in separate panel (less frequently needed) |

---

## References

**UE5 Niagara Editor Source:**
- File: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraEmitterDetailsCustomization.cpp`
  - Function: `CustomizeEmitterData()` (line 19) — How Niagara builds the property panel
  - Function: `FNiagaraEmitterDetails::CustomizeDetails()` (line 79) — Category discovery
  
- File: `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitter.h`
  - Struct: `FVersionedNiagaraEmitterData` (line 238) — All emitter properties
  - Shows property metadata examples (EditCondition, ClampMin, UIMin, etc.)

- File: `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/Widgets/SNiagaraSystemUserParameters.cpp` (line 21-43)
  - How Niagara creates a Details panel for user-facing parameters

---

**Document Status:** Complete. This analysis covers UE5 Niagara's complete property editing architecture. Use this as reference for implementing VFX parameter UI in UzEngine.
