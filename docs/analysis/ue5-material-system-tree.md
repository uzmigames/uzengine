<!-- doc-meta
title: UE5 Material System Directory Tree
description: Visual directory tree of material system files organized by layer
date: 2026-03-18
status: published
-->

# UE5 5.6 Material System — Directory Tree

## Quick Reference Directory Structure

```
W:\UE_5.6_Source\Engine\Source\
│
├── Runtime\Engine\
│   │
│   ├── Public\Materials\  [RUNTIME LAYER — ships with game]
│   │   ├── Material.h                           [UMaterial class — line 25]
│   │   ├── MaterialInterface.h
│   │   ├── MaterialExpression.h                 [Base class — line 146]
│   │   ├── MaterialExpressionIO.h
│   │   ├── MaterialValueType.h
│   │   ├── MaterialCompiler.h                   [Compiler interface — line 119]
│   │   │
│   │   ├── [274 Expression Node Types]
│   │   ├── MaterialExpressionAdd.h              (1,419 bytes)
│   │   ├── MaterialExpressionAbs.h              (889 bytes)
│   │   ├── MaterialExpressionAbsorptionMediumMaterialOutput.h
│   │   ├── MaterialExpressionActorPositionWS.h
│   │   ├── MaterialExpressionAppendVector.h    (809 bytes)
│   │   ├── MaterialExpressionArccosine.h       (926 bytes)
│   │   ├── MaterialExpressionArctangent.h
│   │   ├── MaterialExpressionAntialiasedTextureMask.h
│   │   ├── MaterialExpressionAtmosphericFogColor.h
│   │   ├── MaterialExpressionAtmosphericLightColor.h
│   │   ├── MaterialExpressionBentNormalCustomOutput.h
│   │   ├── MaterialExpressionBlackBody.h
│   │   ├── MaterialExpressionBlendMaterialAttributes.h
│   │   ├── MaterialExpressionBounds.h
│   │   ├── MaterialExpressionBreakMaterialAttributes.h
│   │   ├── MaterialExpressionBumpOffset.h
│   │   ├── MaterialExpressionCameraPositionWS.h
│   │   ├── ... (254 more node types)
│   │   │
│   │   ├── [Material Instance Types]
│   │   ├── MaterialInstance.h
│   │   ├── MaterialInstanceConstant.h
│   │   ├── MaterialInstanceDynamic.h
│   │   ├── MaterialInstanceActor.h
│   │   ├── MaterialInstanceBasePropertyOverrides.h
│   │   │
│   │   └── [Support Headers]
│   │       ├── MaterialAttributeDefinitionMap.h
│   │       ├── MaterialDependencyWalker.h
│   │       ├── MaterialFunctionInterface.h
│   │       ├── MaterialLayersFunctions.h
│   │       ├── MaterialOverrideNanite.h
│   │       ├── MaterialExpressionSettings.h
│   │       └── MaterialExpressionCollectionTransform.h
│   │
│   ├── Private\Materials\  [RUNTIME IMPLEMENTATION — editor + game]
│   │   ├── HLSLMaterialTranslator.h             (75,965 bytes)
│   │   ├── HLSLMaterialTranslator.cpp           (678,931 bytes)
│   │   ├── HLSLMaterialDerivativeAutogen.h
│   │   ├── HLSLMaterialDerivativeAutogen.cpp
│   │   ├── Material.cpp                         (302,731 bytes)
│   │   ├── MaterialExpressions.cpp              (773,361 bytes) [All node Compile() impl]
│   │   ├── MaterialInstance.cpp                 (199,595 bytes)
│   │   ├── MaterialInstanceConstant.cpp         (8,318 bytes)
│   │   ├── MaterialInstanceDynamic.cpp          (17,362 bytes)
│   │   ├── MaterialInterface.cpp                (73,573 bytes)
│   │   ├── MaterialInstanceActor.cpp
│   │   │
│   │   ├── [Compilation Pipeline]
│   │   ├── MaterialCompiler.cpp
│   │   ├── MaterialCachedData.cpp               (44,139 bytes)
│   │   ├── MaterialExpressionSubstrate.cpp      (172,158 bytes)
│   │   ├── MaterialExpressionNoise.cpp
│   │   │
│   │   ├── [Intermediate Representation — NEW in 5.6]
│   │   ├── MaterialIR.cpp                       (18,569 bytes)
│   │   ├── MaterialIREmitter.cpp                (89,879 bytes)
│   │   ├── MaterialIRToHLSLTranslator.cpp       (43,912 bytes)
│   │   ├── MaterialIRModuleBuilder.cpp          (21,633 bytes)
│   │   ├── MaterialIRValueAnalyzer.cpp          (11,384 bytes)
│   │   ├── MaterialIRDebug.cpp                  (14,551 bytes)
│   │   │
│   │   ├── [Support]
│   │   ├── MaterialAttributeDefinitionMap.cpp
│   │   ├── MaterialDependencyWalker.cpp
│   │   ├── MaterialExternalCodeRegistry.h
│   │   ├── MaterialExternalCodeRegistry.cpp
│   │   ├── MaterialKeyGeneratorContext.cpp
│   │   ├── MaterialOverrideNanite.cpp
│   │   ├── MaterialParameterCollection.cpp
│   │   ├── MaterialRenderProxy.cpp              (36,760 bytes)
│   │   ├── MaterialInstanceSupport.h            (22,479 bytes)
│   │   ├── MaterialExpressionSettings.cpp
│   │   └── MaterialInsights.cpp
│   │
│   └── Internal\Materials\  [PRIVATE HELPERS]
│       ├── MaterialExpressionMaterialSample.h
│       ├── MaterialExpressionMaterialSample.cpp
│       ├── MaterialExpressionUtils.h
│       └── MaterialExpressionUtils.cpp
│
└── Editor\
    │
    ├── UnrealEd\
    │   │
    │   ├── Classes\MaterialGraph\  [EDITOR GRAPH MODEL]
    │   │   ├── MaterialGraph.h                  [Visual graph container — line 8]
    │   │   └── MaterialGraph.generated.h
    │   │
    │   └── Private\MaterialEditor\  [EDITOR IMPLEMENTATION]
    │       └── ... (editor-specific code)
    │
    └── MaterialEditor\  [EDITOR UI PLUGIN]
        │
        ├── Public\
        │   ├── MaterialEditor\
        │   │   ├── MaterialNodes\              [Node widget factories]
        │   │   └── MaterialPins\               [Pin connection widgets]
        │   ├── MaterialNodes\
        │   └── MaterialPins\
        │
        ├── Private\
        │   ├── MaterialNodes\                  [Node UI implementations]
        │   ├── MaterialPins\                   [Pin UI implementations]
        │   ├── Tabs\                           [Editor panels]
        │   └── Tests\                          [Editor unit tests]
        │
        └── Resources\  [Icons, textures, etc.]

    └── GraphEditor\  [GENERIC GRAPH EDITING SYSTEM]
        │
        └── Public\
            ├── MaterialNodes\                  [Material-specific node widgets]
            └── MaterialPins\                   [Material-specific pin widgets]
```

