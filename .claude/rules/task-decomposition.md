# Task Decomposition — One Subsystem Per Sub-Task

## Rule

When a task touches multiple subsystems, ALWAYS decompose it into sub-tasks where each sub-task modifies **at most 1-2 files in the same subsystem**.

## Why

Claude loses context when juggling too many files simultaneously. A task like "implement sky→fog injection" that touches 6 files across 3 subsystems (ECS, renderer, shader) will result in errors because by the time the shader is being written, the details of the CB layout are already compressed out of context.

## How to Decompose

### Bad: One big task
```
"Implement sky→fog color injection"
→ Touch: component.h, fog_renderer.cpp, constant_buffers.h,
  height_fog_ps.hlsl, frame_setup.cpp, frame_lighting.cpp
→ Result: context overload, mistakes in CB offsets, missing fields
```

### Good: Sub-tasks by subsystem
```
Task 1: "Add skyAtmContribColorScale to HeightFogComponent"
→ Touch: height_fog_component.h (1 file)
→ Done when: field exists with UE5 default, @source citation

Task 2: "Add skyFogColor fields to FogCB"
→ Touch: constant_buffers.h (1 file)
→ Done when: CB has new fields at correct offsets

Task 3: "Pass sky color to fog CB in fog_renderer.cpp"
→ Touch: fog_renderer.cpp (1 file)
→ Done when: reads component, writes to CB

Task 4: "Read sky color in fog shader"
→ Touch: height_fog_ps.hlsl (1 file)
→ Done when: shader reads new CB fields, applies formula

Task 5: "Wire SkyLight color into fog renderer call"
→ Touch: frame_lighting.cpp (1 file)
→ Done when: extracts SkyLight color, passes to fog renderer
```

## Rules

1. **Each sub-task modifies 1-2 files** — never more
2. **Each sub-task is independently verifiable** — it compiles, it doesn't break existing behavior
3. **Sub-tasks follow data flow order** — component → CB → renderer → shader (upstream first)
4. **Each sub-task has a clear "done when"** — not "implement X", but "field Y exists with default Z"
5. **Build after each sub-task** — verify compilation before proceeding

## When an Agent Receives a Multi-File Task

If you are an agent and receive a prompt that requires changing 3+ files across subsystems:

1. **STOP** — do not start implementing
2. **Create a data flow document** (see `.claude/rules/data-flow-first.md`)
3. **Decompose into sub-tasks** following this rule
4. **Report back** with the decomposition and data flow doc
5. **Wait for approval** before implementing

The team lead or main conversation will then dispatch each sub-task individually.
