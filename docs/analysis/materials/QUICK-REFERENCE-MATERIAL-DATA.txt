================================================================================
UE5 MATERIAL DATA MODEL — QUICK REFERENCE CARD
================================================================================

SCALAR PARAMETERS (Material Author Defined)
─────────────────────────────────────────────────────────────────────────────
NAME              │ RANGE  │ TYPICAL │ PURPOSE
─────────────────────────────────────────────────────────────────────────────
Roughness         │ [0, 1] │  0.5    │ Perceptual roughness (remapped to α²)
Metallic          │ [0, 1] │  0.0    │ Metallic blend (0=dielectric, 1=metal)
Specular          │ [0, 1] │  0.5    │ Non-metallic specular strength
Opacity           │ [0, 1] │  1.0    │ Transparency (forward only)
Normal Strength   │ [0, 2] │  1.0    │ Normal map intensity multiplier
AO Strength       │ [0, 1] │  1.0    │ Ambient occlusion strength
Emissive Int.     │ [0,∞]  │  0.0    │ Self-illumination HDR scale
Displacement      │ [0, 1] │  0.0    │ Parallax occlusion height scale

VECTOR PARAMETERS (Material Author Defined)
─────────────────────────────────────────────────────────────────────────────
NAME              │ COMPONENTS │ RANGE    │ TYPICAL          │ PURPOSE
─────────────────────────────────────────────────────────────────────────────
Base Color        │ RGB        │ [0, 1]   │ (1, 1, 1)        │ Albedo tint
Emissive Color    │ RGBA       │ [0, ∞]   │ (0, 0, 0, 0)     │ Glow + intensity
Subsurface Color  │ RGB        │ [0, 1]   │ (1, 0.5, 0.5)    │ SSS transmission
Tint Color        │ RGBA       │ [0, 1]   │ (1, 1, 1, 1)     │ Generic overlay

TEXTURE PARAMETERS (Material Author Defined)
─────────────────────────────────────────────────────────────────────────────
SLOT              │ FORMAT        │ DEFAULT                │ PURPOSE
─────────────────────────────────────────────────────────────────────────────
Base Color        │ SRGB RGB8/BC1 │ White (1, 1, 1, 1)     │ Albedo map
Normal            │ Linear RGB8   │ Flat (0.5, 0.5, 1.0)   │ Surface detail
Roughness         │ Linear R8/BC4 │ 0.5 gray               │ Per-texel roughness
Metallic          │ Linear R8/BC4 │ 0.0 black              │ Per-texel metallic
Ambient Occlusion │ Linear R8/BC4 │ 1.0 white              │ Occlusion darken
Emissive          │ HDR RGBA16F   │ (0, 0, 0, 0) black     │ Self-illumination
Displacement      │ Linear R8/BC4 │ 0.5 gray               │ Height offset

UV TRANSFORMS
─────────────────────────────────────────────────────────────────────────────
Scope:       Per-texture-sampler in material graph (NOT global)
Fields:      ScaleX, ScaleY, OffsetX, OffsetY
Example:     Normal map @ 1× tiling, BaseColor @ 2× tiling (independent)

GPU CONSTANT BUFFER LAYOUT (Typical PBR)
─────────────────────────────────────────────────────────────────────────────
OFFSET │ SIZE │ FIELD
─────────────────────────────────────────────────────────────────────────────
[0:15]   │ 16   │ float4 BaseColor         (RGB albedo + A opacity)
[16:19]  │ 4    │ float Metallic           (0=dielectric, 1=metal)
[20:23]  │ 4    │ float Roughness          (0=mirror, 1=diffuse)
[24:27]  │ 4    │ float Specular           (non-metal specular)
[28:31]  │ 4    │ float _Pad1              (16-byte alignment)
[32:47]  │ 16   │ float4 EmissiveColor     (RGB color + A intensity)
[48:51]  │ 4    │ float NormalStrength     (normal map intensity)
[52:55]  │ 4    │ float AOStrength         (AO multiplier)
[56:59]  │ 4    │ float DisplacementScale  (POM height)
[60:63]  │ 4    │ float _Pad2              (alignment)
[64:79]  │ 16   │ float4 UVScale           (TileX, TileY, OffsetX, OffsetY)
[80:95]  │ 16   │ [padding for GPU alignment]

TOTAL: 96 bytes (aligned to 16-byte GPU boundary)

MATERIAL INSTANCE INHERITANCE CHAIN
─────────────────────────────────────────────────────────────────────────────
Max Depth: 5 levels
Example:   BasePBRMaterial (UMaterial)
           └── WoodMaterial (UMaterialInstance → parent)
               └── OakWood (UMaterialInstance → parent: WoodMaterial)
                   └── WetOakWood (UMaterialInstance → parent: OakWood)