---

## File Count Summary

| Directory | Count | Purpose |
|-----------|-------|---------|
| `/Public/Materials/` | 297 files | Expression types + Material classes |
| `/Private/Materials/` | 40+ files | Compiler implementation |
| `/Internal/Materials/` | 4 files | Private helpers |
| **Total Runtime** | **~340 files** | Core material system |
| `/Editor/MaterialEditor/` | 50+ files | Editor UI (optional) |
| `/Editor/UnrealEd/MaterialGraph/` | 2 files | Graph model (optional) |
| **Total Editor** | **~60 files** | Editor tooling (can be stripped) |

---

## Key File Sizes

| File | Size | Purpose |
|------|------|---------|
| Material.h | 99 KB | Material asset class |
| MaterialExpression.h | 26 KB | Expression base class |
| Material.cpp | 303 KB | Material implementation |
| MaterialExpressions.cpp | **773 KB** | All node type compilations |
| HLSLMaterialTranslator.cpp | **679 KB** | Main compiler implementation |
| HLSLMaterialTranslator.h | 76 KB | Compiler class definition |
| MaterialIREmitter.cpp | 90 KB | IR generation |

**Total Implementation:** ~3.6 MB in `/Private/Materials/` alone

---

## Module Dependencies

```
MaterialExpression.h
    ↓
    includes → MaterialExpressionIO.h
    includes → MaterialValueType.h

Material.h
    ↓
    includes → Materials/MaterialExpression.h
    includes → MaterialInterface.h
    includes → MaterialExpressionIO.h

MaterialCompiler.h
    ↓
    includes → Materials/MaterialExpression*.h (all 274 types)

HLSLMaterialTranslator.h (PRIVATE)
    ↓
    includes → MaterialCompiler.h
    includes → Material.h
    includes → All MaterialExpression*.h headers
    includes → ShaderCompilerDefinitions.h
```

