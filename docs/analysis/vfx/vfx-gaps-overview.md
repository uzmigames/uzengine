<!-- doc-meta
  title: VFX System Architectural Gap Analysis — Documentation Index
  author: Claude Code
  date: 2026-03-25
  scope: Index and navigation for all VFX gap analysis documents
-->

# VFX System Architectural Gap Analysis

**Date**: 2026-03-25
**Scope**: Complete architectural analysis of UzEngine VFX system (phases 3.60-3.65) vs industry-standard systems (UE5 Niagara, Unity VFX Graph, Godot GPU Particles).

**Status**: ✅ Analysis complete; 18 missing features identified; 14-week roadmap created.

---

## Documents in This Analysis

### 1. **vfx-gaps-executive-summary.md** (START HERE)
**For**: PMs, lead engineers, decision-makers
**Length**: 1 page
**Contains**:
- What's currently implemented (60-70% feature complete)
- 18 missing features organized by tier (Gameplay, Rendering, Performance, UX)
- MVP timeline (6 weeks) vs full parity (14 weeks)
- Risk assessment + decision points

**Read this first if**: You need to decide priorities/timeline for team.

---

### 2. **vfx-architectural-gaps.md** (TECHNICAL DEEP DIVE)
**For**: Graphics engineers, architects
**Length**: 25 pages
**Contains**:
- Detailed gap analysis per feature
- Why each feature is missing (architectural reasons)
- Comparison vs UE5 Niagara, Unity VFX Graph, Godot
- Effort estimates (S/M/L/XL per feature)
- Recommended implementation sequence

**Read this to**: Understand architecture trade-offs; plan implementation strategy.

---

### 3. **vfx-missing-features-detailed.md** (IMPLEMENTATION SPEC)
**For**: Graphics engineers (writing code)
**Length**: 40 pages
**Contains**:
- Detailed spec for each of 18 features
- Parameter definitions + shader pseudocode
- File structure + estimated line counts
- GPU texture formats, constant buffer layouts
- Acceptance criteria per feature

**Read this to**: Implement specific features; understand GPU data layout.

---

### 4. **vfx-implementation-roadmap.md** (PHASE-BY-PHASE PLAN)
**For**: Project managers, team leads
**Length**: 30 pages
**Contains**:
- 5-phase rollout plan (3.66 through 3.70)
- Week-by-week timeline (12-16 weeks total)
- Dependencies + risk mitigations
- Success criteria per phase
- Effort breakdown (52-76 days for 1 FTE)

**Read this to**: Execute the work; track progress; manage team.

---

## Quick Reference: Feature Summary

### Tier 1: Critical Simulation (Gameplay)
**Timeline**: Weeks 3-5 | **Total Effort**: 20 days | **Blocking**: Gameplay features

| Feature | Effort | Weeks | Priority |
|---------|--------|-------|----------|
| Attractor Forces | M | 1 | P0 |
| Vector Field Sampling | M | 1 | P0 |
| Sub-Emitters (Event-Driven) | L | 2 | P0 |
| Advanced Constraints | S | 0.5 | P1 |

**Why**: Gameplay effects (explosions, magic, weather) require dynamic forces.

---

### Tier 2: Rendering (Visual Quality)
**Timeline**: Weeks 6-9 | **Total Effort**: 20 days | **Blocking**: Visual polish

| Feature | Effort | Weeks | Priority |
|---------|--------|-------|----------|
| GPU Sorting (Radix Sort) | L | 1.5 | **P0** |
| Soft Particles | S | 0.5 | P1 |
| Flipbook Animation | M | 1 | P1 |
| Custom Materials | L | 1 | P1 |
| Distortion Particles | L | 1.5 | P2 |
| Strand Rendering | M | 1 | P2 |

**Why**: GPU sort fixes critical alpha-blending bug; others enable visual variety.

---

### Tier 3: Performance (Optimization)
**Timeline**: Weeks 10-12 | **Total Effort**: 10 days | **Blocking**: Large-scale effects

| Feature | Effort | Weeks | Priority |
|---------|--------|-------|----------|
| Frustum Culling | M | 0.5 | P2 |
| Dynamic LOD (GPU) | S | 0.25 | P2 |
| Async Compute | L | 1 | P3 |

**Why**: Enables 1000+ emitter scenes; reduces GPU latency.

---

