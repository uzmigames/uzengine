# Data Flow Document BEFORE Multi-Subsystem Implementation

## Rule (applies to ALL agents — NO EXCEPTIONS)

When a task involves **3 or more files across different subsystems** (e.g., C++ component → constant buffer → shader), you MUST create a data flow document BEFORE writing any code.

## When This Rule Applies

- Adding a new field that flows from ECS component → renderer → CB → shader
- Implementing a rendering system that spans CPU setup + GPU shader (e.g., sky→fog injection)
- Fixing a bug that involves data flowing through multiple pipeline stages
- Any change where you need to modify both C++ and HLSL files

## When This Rule Does NOT Apply

- Single-file changes (editing one shader, one component, one renderer)
- Two-file changes where the relationship is obvious (e.g., .h + .cpp pair)
- Pure refactoring within one subsystem
- Documentation-only changes

## The Data Flow Document

Create a markdown file in `docs/analysis/dataflow/` named `<feature>.md` with this structure:

```markdown
# Data Flow: <Feature Name>

## Reference
| Stage | Reference File | Function/Line | What It Does |
|-------|----------|---------------|--------------|
| 1. Component | ExponentialHeightFogComponent.h:42 | FogDensity UPROPERTY | Stores fog density param |
| 2. Gathering | FogRendering.cpp:282 | InitFogConstants() | Reads component, fills CB |
| 3. CB Layout | SceneView.h:1204 | FogStruct | CB field layout |
| 4. Shader | HeightFogCommon.ush:319 | GetExponentialFogColor() | Reads CB, computes fog |

## UzEngine Mapping
| Stage | UzEngine File | Function | Status |
|-------|---------------|----------|--------|
| 1. Component | height_fog_component.h | HeightFogComponent | EXISTS / NEEDS FIELD X |
| 2. Gathering | fog_renderer.cpp | RenderFog() | EXISTS / NEEDS CHANGE |
| 3. CB Layout | constant_buffers.h | FogCB | EXISTS / NEEDS FIELD X |
| 4. Shader | height_fog_ps.hlsl | main() | EXISTS / NEEDS CHANGE |

## Data Flow Diagram
Component.field → fog_renderer.cpp reads it → writes to FogCB.field → shader reads FogCB.field

## Changes Required (one per file, in order)
1. `height_fog_component.h` — add `float newField = 0.02f;` (reference default)
2. `constant_buffers.h` — add `float newField;` to FogCB at offset X
3. `fog_renderer.cpp` — read component.newField, write to fogCB.newField
4. `height_fog_ps.hlsl` — read cb.newField, use in formula at line Y
```

## Why This Works

1. **Prevents context loss** — the document persists across context window compressions
2. **Forces research upfront** — you can't fill the table without reading reference engine source
3. **Makes the implementation mechanical** — each row = one edit, one file
4. **Catches missing pieces** — if the reference has 6 stages and we only mapped 4, we see the gap
5. **Reviewable by user** — user can approve the plan before any code is written

## Workflow

1. **Research**: Read all relevant reference files, fill the Reference table
2. **Map**: Fill the UzEngine Mapping table (what exists, what's missing)
3. **Plan**: List changes required, one per file, in dependency order
4. **Present**: Show the document to the user (or team lead) for approval
5. **Implement**: Execute changes one file at a time, in the listed order
6. **Verify**: Check each change compiles before moving to the next file

## FORBIDDEN

- Starting implementation before the data flow document is written
- Writing "I'll figure out the CB layout as I go" — NO, map it first
- Skipping the Reference table — if you can't fill it, launch architecture analysis agent first
- Modifying files in random order — follow the dependency order in the document