---

## Runtime vs Editor: Module Boundaries

```
RUNTIME (ships with game):
├── Public/Materials/
│   ├── Material.h
│   ├── MaterialExpression.h
│   ├── MaterialExpression*.h (274 nodes)
│   ├── MaterialInstance.h
│   └── MaterialCompiler.h (interface only)
│
└── Private/Materials/
    ├── HLSLMaterialTranslator.cpp (linked into editor/cooker, NOT into game)
    ├── MaterialExpressions.cpp
    ├── MaterialInstance.cpp
    ├── Material.cpp
    └── ... (compilation infrastructure)

EDITOR ONLY (NOT shipped):
├── Editor/UnrealEd/Classes/MaterialGraph/MaterialGraph.h
├── Editor/MaterialEditor/ (UI plugin)
└── Editor/GraphEditor/ (generic graph system)
```

---

## Compilation Flow (File Sequence)

```
1. User edits Material in Editor
        ↓ (MaterialGraph UI updates in-memory graph)
2. MaterialGraph→Material sync
        ↓ (expression array updated from visual nodes)
3. Material needs recompile
        ↓
4. FHLSLMaterialTranslator instantiated (Private\Materials\HLSLMaterialTranslator.h)
        ↓
5. Compile(UMaterial*) called
        ↓
6. For each property (Albedo, Normal, etc.):
    - CallExpression(RootExpression, &Translator)
        ↓
7. Expression→Compile(FMaterialCompiler*)  [MaterialExpressions.cpp]
        ↓
8. FHLSLMaterialTranslator generates HLSL
        ↓
9. Shader code cached via MaterialCachedData.cpp
        ↓
10. HLSL compiled to GPU bytecode
        ↓
11. Game loads .uasset, uses cached compiled shader
```

---

## Expression Node Organization (274 Types)

**Grouped by Function:**

1. **Basic Math** (20+)
   - Add, Subtract, Multiply, Divide
   - Abs, Ceiling, Floor, Sign
   - Arcsine, Arccos, Arctangent
   - etc.

2. **Texture Sampling** (10+)
   - TextureSample (most common)
   - AntialiasedTextureMask
   - TextureProperty
   - etc.

3. **Vector Operations** (15+)
   - AppendVector, BreakOutFloat*
   - ComponentMask
   - Normalize, Dot, Cross
   - etc.

4. **Coordinate Systems** (10+)
   - WorldPosition
   - ObjectPosition
   - CameraPosition
   - TexCoord (up to 8 channels)
   - etc.

5. **Lighting & Shading** (20+)
   - CustomOutput (base for sub-surface, bent normal, etc.)
   - MaterialAttributes (blend properties)
   - BentNormalCustomOutput
   - AbsorptionMediumMaterialOutput
   - etc.

6. **Advanced** (100+)
   - MaterialFunctionCall (graph composition)
   - If, Select (conditionals)
   - RerouteExpression (routing/organization)
   - Noise, Noise2D (procedural)
   - SpeedTree (vegetation-specific)
   - etc.

**Stored as:** 274 separate `.h` files in `/Public/Materials/MaterialExpression*.h`

---

## Key Takeaway

**UE5 Material Architecture = Data + Visitor Pattern**

1. **Expressions** (`MaterialExpression.h`) are **lightweight data containers** holding connections only
2. **Graph** (`Material.h`) is a **serialized array** of expression pointers + connections
3. **Compiler** (`MaterialCompiler.h`) is an **abstract interface** for code generation
4. **Translator** (`HLSLMaterialTranslator.cpp`) **visits each expression** recursively, generating HLSL
5. **Editor** (`MaterialGraph.h`) wraps runtime data in a visual representation
6. **Module split:** Runtime (data) + Optional Editor (UI)

This design separates **what** (data model) from **how** (compilation), making both easily extensible.

