# Analysis Documents

Reference implementations and architectural patterns extracted from engine source code.

## VFX System — Niagara Editor Analysis

Complete analysis of UE5 Niagara's editor architecture for real-time VFX parameter editing.

### Documents

1. **niagara-editor-ui-patterns.md** — Complete UI architecture
   - How Niagara organizes emitter properties (categories, sections, parameter groups)
   - Widget types for each parameter (slider, color picker, dropdown, etc.)
   - Real-time update flow (parameter change → emitter restart → preview)
   - Module stack organization (spawn/update/render modules)
   - Property metadata system (EditCondition, ClampMin, UIMin, etc.)
   - User parameter exposure mechanism
   - ~1800 lines of detailed reference

2. **dataflow/vfx-parameter-ui-to-shader.md** — Complete parameter data flow
   - Four-stage pipeline: UI → CPU → GPU CB → Shader
   - Detailed mapping of ImGui slider to GPU constant buffer
   - Restart vs live update decision logic
   - Constant buffer struct layout with offsets
   - Shader integration (compute + pixel shader)
   - Complete worked example: spawn rate change walkthrough
   - Latency analysis and synchronization points

### Quick Start Guide

3. **../guides/vfx-parameter-ui-quickstart.md** — Implementation reference
   - Parameter organization checklist
   - ImGui widget patterns for each parameter type
   - Min/max constraints for each parameter
   - Real-time update integration code
   - Parameter restart vs live update categorization
   - Preset system skeleton
   - Troubleshooting checklist

## How to Use These Documents

**For Architecture Understanding:**
- Read `niagara-editor-ui-patterns.md` Part 1-7 for complete UI architecture
- Understand the three-layer structure (System → Emitter → Module)
- Learn the property metadata system used by Niagara

**For Data Flow Understanding:**
- Read `vfx-parameter-ui-to-shader.md` for complete parameter pipeline
- Understand where validation happens (Stage 2: CPU)
- Understand where GPU upload happens (Stage 3: Render Graph)
- Verify shader integration (Stage 4: GPU)

**For Implementation:**
- Start with `vfx-parameter-ui-quickstart.md`
- Define your parameter categories
- Implement ImGui sliders using the widget patterns
- Create the constant buffer struct
- Integrate with render graph upload

## Key Patterns Extracted

### Property Organization (Metadata-Driven)
```cpp
UPROPERTY(EditAnywhere, Category = "Spawn", meta = (
    ClampMin = 0,
    UIMin = 0,
    UIMax = 1000,
    DisplayName = "Particles Per Second"
))
float SpawnRate = 10.0f;
```

### Real-Time Update Flow
```
ImGui change → Mark dirty → Next frame:
  ├─ CPU: Decide restart vs live update
  ├─ Render Graph: Build CB from emitter params
  ├─ RHI: Upload CB to GPU
  └─ Shader: Read CB, apply to particles
```

### Restart vs Live Update
- **Restart** (spawn rate, lifetime, size, blend) — Kill particles, respawn
- **Live update** (gravity, drag, wind) — Update CB, particles continue with new forces

## File Locations

```
docs/analysis/
├── niagara-editor-ui-patterns.md           (2500 lines, complete reference)
├── dataflow/
│   └── vfx-parameter-ui-to-shader.md       (600 lines, data flow trace)
└── README.md                                (this file)

docs/guides/
└── vfx-parameter-ui-quickstart.md          (600 lines, implementation checklist)
```

## References

Source files analyzed:
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/NiagaraEmitterDetailsCustomization.cpp`
  - Property categorization, edit conditions, callback system
- `Engine/Plugins/FX/Niagara/Source/Niagara/Classes/NiagaraEmitter.h` (600+ lines)
  - FVersionedNiagaraEmitterData with all emitter properties
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Private/Widgets/SNiagaraSystemUserParameters.cpp`
  - User parameter UI pattern
- `Engine/Plugins/FX/Niagara/Source/NiagaraEditor/Public/ViewModels/NiagaraParameterPanelViewModel.h`
  - Parameter management architecture

---

**Status:** Complete. All three documents are production-ready and suitable for:
- Architecture design reviews
- Implementation reference during coding
- Training/onboarding documentation
- API design validation

See the individual documents for detailed analysis.
