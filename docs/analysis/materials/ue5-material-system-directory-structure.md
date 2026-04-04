<!-- doc-meta
title: UE5 Material System Directory Structure
description: Exact paths, module boundaries, and architectural layering of UE5's material system
date: 2026-03-18
status: published
-->

# UE5 5.6 Material System — Directory Structure & Architecture

## Overview

UE5's material system spans **three major domains**: Runtime (game code), Editor (designer tools), and Internal (utilities). This document maps the exact directory structure and explains the module boundaries that define what code runs on game platforms vs editor-only.

---

## 1. Runtime Material System (`Engine/`)

### 1.1 Core Material Class

**File:** `/w/UE_5.6_Source/Engine/Public/Materials/Material.h`
- **Line 25:** `#include "Material.generated.h"`
- **UClass:** `UCLASS(BlueprintType)` material asset
- **Size:** 99,195 bytes (June 3, 2025)
- **Purpose:** Root asset class that holds the material expression graph and shader compilation state
- **Module:** `Engine` (Runtime)
- **Visibility:** Public — material assets are loaded and used at runtime

### 1.2 Material Expression Base Class

**File:** `/w/UE_5.6_Source/Engine/Public/Materials/MaterialExpression.h`
- **Line 146:** `UCLASS(abstract, Optional, BlueprintType, Config=MaterialExpressions, hidecategories=Object, MinimalAPI)`
- **Base Class:** `UObject`
- **Size:** 25,963 bytes (June 3, 2025)
- **Purpose:** Abstract base class for all node types in the material graph
- **Key Structures:**
  - `FExpressionInput` — connection info (line 42)
  - `FExpressionOutput` — output definition (line 88)
  - `FMaterialExpressionCollection` — array of expressions (line 120)
- **Visibility:** Public — expressions are runtime objects
- **Module:** `Engine` (Runtime)

### 1.3 Material Expression Node Types (274 header files)

**Directory:** `/w/UE_5.6_Source/Engine/Public/Materials/`

**Sample Expression Nodes:**
```
MaterialExpressionAdd.h                    (1,419 bytes)
MaterialExpressionAbs.h                    (889 bytes)
MaterialExpressionAbsorptionMediumMaterialOutput.h
MaterialExpressionActorPositionWS.h
MaterialExpressionAppendVector.h           (809 bytes)
MaterialExpressionArccosine.h              (926 bytes)
MaterialExpressionAntialiasedTextureMask.h (1,368 bytes)
MaterialExpressionBlendMaterialAttributes.h (3,366 bytes)
... (268 more node types)
```

**Key Observation:** Each expression is a separate `.h` file with minimal implementation — node types are **data-heavy, logic-light**. The actual compilation work happens in the translator, not in expression nodes themselves.

**File Count:** 274 expression-related headers in `/Public/Materials/`

### 1.4 Material Instances (Runtime)

**Files:**
- `/w/UE_5.6_Source/Engine/Public/Materials/MaterialInstance.h`
- `/w/UE_5.6_Source/Engine/Public/Materials/MaterialInstanceConstant.h`
- `/w/UE_5.6_Source/Engine/Public/Materials/MaterialInstanceDynamic.h`

**Purpose:**
- `MaterialInstance` — base for runtime parameter overrides
- `MaterialInstanceConstant` — immutable instance (compile-time parameters only)
- `MaterialInstanceDynamic` — mutable instance (runtime scalar/vector/texture changes)

**Module:** Runtime — instances are used during gameplay

---

## 2. Material Compilation (Runtime Private)

### 2.1 Material Compiler Interface

**File:** `/w/UE_5.6_Source/Engine/Public/MaterialCompiler.h`
- **Line 119:** `class FMaterialCompiler { ... }`
- **Purpose:** Abstract interface that all material translators must implement
- **Key Methods:**
  - `SetMaterialProperty()` — set which shader output to compile (line 129)
  - `CallExpression()` — recursively compile an expression (line 154)
  - `GetType()` — query the value type of a code index (line 178)
- **Visibility:** Public — exposed so editor can instantiate compilers
- **Module:** Runtime

### 2.2 HLSL Material Translator

**File:** `/w/UE_5.6_Source/Engine/Private/Materials/HLSLMaterialTranslator.h`
- **Line 1:** `// Translates material expressions into HLSL code.`
- **Size:** 75,965 bytes (June 3, 2025)
- **Implementation File:** `HLSLMaterialTranslator.cpp` (678,931 bytes)
- **Purpose:** Concrete implementation of `FMaterialCompiler` that outputs HLSL shader code
- **Key Feature:** Recursive expression traversal with inline code generation
- **Module:** Runtime/Private — only linked into the runtime engine
- **Visibility:** Private header — not exposed in public API

