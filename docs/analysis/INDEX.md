# Analysis Index — Engine Architecture & Rendering References

**Last Updated**: 2026-04-04  
**Documents**: 4 major analysis suites

---

## Quick Navigation

### I Want To...

**Understand what CVars exist**  
→ Start with ue5-rendering-cvars-reference.md

**Implement CVars in UzEngine**  
→ Follow uzengine-cvar-implementation-guide.md (7 steps)

**Know what NOT to do**  
→ Read cvar-architecture-antipatterns.md (10 anti-patterns)

**Get started in 5 minutes**  
→ Read README.md

---

## Document Suites

### Suite A: UE5 Rendering CVars (1,750 lines, 67KB)

#### 1. ue5-rendering-cvars-reference.md (608 lines)

**Complete reference of all rendering CVars from UE5 5.6**

Organized by 20 categories:
- Tone Mapping (2 CVars)
- Bloom (4 CVars)
- GTAO (8 CVars)
- Shadow Mapping (50+ CVars)
- TAA/TSR (10 CVars)
- Sky Atmosphere (30+ CVars)
- Volumetric Clouds (20+ CVars)
- Volumetric Fog (7 CVars)
- And 12 more categories...

Each CVar includes: name, type, default, range, description

#### 2. uzengine-cvar-implementation-guide.md (482 lines)

**7-step process to implement CVars**

1. Define CVar in C++
2. Wire into constant buffer struct
3. Populate CB from CVars (read every frame)
4. Wire CB into shader
5. Validation & clamping (C++ side)
6. Console command registration
7. Runtime testing

Includes complete code examples for each step.

#### 3. cvar-architecture-antipatterns.md (577 lines)

**10 anti-patterns to avoid**

1. Hardcoded values in shaders
2. Compilation-time conditionals
3. Initialization-time only parameters
4. Inconsistent C++/shader defaults
5. Skipping CB updates
6. Missing validation & clamping
7. No documentation
8. Cascading dependencies without checks
9. Forgetting frame boundary reads
10. CVar scope confusion

Includes WRONG/CORRECT code examples for each.

#### 4. README.md (84 lines)

**Quick-start overview and navigation**

---

### Suite B: Godot 4 Architecture Analysis (1,200+ lines, 42KB)

#### 5. godot4-normal-smoothing-analysis.md (550+ lines)

**Deep analysis of Godot 4's mesh normal smoothing and tangent generation**

Topics:
- Smoothing group mechanism (uint32 per vertex)
- Normal averaging algorithm (unweighted face normal sums)
- Tangent generation via mikktspace library
- Normal encoding (uncompressed Vector3, optional octahedral compression)
- Import pipeline (FBX vs glTF handling)
- Comparison with UE5 (angle-weighted vs group-based)
- Runtime modification (MeshDataTool)
- Implementation recommendations for DX12 engines

Key insight: Godot's smoothing groups are simpler than UE5's edge-based approach but equally effective.

---

### Suite C: UE5 SSR Architecture (450+ lines, 18KB)

#### 6. ue5-ssr-architecture.md

**Screen-space reflection pipeline, parameters, quality tiers**

---

## Key Resources by Task Type

### For Normal Smoothing / Mesh Import
- **Start here:** godot4-normal-smoothing-analysis.md (Section 1: Smoothing Group System)
- **Implementation:** Section 11: Recommendations for UzEngine
- **Comparison:** Section 12: Godot vs UE5 vs Industry

### For Rendering Quality Parity
- **Start here:** ue5-rendering-cvars-reference.md
- **Implementation:** uzengine-cvar-implementation-guide.md
- **Anti-patterns:** cvar-architecture-antipatterns.md

### For Advanced Rendering
- **SSR:** ue5-ssr-architecture.md

---

## Key Statistics

| Item | Count |
|------|-------|
| Total CVars | 1,000+ |
| Categories | 20 |
| Lines of Documentation | 1,750 |
| Implementation Steps | 7 |
| Anti-Patterns | 10 |

---

## Implementation Priority

**Phase 1 (Week 1)**: Tone Mapping, Bloom, TAA, Shadows, GTAO, Fog  
**Phase 2 (Weeks 2-3)**: SSR, SSGI, Atmosphere, Advanced Fog  
**Phase 3 (Weeks 4-6)**: Clouds, Hair, Ray Tracing, Lumen, Advanced Shadows

---

## Critical Rules

1. Every parameter is a CVar (no exceptions)
2. CVar flows: component → CB → shader
3. CB struct matches shader layout exactly
4. Shaders read CB, never hardcoded
5. Validate ranges in C++ before GPU
6. Update CB every frame (not cached)
7. Document thoroughly (units, range, reference)
8. Test at runtime (change CVar, see result next frame)
9. No recompilation needed
10. Defaults from published references

---

## Next Steps

1. Read README.md (5 min)
2. Skim ue5-rendering-cvars-reference.md (10 min)
3. Study uzengine-cvar-implementation-guide.md (30 min)
4. Review cvar-architecture-antipatterns.md (20 min)
5. Pick 1 feature, follow all 7 steps
6. Test at runtime
7. Repeat

