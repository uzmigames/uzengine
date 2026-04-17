<!-- doc-meta
  title: VFX System Gaps — Executive Summary
  author: Claude Code
  date: 2026-03-25
  scope: 1-page summary of architectural gaps
  context: For PM/lead engineer decision-making
-->

# VFX System Gaps — Executive Summary

## Current State (Phases 3.60-3.65)

UzEngine VFX has **solid foundational infrastructure**:
- ✅ GPU particle buffers (ping-pong SoA, 500k particle budget)
- ✅ Module composition system (13 built-in modules)
- ✅ Render graph integration (compute + graphics passes)
- ✅ Asset system (.uzvfx JSON, hot-reload)
- ✅ Advanced features (collisions, light particles, trails, decals, LOD)

**Status**: ~60% feature-complete vs UE5 Niagara; ~70% vs Unity VFX Graph.

---

## What's Missing (18 Features)

### Tier 1: Critical for Gameplay (Weeks 3-5)

| Feature | Impact | Effort |
|---------|--------|--------|
| **Attractor Forces** | Pull particles toward points (explosions, portals) | M (4-5 days) |
| **Vector Field Sampling** | Wind, vortex, environmental forces | M (5-6 days) |
| **Sub-Emitters** | Spawn children on death (compound effects) | L (8-10 days) |
| **Advanced Constraints** | Pin, bounds, string constraints | S (2-3 days) |

**Blocker**: Gameplay effects requiring dynamic forces (explosions, magic, weather) cannot be authored.

---

### Tier 2: Critical for Visual Quality (Weeks 6-9)

| Feature | Impact | Effort |
|---------|--------|--------|
| **GPU Sorting** | Correct alpha blending order (fixes rendering bugs) | L (5-7 days) |
| **Soft Particles** | Depth fade polish (likely 90% done) | S (1-2 days) |
| **Flipbook Animation** | Sprite animation (visual variety) | M (3-4 days) |
| **Custom Materials** | Artist shader override (flexibility) | L (3-4 days) |
| **Distortion Particles** | Heat haze, refraction (visual effects) | L (6-7 days) |
| **Strand Rendering** | Hair, rope, grass (line-based particles) | M (4-5 days) |

**Blocker**: Dense particle effects render with depth-sorting artifacts; limited visual variety.

---

### Tier 3: Performance & Optimization (Weeks 10-12)

| Feature | Impact | Effort |
|---------|--------|--------|
| **Frustum Culling** | Cull offscreen particles (10-20% GPU savings) | M (3-4 days) |
| **Dynamic LOD (GPU)** | Auto-scale spawn rate based on particle count | S (1-2 days) |
| **Async Compute** | Particle sim on separate queue (1-2ms latency gain) | L (depends on RHI) |

**Blocker**: Large-scale effects (1000+ emitters) may exceed GPU budget; no automatic scaling.

---

### Tier 4: Developer UX & Authoring (Weeks 13-16+)

| Feature | Impact | Effort |
|---------|--------|--------|
| **Curve Editor UI** | Visual curve authoring (faster iteration) | M (4-5 days) |
| **Live Preview** | Real-time effect preview in editor (dev speed) | M (4-5 days) |
| **Parameter Validation** | Error diagnostics in .uzvfx files | S (2-3 days) |
| **Visual Graph Editor** | Node-based effect authoring (artist UX) | XL (10+ days) |

**Blocker**: Artists author effects manually in JSON; no visual feedback; slow iteration.

---

## Impact by Category

### Simulation
**Currently**: Gravity, drag, turbulence, collision, constraints.

**Missing**: Attractors, vector fields, sub-emitters, P2P interaction, spatial queries.

**Gap**: ~40% of Niagara's simulation feature set.

### Rendering
**Currently**: Billboard sprites, mesh particles, light particles, trails, decals.

**Missing**: Flipbook, soft particles (polish), distortion, custom materials, strands, volumetric.

**Gap**: ~30% of Niagara's rendering feature set.

### Performance
**Currently**: Per-emitter LOD, significance manager, particle budget.

**Missing**: GPU sort, frustum culling, async compute.

**Gap**: ~50% of optimization techniques.

### Authoring
**Currently**: JSON-based .uzvfx editing, hot-reload, Lua bindings.

**Missing**: Visual editor, curve UI, live preview, validation.

**Gap**: ~60% of developer UX (artists still edit JSON).

---

## Recommended Implementation Plan

### MVP (6 weeks)
Implement **Tier 1 + core Tier 2**:
1. GPU Sort (critical rendering fix)
2. Attractors + Vector Fields (gameplay forces)
3. Flipbook (visual variety)
4. Curve Editor UI (dev speed)