**Key Dependencies:**
- `#include "Materials/Material.h"` (line 17)
- `#include "Materials/MaterialExpression*.h"` (dozens of includes)
- `#include "MaterialCompiler.h"` (line 25)
- `#include "ShaderCompilerDefinitions.h"` (line 36)

### 2.3 Subsidiary Compilation Systems (Private)

**Files in `/Private/Materials/`:**
- `MaterialExpressions.cpp` — 773,361 bytes; implements `Compile()` for each node type
- `MaterialIREmitter.cpp` — 89,879 bytes; intermediate representation generation
- `MaterialIRToHLSLTranslator.cpp` — 43,912 bytes; IR→HLSL code generation
- `MaterialIRModuleBuilder.cpp` — 21,633 bytes; module-level code structure
- `MaterialCachedData.cpp` — 44,139 bytes; PSO cache & compiled shader data
- `MaterialInstance.cpp` — 199,595 bytes; parameter override logic

**Pattern:** Material compilation is **not a single monolithic pass**. Instead, it's layered:
1. **Expression Compile** — each node's `Compile()` method generates HLSL fragments
2. **IR Generation** — fragments are collected into an intermediate representation
3. **IR to HLSL** — IR is translated into final shader code
4. **Caching** — compiled shaders are cached with dependencies

---

## 3. Editor-Only Material System (`Engine/Source/Editor/`)

### 3.1 Material Graph (Editor)

**File:** `/w/UE_5.6_Source/Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h`
- **Line 1:** `// Copyright Epic Games, Inc. All Rights Reserved.`
- **Line 8:** `#include "EdGraph/EdGraph.h"`
- **Line 9:** `#include "Materials/Material.h"`
- **Purpose:** Visual graph representation of a material for the editor
- **Key Class:** `UMaterialGraph : public UEdGraph`
- **Module:** `UnrealEd` (Editor-only)
- **Visibility:** Editor-only — this class is wrapped in `#if WITH_EDITOR`

**Relationship to Runtime Material:**
- Converts `UMaterial` expression array → visual `UEdGraph` nodes
- Converts visual graph edits → back to `UMaterial` expression array
- **Bidirectional synchronization** — material and graph must stay in sync

### 3.2 Material Editor UI

**Directory:** `/w/UE_5.6_Source/Engine/Source/Editor/MaterialEditor/`
- **Public Subdirs:**
  - `Public/MaterialEditor/` — editor window classes
  - `Public/MaterialEditor/MaterialNodes/` — visual node widgets
  - `Public/MaterialEditor/MaterialPins/` — connection pin widgets
- **Private Subdirs:**
  - `Private/` — implementation
  - `Private/MaterialNodes/` — node UI implementations
  - `Private/MaterialPins/` — pin UI implementations
  - `Private/Tabs/` — editor panels (Properties, Preview, etc.)
  - `Private/Tests/` — editor unit tests

**Module:** `MaterialEditor` (Editor plugin)

### 3.3 Graph Editor Abstraction

**Directory:** `/w/UE_5.6_Source/Engine/Source/Editor/GraphEditor/`
- **Purpose:** Generic visual graph editing system (used by materials, blueprints, etc.)
- **Public Subdirs:**
  - `Public/MaterialNodes/` — material-specific node widget factories
  - `Public/MaterialPins/` — material-specific pin widget factories
- **Module:** `GraphEditor` (Editor plugin)

---

## 4. Module Organization Summary

### 4.1 Runtime vs Editor Boundaries

| Component | Location | Module | Visibility | Runtime? |
|-----------|----------|--------|------------|----------|
| **UMaterial** | `/Public/Materials/Material.h` | Engine | Public | ✓ Yes |
| **UMaterialExpression** | `/Public/Materials/MaterialExpression.h` | Engine | Public | ✓ Yes |
| **274 Expression Nodes** | `/Public/Materials/MaterialExpression*.h` | Engine | Public | ✓ Yes |
| **MaterialInstance** | `/Public/Materials/MaterialInstance.h` | Engine | Public | ✓ Yes |
| **FMaterialCompiler** | `/Public/MaterialCompiler.h` | Engine | Public | ✓ Yes* |
| **FHLSLMaterialTranslator** | `/Private/Materials/HLSLMaterialTranslator.h` | Engine | Private | ✗ Editor only† |
| **UMaterialGraph** | `/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h` | UnrealEd | Private | ✗ Editor only |
| **Material Editor UI** | `/Editor/MaterialEditor/` | MaterialEditor | Private | ✗ Editor only |

