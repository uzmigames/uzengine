# UE5 Rendering CVars Analysis — Complete Reference Index

**Status**: COMPLETE  
**Date**: 2026-04-04  
**Source**: UE5 5.6 (W:\UE_5.6_Source\Engine\Source\Runtime\Renderer\Private)  
**Total CVars Documented**: 1,000+  
**Total Documentation**: 1,750 lines, 67KB

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

## Document Breakdown

### 1. ue5-rendering-cvars-reference.md (608 lines)

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

---

### 2. uzengine-cvar-implementation-guide.md (482 lines)

**7-step process to implement CVars**

1. Define CVar in C++
2. Wire into constant buffer struct
3. Populate CB from CVars (read every frame)
4. Wire CB into shader
5. Validation & clamping (C++ side)
6. Console command registration
7. Runtime testing

Includes complete code examples for each step.

---

### 3. cvar-architecture-antipatterns.md (577 lines)

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

---

### 4. README.md (84 lines)

**Quick-start overview and navigation**

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