### Tier 4: Editor & Tools (Developer UX)
**Timeline**: Weeks 13-16 | **Total Effort**: 16 days | **Blocking**: Artist iteration speed

| Feature | Effort | Weeks | Priority |
|---------|--------|-------|----------|
| Curve Editor UI | M | 1 | P1 |
| Live Preview | M | 1 | P1 |
| Parameter Validation | S | 0.5 | P2 |
| Visual Graph Editor | XL | 2.5 | P3 |

**Why**: Currently artists edit .uzvfx JSON; no visual feedback; slow iteration.

---

## Recommended Phases

### Phase 3.66: Foundation (Weeks 1-2)
**Goal**: Fix rendering bugs; verify near-complete features.

- ✅ Soft Particles (verify + polish)
- ✅ GPU Radix Sort (critical rendering fix)

**Effort**: 6-9 days | **Deliverable**: Correct alpha blending for 50k particles.

---

### Phase 3.67: Core Simulation (Weeks 3-5)
**Goal**: Gameplay-ready force system.

- ✅ Attractor Forces
- ✅ Vector Field Sampling
- ✅ Sub-Emitters (event-driven)
- ✅ Advanced Constraints

**Effort**: 15-20 days | **Deliverable**: Full force system; compound effects possible.

---

### Phase 3.68: Rendering Toolkit (Weeks 6-9)
**Goal**: Rich visual variety.

- ✅ Flipbook Animation
- ✅ Custom Materials
- ✅ Distortion Particles
- ✅ Strand Rendering

**Effort**: 16-20 days | **Deliverable**: Match UE5 Niagara rendering scope.

---

### Phase 3.69: Editor Tools (Weeks 10-14)
**Goal**: Developer iteration speed.

- ✅ Curve Editor UI (VSCode)
- ✅ Live Preview (real-time feedback)
- ✅ Parameter Validation (error diagnostics)

**Effort**: 10-12 days | **Deliverable**: Artists no longer edit JSON; fast iteration.

---

### Phase 3.70: Polish & Advanced (Weeks 15-16+)
**Goal**: Optimization; optional features.

- ⚠️ Frustum Culling (performance)
- ⚠️ Async Compute (latency reduction, if RHI ready)
- ⏸️ Visual Graph Editor (defer to post-launch)

**Effort**: 5-15 days | **Status**: Optional; implement if time allows.

---

## Implementation Sequence (Dependency Graph)

```
Week 1-2:   3.66.0 (Soft Particles)      → 3.66.1 (GPU Sort)
             ↓
Week 3-5:   3.67.0 (Attractors)  3.67.1 (Vector Fields)
             ↓        ↓
            3.67.2 (Sub-Emitters) ← Depends on event refactor
             ↓
            3.67.3 (Constraints)
             ↓
Week 6-9:   3.68.0-3.68.3 (Flipbook, Materials, Distortion, Strands)
             ↓
Week 10-14: 3.69.0-3.69.2 (Curve UI, Live Preview, Validation)
             ↓
Week 15+:   3.70.0-3.70.2 (Culling, Async, Graph Editor)
```

**Critical Path**: GPU Sort (3.66.1) → Attractors (3.67.0) → Flipbook (3.68.0)

---

## Effort Summary

| Phase | Duration | Days | 1 FTE | Status |
|-------|----------|------|-------|--------|
| 3.66 | 2 weeks | 9 | 1 | Ready to start |
| 3.67 | 3 weeks | 20 | 1 | Depends on 3.66 |
| 3.68 | 4 weeks | 20 | 1 | Depends on 3.67 |
| 3.69 | 4 weeks | 12 | 1 | Parallel OK |
| 3.70 | 2+ weeks | 10-15 | 1 | Optional |
| **MVP** (3.66-3.68) | **9 weeks** | **49 days** | **1 FTE** | **Week 9** |
| **FULL** (3.66-3.69) | **14 weeks** | **61 days** | **1 FTE** | **Week 14** |

---

## Key Decisions for Team

### 1. MVP Scope
**Options**:
- **A (6 weeks)**: Phases 3.66-3.67 only (forces + sort; basic rendering)
- **B (9 weeks)**: Phases 3.66-3.68 (full Tier 1-2; production-ready)
- **C (14 weeks)**: Phases 3.66-3.69 (add editor tools)

**Recommendation**: **Option B (9 weeks)** for shipped title with complex effects.

