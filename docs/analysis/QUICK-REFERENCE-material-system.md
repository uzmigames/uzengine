<!-- doc-meta
title: UE5 Material System — Quick Reference
description: One-page summary of key findings
date: 2026-03-18
status: published
-->

# UE5 5.6 Material System — Quick Reference (One Page)

## Exact Paths (All Verified)

| Component | Path | File/Line | Module |
|-----------|------|-----------|--------|
| **UMaterial class** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/Material.h` | Line 25 | Runtime |
| **UMaterialExpression base** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression.h` | Line 146 | Runtime |
| **Expression node types** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialExpression*.h` | 274 files | Runtime |
| **Material compiler interface** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/MaterialCompiler.h` | Line 119 | Runtime |
| **HLSL translator impl** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.h` | 75,965 bytes | Runtime/Private |
| **Translator implementation** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/HLSLMaterialTranslator.cpp` | 678,931 bytes | Runtime/Private |
| **Expression compile logic** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Private/Materials/MaterialExpressions.cpp` | 773,361 bytes | Runtime/Private |
| **Material graph (editor)** | `/w/UE_5.6_Source/Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h` | Line 8 | Editor-only |
| **Material instance** | `/w/UE_5.6_Source/Engine/Source/Runtime/Engine/Public/Materials/MaterialInstance.h` | Runtime | |

---

## Architecture in 30 Seconds

```
Material = Array of Expressions + Root Nodes (one per output)

Expression = Data Container:
  - Inputs: array of connections (pointer to other expression + output index)
  - Outputs: array of output definitions (name, mask)
  - Parameters: texture ID, scalar value, etc.
  - NO compilation logic

Compiler = Recursive Visitor:
  - Interface: FMaterialCompiler (abstract)
  - Implementation: FHLSLMaterialTranslator (HLSL code gen)
  - Memoization: Cache compiled expressions to handle DAGs
  - Per-property: Compile Albedo, Normal, Roughness separately

Graph Storage = Serialized Array:
  - Linear array of expressions
  - Connections via pointers
  - No tree structure — it's a DAG

Module Split:
  - Runtime: Material.h + MaterialExpression.h + 274 node types
  - Runtime/Private: HLSLMaterialTranslator.cpp (compiler impl)
  - Editor: MaterialGraph.h + UI (optional, can be stripped)
```

---

## Key Design Patterns

| Pattern | File | Purpose |
|---------|------|---------|
| **Expression = Data** | MaterialExpression.h:146 | Nodes hold only connections & parameters |
| **Visitor = Compiler** | HLSLMaterialTranslator.cpp | Compilation logic in translator, not nodes |
| **Memoization** | HLSLMaterialTranslator.h | Cache expressions to handle DAGs efficiently |
| **Per-Property** | Material.h | Albedo/Normal/etc. compiled as separate passes |
| **Flat Array** | Material.h:ExpressionCollection | Linear array, connections via pointers |
| **IR Layer** | MaterialIREmitter.cpp | (5.6+) Intermediate representation for optimization |
| **Instances** | MaterialInstance.h | Parameter overrides without graph duplication |
| **Abstract Interface** | MaterialCompiler.h:119 | Multiple compilers (HLSL, Lightmass, Proxy) |

---

## File Size Breakdown

| File/Directory | Size | Purpose |
|---|---|---|
| Material.cpp | 303 KB | Material asset implementation |
| MaterialExpressions.cpp | **773 KB** | All 274 node type compilations |
| HLSLMaterialTranslator.cpp | **679 KB** | Main compiler implementation |
| MaterialIREmitter.cpp | 90 KB | IR generation (new in 5.6) |
| MaterialInstance.cpp | 200 KB | Parameter override system |
| **Total Runtime/Private** | **~3.6 MB** | Core compilation system |

---

## Module Boundaries (Ship vs Strip)

### Ships with Game
- `/Public/Materials/Material.h` — material assets
- `/Public/Materials/MaterialExpression.h` — expression data model
- `/Public/Materials/MaterialExpression*.h` — 274 node types
- `/Public/MaterialCompiler.h` — compiler interface
- Pre-compiled shader cache — cached HLSL bytecode

### Editor Only (Can Be Stripped)
- `/Private/Materials/HLSLMaterialTranslator.cpp` — shader compilation
- `/Editor/UnrealEd/Classes/MaterialGraph/` — visual graph
- `/Editor/MaterialEditor/` — editor UI panels
- Shader source files (`.ush`)

**Why?** Games only need pre-compiled shaders. Shader compilation/editing is editor-only.

---

## Expression Connection Model

```cpp
struct FExpressionInput {
    UMaterialExpression* Expression;  // Pointer to upstream node
    int32 OutputIndex;                 // Which output (0-N)
};

struct FExpressionOutput {
    FName OutputName;      // "RGB", "R", "A", etc.
    int32 Mask;            // Component mask
};

// Example: TextureSample has 5 outputs
TextureSample
├─ 0: RGB    ← Connect here for full color
├─ 1: R      ← Red channel
├─ 2: G      ← Green channel
├─ 3: B      ← Blue channel
└─ 4: A      ← Alpha channel
```

---

## Compilation Sequence

```
1. User edits material → Visual graph updated
2. Save material → Serialize expression array
3. Compile triggered
4. For each output (Albedo, Normal, Roughness, etc.):
   a. Set current property
   b. CallExpression(RootExpression)
   c. Recursively compile upstream expressions
   d. Generate HLSL code
   e. Cache result
5. Link shader → GPU bytecode
6. Save to DDC (Derived Data Cache)
```

---

## For UzEngine Implementation

### DO This (From UE5)
1. **Expressions = data containers only** — no Compile() method
2. **Memoized recursive compiler** — cache results to handle DAGs
3. **Abstract compiler interface** — allow multiple code generators
4. **Flat array storage** — serialize as TArray + connections via indices
5. **Per-property compilation** — separate passes for each output
6. **Material instances** — parameter overrides without duplication
7. **Clear editor/runtime boundary** — compiler is editor-only infrastructure

### DON'T Do (Anti-patterns)
1. **Include compilation logic in expression classes** — breaks separation of concerns
2. **Implement custom logic per expression type** — creates 274 different classes
3. **Use tree data structure for graph** — harder to serialize, harder to optimize
4. **Compile everything into one monolithic shader** — can't partially recompile
5. **Ship shader compiler with game** — bloats binary, unnecessary at runtime

---

## Quick Facts

- **274 expression node types** — all stored as separate `.h` files
- **3.6 MB runtime code** — just for compilation (editor-only infrastructure)
- **679 KB compiler** — HLSLMaterialTranslator.cpp alone
- **No switch-on-type** — uses virtual dispatch + visitor pattern
- **Memoization critical** — single expression can be used by multiple outputs
- **New IR layer** — UE5 5.6 adds intermediate representation for optimization
- **Module split clean** — runtime data is minimal, compilation is all private

---

## References & Further Reading

1. **Main Analysis:** `ue5-material-system-directory-structure.md` — full details
2. **Directory Tree:** `ue5-material-system-tree.md` — visual layout
3. **Patterns Guide:** `ue5-material-system-patterns.md` — architectural lessons
4. **Source Location:** `W:\UE_5.6_Source\Engine\Source\Runtime\Engine\`

