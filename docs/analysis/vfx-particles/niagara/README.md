<!-- doc-meta
title: Niagara VFX Feature Audit — Index
author: Claude Code
date: 2026-03-30
status: complete
subsystem: vfx
-->

# Niagara VFX Feature Audit — Documentation Index

Complete analysis of UE5 Niagara module library to identify missing features for UzEngine VFX system.

**TL;DR**: 15 missing modules identified. Top 4 take 10 hours and transform particle quality. See below for quick links.

---

## Quick Start (Read First)

**START-HERE.md** — 10 minute quickstart
- TL;DR summary
- Top 5 features
- Implementation example
- Next action

**NIAGARA-AUDIT-SUMMARY.md** — Executive overview
- All 15 features ranked
- Implementation order
- Pattern template
- Timeline

---

## Decision Tree: Which Document to Read?

### "I want to pick a feature and start implementing"
→ **START-HERE.md** (5 min)  
→ **quick-reference-missing-vfx.md** (3 min to pick)  
→ **niagara-missing-features-audit.md** (find full params)  
→ Start coding!

### "I want to understand the full scope"
→ **NIAGARA-AUDIT-SUMMARY.md** (full overview)  
→ **niagara-missing-features-audit.md** (all 15 features)  
→ Plan next quarter

### "I'm implementing a feature and need UE5 source reference"
→ **ue5-niagara-source-map.md** (exact paths + patterns)  
→ Open UE5 source files  
→ Copy pattern

### "I want a ranked list by ROI"
→ **quick-reference-missing-vfx.md** (ranked by effort)  
→ Pick feature  
→ Go to START-HERE.md

---

## Document Guide

| Document | Purpose | Audience | Read Time |
|----------|---------|----------|-----------|
| **START-HERE.md** | Quick start guide + implementation walkthrough | Anyone | 5-10 min |
| **NIAGARA-AUDIT-SUMMARY.md** | Executive overview, implementation order, timeline | Project lead, planners | 5 min |
| **quick-reference-missing-vfx.md** | Ranked by ROI, integration points, checklist template | Implementers | 5 min |
| **ue5-niagara-source-map.md** | UE5 source paths, module structure pattern, binding system | Implementers (reference) | 5 min + lookup |
| **niagara-missing-features-audit.md** | Full feature specifications (parameters, defaults, integration) | Implementers (detailed) | 20 min + lookup |

---

## Key Findings

### Tier 1: Do First (10 hours, transforms quality)
1. **Drag** (2h) — velocity damping, natural feel
2. **Camera Offset** (1h) — depth softness, closeup glow
3. **Size by Speed** (3h) — motion blur illusion
4. **Sprite Facing** (4h) — 5 billboard modes, variety

### Tier 2: Dramatic Effects (6-8 hours each)
5. **Orbit/Rotate** (6h) — spiral, vortex, halo effects
6. **Particle Lights** (8h) — glowing particles, dynamic light
7. **Sub-UV** (4h) — flipbook animation
8. **Mesh Index** (5h) — mesh variants

### Tier 3: Polish (1-2 hours each)
9-15. Color scale, gravity, acceleration, curl noise, decal, material params, mesh rotation

---

## Recommended Reading Order

1. **START-HERE.md** (5 min) — understand the landscape
2. **NIAGARA-AUDIT-SUMMARY.md** (5 min) — see all 15 features
3. **quick-reference-missing-vfx.md** (3 min) — decide what's first
4. **niagara-missing-features-audit.md** (look up as needed) — full specs
5. **ue5-niagara-source-map.md** (reference) — UE5 source patterns

---

## How to Implement

**Example: Adding Drag**

1. Read START-HERE.md "How to Implement One Feature"
2. Pick Drag module
3. Read niagara-missing-features-audit.md for parameters
4. Read ue5-niagara-source-map.md for UE5 source path
5. Open U:\UnrealGITHUB\UnrealEngine\...\NiagaraStatelessModule_Drag.h
6. Copy the pattern
7. Implement in UzEngine
8. Test visually
9. Done!

**Estimated time per Tier 1 feature**: 1-4 hours

---

## All Features (Complete List)

| # | Name | Impact | Complexity | Hours | Tier |
|---|------|--------|-----------|-------|------|
| 1 | Drag | ★★★ | LOW | 2 | P0 |
| 2 | Camera Offset | ★★★ | LOW | 1 | P0 |
| 3 | Size by Speed | ★★★ | MED | 3 | P0 |
| 4 | Sprite Facing | ★★★ | MED | 4 | P0 |
| 5 | Orbit | ★★★ | MED | 6 | P1 |
| 6 | Particle Lights | ★★★ | HIGH | 8 | P1 |
| 7 | Sub-UV | ★★☆ | MED | 4 | P1 |
| 8 | Mesh Index | ★★☆ | MED | 5 | P1 |
| 9 | Scale Color | ★☆☆ | LOW | 1 | P2 |
| 10 | Gravity Force | ★☆☆ | LOW | 2 | P2 |
| 11 | Accel Force | ★☆☆ | LOW | 1 | P2 |
| 12 | Curl Noise | ★☆☆ | LOW | 1 | P2 |
| 13 | Decal | ★☆☆ | MED | 4 | P2 |
| 14 | Dynamic Params | ★☆☆ | MED | 6 | P3 |
| 15 | Mesh Rotation | ★☆☆ | LOW | 1 | P3 |

---

## Already Implemented in UzEngine

✓ GPU particle simulation  
✓ Billboard rendering  
✓ Color/size over lifetime  
✓ Spawn shapes  
✓ Curl noise, vortex forces, wind  
✓ Ground collision  
✓ Kill volumes  
✓ Sub-emitters  
✓ Lit particles  
✓ Soft particle fade  
✓ Blend modes  
✓ Trails/ribbons  
✓ Multi-emitter effects  
✓ UI editor  
✓ Presets  
✓ Frustum culling  

---

## Next Steps

1. Read START-HERE.md
2. Pick a Tier 1 feature
3. Create Rulebook task
4. Implement (follow pattern)
5. Test visually
6. Move to next

---

## Questions?

See the respective document:
- **Which feature first?** → START-HERE.md (decision tree)
- **How long does X take?** → quick-reference-missing-vfx.md (table)
- **What are the parameters?** → niagara-missing-features-audit.md
- **Where's the UE5 source?** → ue5-niagara-source-map.md
- **Full scope?** → NIAGARA-AUDIT-SUMMARY.md

---

## Files in This Analysis

```
docs/analysis/vfx/
├── README.md (this file)
├── START-HERE.md ← Read this first
├── NIAGARA-AUDIT-SUMMARY.md
├── quick-reference-missing-vfx.md
├── ue5-niagara-source-map.md
└── niagara-missing-features-audit.md
```

---

**Analysis Complete**: 2026-03-30  
**Source**: UE5.6+ Niagara Stateless Module Library  
**Confidence**: HIGH (all features sourced from actual UE5 code)