*FMaterialCompiler is public but only instantiated in editor (shader cooking). At runtime, materials use pre-compiled shaders.
†Linked into the editor to support shader recompilation and live preview.

### 4.2 Compilation Workflows

**Editor Workflow (Material Design):**
```
Editor UI (MaterialEditor)
        ↓
UMaterialGraph (visual representation)
        ↓
Sync to UMaterial (runtime data)
        ↓
Compile UMaterial → FHLSLMaterialTranslator
        ↓
Generate HLSL → Cache compiled shaders
        ↓
Load into GPU runtime
```

**Runtime Workflow (Game Execution):**
```
Load UMaterial asset
        ↓
Query cached compiled shader
        ↓
Bind to renderer (no compilation needed)
        ↓
Use in draw calls
```

---

## 5. Key Files and Line Numbers

### 5.1 Material Class Hierarchy

**UMaterial Definition:**
- File: `/w/UE_5.6_Source/Engine/Public/Materials/Material.h`
- Line 25: `#include "Material.generated.h"`
- UClass declaration follows shortly after

**UMaterialExpression Base:**
- File: `/w/UE_5.6_Source/Engine/Public/Materials/MaterialExpression.h`
- Line 146: `UCLASS(abstract, Optional, BlueprintType, ...)`
- Line 147: `class UMaterialExpression : public UObject`

**Material Instance:**
- File: `/w/UE_5.6_Source/Engine/Public/Materials/MaterialInstance.h`
- Runtime-loadable parameter override container

### 5.2 Compilation Pipeline

**Compiler Interface:**
- File: `/w/UE_5.6_Source/Engine/Public/MaterialCompiler.h`
- Line 119: `class FMaterialCompiler { ... }` — abstract interface
- Line 154: `virtual int32 CallExpression(FMaterialExpressionKey ExpressionKey, FMaterialCompiler* InCompiler) = 0;`

**HLSL Translator Implementation:**
- File: `/w/UE_5.6_Source/Engine/Private/Materials/HLSLMaterialTranslator.h`
- Line 75,965 bytes total
- `.cpp` file: 678,931 bytes — contains actual node→HLSL logic

**Expression Implementation:**
- File: `/w/UE_5.6_Source/Engine/Private/Materials/MaterialExpressions.cpp`
- Line 773,361 bytes — individual `Compile()` implementations

### 5.3 Editor-Only Components

**Material Graph:**
- File: `/w/UE_5.6_Source/Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h`
- Wraps `UMaterial` in visual `UEdGraph` representation

**Material Editor:**
- Directory: `/w/UE_5.6_Source/Engine/Source/Editor/MaterialEditor/`
- Module-specific — only loaded when material asset is opened in editor

---

## 6. Architecture Patterns (for UzEngine Adaptation)

### 6.1 Expression Node Design

**Pattern: Data Container + Compiler Visitor**

UE5 expressions are lightweight containers:
```cpp
// MaterialExpressionAdd.h (~30 lines)
UCLASS(...)
class UMaterialExpressionAdd : public UMaterialExpression
{
    UPROPERTY() FExpressionInput A;
    UPROPERTY() FExpressionInput B;

    // Minimal implementation — just data
};
```

Compilation happens via the compiler visiting each node:
```cpp
// HLSLMaterialTranslator.cpp
int32 FHLSLMaterialTranslator::MaterialExpressionAdd(UMaterialExpressionAdd* In)
{
    int32 A = CompileExpression(In->A);
    int32 B = CompileExpression(In->B);
    return Mul(A, B);  // or Add, etc.
}
```

**UzEngine Lesson:** Expression nodes should be **data only**. Compilation logic belongs in the translator, not the node.

### 6.2 Compiler Interface Pattern

**Pattern: Recursive Compilation with Code Index Caching**

The compiler maintains a code index map:
```cpp
int32 FMaterialCompiler::CallExpression(FMaterialExpressionKey Key, ...)
{
    // Look up cached result
    if (CachedExpressions[Key]) return CachedExpressions[Key];

    // Compile expression
    int32 CodeIndex = Expression->Compile(this);

    // Cache for DAG nodes
    CachedExpressions[Key] = CodeIndex;
    return CodeIndex;
}
```

**UzEngine Lesson:** Use memoization to handle DAG graphs efficiently. Compile each unique expression once.