Parameter Lookup:
  if (WetOakWood.scalarOverrides.contains("Roughness"))
    return WetOakWood.scalarOverrides["Roughness"];
  else if (OakWood.parent.getScalar("Roughness", &value))
    return value;
  else if (WoodMaterial.parent.getScalar("Roughness", &value))
    return value;
  else if (BasePBRMaterial.getScalar("Roughness", &value))
    return value;  // Base shader default (0.5)

SPARSE STORAGE EXAMPLE
─────────────────────────────────────────────────────────────────────────────
BasePBRMaterial:
  m_baseColor = (1, 1, 1, 1)
  m_metallic = 0.0
  m_roughness = 0.5
  m_ao = 1.0
  m_emissive = (0, 0, 0, 0)

WoodMaterial (parent: BasePBR):
  STORED:
    scalarOverrides["Roughness"] = 0.6
    vectorOverrides["BaseColor"] = (0.8, 0.6, 0.3, 1.0)
  INHERITED:
    metallic, ao, emissive (from parent)

OakWood (parent: Wood):
  STORED:
    vectorOverrides["BaseColor"] = (0.9, 0.5, 0.2, 1.0)  (darker)
  INHERITED:
    roughness=0.6 (from WoodMaterial), metallic, ao, emissive

Memory per instance: ~344 bytes if 3 scalar + 2 vector overrides
                     ~100 bytes if only 1 vector override + rest inherited

THREAD-SAFE RENDER PROXY PATTERN
─────────────────────────────────────────────────────────────────────────────
GAME THREAD:
  1. Call Material->GetRenderProxy()
  2. Proxy captures ALL parameters THIS FRAME
  3. Proxy is immutable after creation
  4. Pass proxy to render command

RENDER THREAD:
  1. Receive immutable proxy
  2. Read parameters from proxy (no virtual calls, direct map lookup)
  3. Cannot modify proxy (no race condition)
  4. Use to fill GPU constant buffer
  5. Proxy destroyed at end of frame

NEXT FRAME: New proxy captured, old proxy destroyed.

KEY DIFFERENCES: UE5 vs UzEngine (Current)
─────────────────────────────────────────────────────────────────────────────
ASPECT                 │ UE5 (Flexible)   │ UzEngine (Fixed)
─────────────────────────────────────────────────────────────────────────────
Base Class             │ Virtual interface│ Single struct
Parameter Count        │ Unlimited        │ Fixed 6 fields
Parameter Storage      │ Graph-defined    │ Hardcoded layout
Instance Overrides     │ Sparse maps      │ None (full copy)
Inheritance Levels     │ Up to 5          │ N/A
UV Transforms          │ Per-sampler      │ Per-material only
Thread Safety          │ Proxy snapshot   │ Implicit (risky)
Extensibility          │ ★★★★★           │ ★☆☆☆☆

CRITICAL ARCHITECTURAL GAPS IN UZENGINE
─────────────────────────────────────────────────────────────────────────────
❌ No polymorphism → Can't add cloth/hair/skin without binary break
❌ Fixed 48 bytes  → Can't extend with new parameters
❌ No instances    → Can't override individual parameters from parent
❌ No inheritance  → Material duplication, no variants
❌ No sparse stor. → 1000 instances = wasteful memory copies
❌ No proxy snap.  → Potential race condition (game thread modifies)

UZENGINE IMPLEMENTATION PHASES
─────────────────────────────────────────────────────────────────────────────
PHASE 1 (1 week):   Virtual IMaterialInterface + MaterialRenderProxy
                    ✓ Extensible for future shading models
                    ✓ No breaking changes

PHASE 2 (1 week):   Sparse overrides + inheritance
                    ✓ Material variants (wood → oak → wet oak)
                    ✓ Memory efficiency

PHASE 3 (3-5 days): Render-time proxy snapshot
                    ✓ Thread-safe parameter access
                    ✓ Deterministic per-frame values

PHASE 4 (future):   New shading models (cloth, hair, skin)
                    ✓ Subclass IMaterialInterface
                    ✓ Zero breaking changes

RECOMMENDED READING ORDER
─────────────────────────────────────────────────────────────────────────────
1. This file (5 min)     ← You are here
2. ANALYSIS-SUMMARY.txt (15 min) → Overview
3. ue5-material-parameter-system-extraction.md (20 min) → Architecture
4. ue5-material-struct-layouts-concrete.md (30 min) → Data structures
5. ue5-material-patterns-uzengine-adaptation.md (40 min) → Implementation

TOTAL TIME: ~2 hours to understand complete material system

================================================================================