**Result**: Renders correctly, gameplay effects possible, faster iteration.

### Full Feature Parity (14 weeks)
Add **Tier 3 + remaining Tier 2**:
- Sub-emitters, constraints
- Distortion, custom materials, strands
- Frustum culling, dynamic LOD
- Live preview, validation

**Result**: Production-ready; ships complex effects; artists can iterate without JSON.

### Advanced (16+ weeks)
Optional features:
- Async compute (if RHI ready)
- Visual graph editor (if team allows)
- P2P interaction, volumetric (high complexity; niche)

---

## Timeline & Effort

| Phase | Duration | Effort | Target Completion |
|-------|----------|--------|-------------------|
| **3.66** (Sort, soft particles) | 2 weeks | 9 days | Week 2 |
| **3.67** (Forces, sub-emitters) | 3 weeks | 20 days | Week 5 |
| **3.68** (Rendering toolkit) | 4 weeks | 20 days | Week 9 |
| **3.69** (Editor tools) | 4 weeks | 12 days | Week 13 |
| **3.70** (Polish, advanced) | 2+ weeks | 10-15 days | Week 16+ |
| **TOTAL (MVP)** | **6 weeks** | **38 days** | **Week 6** |
| **TOTAL (Full)** | **14 weeks** | **61 days** | **Week 14** |

**Assumes**: 1 FTE senior graphics engineer; can parallelize with other work.

---

## Risk Assessment

| Risk | Severity | Mitigation |
|------|----------|-----------|
| GPU sort is complex | Medium | Study NVIDIA algorithm + prototype early |
| Sub-emitters need event refactor | Medium | Design event queue carefully; not urgent |
| Distortion requires post-process integration | Medium | Verify post-process exists; if not, defer to 3.69 |
| Visual graph editor scope creep | High | **DEFER to MVP**; ship with JSON editing first |
| RHI doesn't support async compute | Low | Fall back to single queue; not critical for MVP |

---

## Decision Points

**For PM/Lead Engineer**:

1. **Do we need visual effects gameplay on launch?**
   - YES → Implement Phase 3.66-3.67 (forces, sorting). 6-week timeline.
   - NO → Defer to post-launch; focus on other systems first.

2. **Is artist iteration speed critical?**
   - YES → Add Phase 3.69 curve/preview UI. 10-week timeline.
   - NO → JSON editing sufficient; reduce to 6-9 weeks.

3. **How ambitious are target effects?**
   - Complex (explosions, weather, magic) → Phase 3.68 rendering (full 14 weeks).
   - Simple (sprites, trails) → Phases 3.66-3.67 sufficient (9 weeks).

4. **Is visual graph editor essential for MVP?**
   - YES → Plan Phase 3.69.2 (XL effort). **Recommend deferring**.
   - NO → JSON editing + curve UI sufficient (10 weeks faster).

---

## Recommendation

**For shipped title in 12-14 weeks**:

1. **Implement Phases 3.66-3.68** (Tiers 1-2): 9 weeks
   - Achieves gameplay-ready, visually rich VFX
   - Fixes alpha blending bug (GPU sort)
   - Enables all standard effects (explosions, magic, weather)

2. **Add Phase 3.69** (Editor tools): +4 weeks
   - Curve UI + live preview
   - Developer iteration speed
   - Parameter validation

3. **Defer Phase 3.70** (Optimization + graph editor)
   - Implement post-launch if needed
   - Frustum culling = easy win if time allows

**Timeline**: MVP at **week 9**, full feature-parity at **week 13**.

---

## Deliverables (This Analysis)

1. **vfx-architectural-gaps.md** — Complete 18-feature gap analysis vs industry systems
2. **vfx-missing-features-detailed.md** — Per-feature implementation specs + code estimates
3. **vfx-implementation-roadmap.md** — Phased 14-week roadmap with success criteria
4. **vfx-gaps-executive-summary.md** — This document (1-page executive reference)

**Next Steps**:
1. Review roadmap with team lead
2. Prioritize features (MVP vs nice-to-have)
3. Assign ownership to graphics engineer
4. Execute Phase 3.66 (2 weeks) as proof-of-concept

---

## Questions for Team

1. **When do we need visual effects for gameplay?** (Determines urgency)
2. **How many artists will author effects?** (Determines editor tooling priority)
3. **What's the target game scope?** (Determines feature set needed)
4. **Is RHI compute queue implemented?** (Determines async compute feasibility)
5. **Can we defer 3.70 (graph editor) to post-launch?** (Recommended)