### 6.3 Module Boundary Pattern

**Pattern: Runtime Core + Editor Wrapper**

- **Runtime:** Material assets, expression graph (serialized data)
- **Editor:** Graph UI, compilation infrastructure, preview rendering
- **Boundary:** Editor-only code is `#if WITH_EDITOR` guarded

```cpp
#if WITH_EDITOR
class UMaterialGraph : public UEdGraph { ... };
#endif
```

**UzEngine Lesson:** Keep serialized material data in runtime. Editor code is **optional and can be stripped** for shipping games.

---

## 7. Internal/Hidden Components

### 7.1 Internal Directory

**Location:** `/w/UE_5.6_Source/Engine/Internal/Materials/`

**Files:**
- `MaterialExpressionMaterialSample.h` — internal helper
- `MaterialExpressionUtils.h` — utility functions
- `MaterialExpressionMaterialSample.cpp`
- `MaterialExpressionUtils.cpp`

**Purpose:** Shared internals that expressions use but don't expose publicly.

---

## 8. Serialization & Caching

### 8.1 Material Serialization

**Pattern:** Expressions are **directly serialized** into the `.uasset` file as a `FMaterialExpressionCollection`:

```cpp
// Material.h
UPROPERTY()
FMaterialExpressionCollection ExpressionCollection;  // Serialized array of expressions
```

**Key Point:** The material graph is stored as a linear array of `UMaterialExpression*` pointers. Connections are stored as `FExpressionInput` which holds a pointer to the connected expression + output index.

### 8.2 Compiled Shader Caching

**Directory:** `/Private/Materials/`
- `MaterialCachedData.cpp` — manages compiled shader cache
- Cache is keyed by:
  - Material expression hash
  - Platform (DX11, Vulkan, etc.)
  - Feature level
  - Quality settings

**Pattern:** Compiled shaders are **separate from the material asset**. They're stored in a derived data cache (DDC) and regenerated if the material or engine changes.

---

## 9. Comparison to UzEngine

| Aspect | UE5 | UzEngine (Target) |
|--------|-----|---|
| **Expression Storage** | `/Public/Materials/` | `runtime/engine/renderer/materials/` |
| **Expression Base Class** | `UMaterialExpression` | `MaterialNode` (C struct?) |
| **Compiler** | `FMaterialCompiler` (interface) + `FHLSLMaterialTranslator` (impl) | Similar pattern needed |
| **Module Boundary** | Public (Runtime) vs Private (Editor) | Clear Renderer/Editor split |
| **Serialization** | Built into UObject system | Need custom serialization |
| **Node Types** | 274 expression types | Subset for MVP (Add, Multiply, Sample, etc.) |

---

## 10. References

**Primary Source Files:**
1. `/w/UE_5.6_Source/Engine/Public/Materials/Material.h`
2. `/w/UE_5.6_Source/Engine/Public/Materials/MaterialExpression.h`
3. `/w/UE_5.6_Source/Engine/Public/MaterialCompiler.h`
4. `/w/UE_5.6_Source/Engine/Private/Materials/HLSLMaterialTranslator.h`
5. `/w/UE_5.6_Source/Engine/Source/Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h`

**Expression Node Count:** 274 distinct `.h` files in `/Public/Materials/MaterialExpression*.h`

**Total Code:**
- Runtime material system: ~3.6 MB (`Private/Materials/` directory)
- Expression implementations: 773 KB (MaterialExpressions.cpp alone)
- Compiler: 679 KB (HLSLMaterialTranslator.cpp)

---

## 11. Recommended UzEngine Implementation Pattern

Based on UE5's architecture:

1. **Create `runtime/engine/renderer/materials/` with:**
   - `material.h` — UMaterial equivalent (serialized expression graph)
   - `material_expression.h` — UMaterialExpression base class
   - `material_expression_*.h` — node type definitions (Add, Multiply, TextureSample, etc.)
   - `material_compiler.h` — FMaterialCompiler interface
   - `hlsl_material_translator.cpp` — HLSLMaterialTranslator equivalent

2. **Editor layer in `runtime/editor/panels/materials/`:**
   - Material graph UI (visual editor)
   - Material preview
   - Compilation trigger & error reporting

3. **Keep material instances in runtime:**
   - Load `.uzmat` files and apply parameter overrides
   - Support dynamic parameter changes (similar to MaterialInstanceDynamic)

4. **Module boundaries:**
   - Runtime core (materials + expressions) — ships with game
   - Editor (graph UI + compiler UI) — editor-only, can be stripped