### 2. Visual Graph Editor
**Options**:
- **YES**: Include in Phase 3.69 (+2 weeks); artists can author visually
- **NO**: Ship with JSON editing + curve UI; defer graph editor to post-launch

**Recommendation**: **NO (defer)**. Curve UI + validation sufficient for MVP; graph editor is nice-to-have.

### 3. Performance Optimization
**Options**:
- **Skip**: Trust per-emitter LOD + budget system; add in 3.70 if needed
- **Include**: Add frustum culling + async compute in 3.70 for large-scale scenes

**Recommendation**: **Skip for MVP**. Add in post-launch if performance issues arise.

---

## Success Metrics

### Phase 3.66 Complete ✅
- [ ] Soft particles fade correctly at close range
- [ ] 50k particles render with correct depth order (back-to-front)
- [ ] GPU sort completes in < 1ms

### Phase 3.67 Complete ✅
- [ ] Attractors pull particles (visual verification)
- [ ] Vector fields apply force (wind, vortex)
- [ ] Sub-emitters spawn children on parent death
- [ ] Constraints pin/bound particles correctly

### Phase 3.68 Complete ✅
- [ ] Flipbook sprites animate correctly
- [ ] Custom materials override default shader
- [ ] Distortion particles create heat-haze effect
- [ ] Strands render as lines (hair, grass, rope)

### Phase 3.69 Complete ✅
- [ ] Curve UI fully functional; hot-reloads to engine
- [ ] Live preview panel shows effect changes in real-time
- [ ] Invalid .uzvfx files show diagnostic squiggles

### Phase 3.70 Complete ✅
- [ ] Frustum culling reduces GPU work for offscreen effects
- [ ] (Optional) Async compute running if RHI supports compute queue

---

## How to Use This Analysis

### For PM/Tech Lead:
1. Read **vfx-gaps-executive-summary.md** (5 min)
2. Review timeline + effort table above
3. Decide MVP scope (6 vs 9 vs 14 weeks)
4. Assign to graphics engineer
5. Use roadmap milestones for sprint planning

### For Graphics Engineer (Implementing):
1. Read **vfx-architectural-gaps.md** (understand why features are missing)
2. Read **vfx-missing-features-detailed.md** for target feature (detailed spec + code estimates)
3. Follow **vfx-implementation-roadmap.md** phase-by-phase
4. Reference individual feature specs as you code

### For Architect (Designing):
1. Read **vfx-architectural-gaps.md** (full gap analysis)
2. Review **vfx-missing-features-detailed.md** (data structures + GPU layouts)
3. Check **Dependencies** section in **vfx-implementation-roadmap.md**
4. Identify refactoring needs (e.g., event system for sub-emitters)

---

## Files Referenced in This Analysis

### UzEngine VFX Source
- `runtime/engine/vfx/vfx_system.h` — Central VFX manager
- `runtime/engine/vfx/core/gpu_particle_buffer.h` — Particle storage (ping-pong SoA)
- `runtime/engine/vfx/modules/vfx_module.h` — Module interface
- `runtime/engine/vfx/passes/particle_simulation_pass.h` — Compute pass
- `runtime/engine/vfx/passes/particle_render_pass.h` — Graphics pass
- `runtime/engine/vfx/asset/vfx_graph.h` — Asset system

### Documentation
- `docs/engine/25-vfx/` — VFX system docs
- `docs/engine/25-vfx/02-particle-system.md` — Particle system architecture
- `docs/engine/25-vfx/03-vfx-modules.md` — Module system design

---

## Further Reading

- **UE5 Niagara Architecture**: https://docs.unrealengine.com/5.0/en-US/niagara-overview/ (FX framework reference)
- **GPU Radix Sort**: NVIDIA "Parallel Radix Sort" whitepaper (for 3.66.1 implementation)
- **Event-Driven Systems**: "Game Engine Architecture" (Gregory 2019) Ch. 6 (for 3.67.2 design)

---

## Contact / Questions

For questions about this analysis or implementation details:
1. Review the specific feature in **vfx-missing-features-detailed.md**
2. Check **vfx-architectural-gaps.md** for "Why Missing" section
3. Consult **vfx-implementation-roadmap.md** for timeline/dependencies

---

## Version History

| Date | Author | Change |
|------|--------|--------|
| 2026-03-25 | Claude Code | Initial analysis (18 features, 14-week roadmap) |

---

**Status**: ✅ Analysis complete. Ready for team review and implementation planning.

