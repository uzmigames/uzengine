# LOD & Budget System

> **Status**: Planned (Phase 3.68.0)
> **References**: [VFX System Overview](01-overview.md), [Particle System](02-particle-system.md), [Significance Manager](budget/vfx_significance_manager.h)

## Purpose

The LOD (Level of Detail) and Budget system ensures the particle engine stays within a per-frame GPU budget by:

1. **Evaluating LOD** based on distance and screen coverage (per emitter)
2. **Ranking emitters** by visual significance (priority, distance, visibility)
3. **Enforcing the budget** by degrading LOD on low-importance effects
4. **Reporting statistics** for profiling and debug overlays

This enables developers to spawn unlimited effects in a scene while the system automatically reduces quality on distant or off-screen emitters.

## Conceptual Overview

```
┌─────────────────────────────────────────────────────────────────────────────┐
│ Each Frame: LOD & Budget Evaluation                                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│ 1. Collect Emitters                                                         │
│    For each active emitter:                                                 │
│    - distanceToCamera = (emitterPos - cameraPos).Length()                  │
│    - isFrustumVisible = camera.Frustum().Contains(emitterBounds)           │
│    - particleDemand = emitter.GetMaxParticles()                            │
│    - priority = user-assigned importance (1–100)                           │
│                                                                              │
│ 2. Evaluate LOD Per Emitter                                                │
│    - Distance LOD: distances [50m, 100m, 200m] → Full/Reduced/Minimal/Cull│
│    - Screen-size LOD: 4px threshold → Culled if tiny on screen            │
│    - Result: max(distanceLOD, screenLOD) — use most aggressive             │
│                                                                              │
│ 3. Rank by Significance                                                    │
│    score = priority * (1.0 / (1.0 + distance * 0.01)) * (visible ? 1.0 : 0.1)
│    Sort emitters descending by score                                        │
│                                                                              │
│ 4. Enforce Budget                                                           │
│    if totalParticles > maxBudget:                                          │
│        Degrade least-significant emitters:                                 │
│        Full (1.0×) → Reduced (0.5×) → Minimal (0.25×) → Culled (0.0×)    │
│                                                                              │
│ 5. Apply & Report                                                           │
│    Set each emitter's spawn rate multiplier: spawnRate *= lodFactor       │
│    Update stats: particlesUsed, activeEmitters, budgetUtilization          │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

## Quality Levels

### LOD Tiers

| Level | Spawn Factor | Pixel Impact | Intent |
|-------|--------------|--------------|--------|
| **Full** | 1.00 | 100% density | Emitter is close or fills the screen — full quality |
| **Reduced** | 0.50 | 50% density | Medium distance or moderate screen coverage |
| **Minimal** | 0.25 | 25% density | Far distance or very small on screen |
| **Culled** | 0.00 | invisible | Beyond cull range or invisible — spawns no new particles |

The spawn factor is multiplied by the emitter's base spawn rate each frame. For example, an emitter with 100 particles/sec spawns 50/sec at Reduced LOD.

### Evaluation Criteria

Two independent criteria determine LOD; the most aggressive (highest LOD number) wins:

#### 1. Distance-Based LOD

```
distance = |emitterPos - cameraPos|

if distance < distances[0] (default 50m):    LOD = Full
if distance < distances[1] (default 100m):   LOD = Reduced
if distance < distances[2] (default 200m):   LOD = Minimal
else:                                        LOD = Culled
```

The thresholds are configurable per emitter via `VFXLodSettings::distances[3]`.

#### 2. Screen-Size LOD

```
screenPixels = (boundingRadius / max(distance, 0.001)) * (screenHeight * 0.5)

if screenPixels < minScreenPixels (default 4.0px):
    LOD = Culled
else:
    LOD = Full
```

**Rationale**: An emitter at 200m distance but filling 50% of the screen remains Full. Conversely, a nearby emitter that projects to <4 pixels is Culled to avoid visual noise.

### Final LOD

```
appliedLOD = max(distanceLOD, screenSizeLOD)
```

The *most restrictive* LOD is selected. This ensures conservative behavior: if either criterion demands Culled, the emitter is culled.

## Significance Scoring

The significance manager ranks emitters to determine which ones to degrade when the budget is tight.

### Score Formula

```
score = priority * (1.0 / (1.0 + distance * 0.01)) * visibilityMultiplier
```

Where:
- **priority** — User-assigned weight (1–100, typical). Higher = more important.
- **distance** — Shallow 1%-per-metre attenuation. An emitter at 100m has half the score of one at 0m.
- **visibilityMultiplier** — 1.0 if frustum-visible, 0.1 if off-screen. Off-screen effects aren't culled entirely but are heavily penalized.

### Example Scores

| Scenario | Priority | Distance | Visible | Score |
|----------|----------|----------|---------|-------|
| Muzzle flash (close, on-screen) | 100 | 0m | Yes | 100.0 |
| Fire (medium, on-screen) | 50 | 50m | Yes | 25.0 |
| Dust (far, on-screen) | 30 | 150m | Yes | 11.3 |
| Explosion (off-screen) | 80 | 10m | No | 3.6 |

When budget is exceeded, the Explosion (score 3.6) is degraded first, then Dust, then Fire, preserving the Muzzle Flash at full quality.

## Budget Enforcement

The `VFXSignificanceManager::EnforceBudget()` function implements the degradation algorithm:

```cpp
// Pseudocode
void EnforceBudget(vector<VFXEmitterSignificance>& emitters, uint32_t maxBudget) {
    // 1. Rank emitters (sort by score descending)
    RankEmitters(emitters);

    // 2. Sum current demand at Full LOD
    uint32_t totalParticles = sum of all emitter.particleDemand;

    // 3. If within budget, exit early
    if (totalParticles <= maxBudget) {
        return;
    }

    // 4. Degrade low-significance emitters until demand fits
    for (int i = emitters.size() - 1; i >= 0; --i) {  // Iterate low→high importance
        auto& emitter = emitters[i];

        // Try all LOD degradations: Full → Reduced → Minimal → Culled
        while (emitter.appliedLod < VFXLodLevel::Culled && totalParticles > maxBudget) {
            uint32_t oldDemand = emitter.particleDemand * spawnFactor[emitter.appliedLod];
            emitter.appliedLod++;
            uint32_t newDemand = emitter.particleDemand * spawnFactor[emitter.appliedLod];
            totalParticles -= (oldDemand - newDemand);
        }
    }
}
```

Key properties:
- **High-significance emitters** degrade only after all low-significance ones are fully culled
- **Smooth degradation** — each LOD step reduces demand proportionally
- **Predictable behavior** — deterministic, no randomness or flickering

## Quality Level Presets

`VFXBudget` provides named quality presets for easy configuration:

| Level | Max Particles | Use Case |
|-------|---------------|----------|
| **Low** | 125,000 | Low-end GPUs, mobile, web |
| **Medium** | 250,000 | Mid-range hardware (default for dev) |
| **High** | 500,000 | Modern discrete GPUs (RTX 3070+) |
| **Ultra** | 1,000,000 | High-end workstations, consoles |

The game can switch quality dynamically (e.g., in settings menu):

```cpp
VFXBudget budget(VFXQualityLevel::High);
// Later, when user changes settings:
budget.SetQualityLevel(VFXQualityLevel::Medium);
```

## Statistics & Profiling

The `VFXBudgetStats` struct is populated each frame for profiling overlays and debug HUDs:

```cpp
struct VFXBudgetStats {
    uint32_t totalBudget;        // Max particles for current quality level
    uint32_t particlesUsed;      // Alive particles across all emitters
    uint32_t particlesAvailable; // Budget remaining (totalBudget - particlesUsed)
    uint32_t activeEmitters;     // Emitters at Full/Reduced/Minimal LOD
    uint32_t culledEmitters;     // Emitters at Culled LOD
    float    budgetUtilization;  // particlesUsed / totalBudget (0–1)
};
```

### Example Debug Overlay Display

```
VFX Budget: 500,000 particles
Used:       342,000 (68.4%)
Available:  158,000
Active Emitters:  12
Culled Emitters:  5
```

### Frame Loop Integration

```cpp
// During emitter simulation:
vfxBudget.UpdateStats(
    aliveParticleCount,
    activeEmitterCount,
    culledEmitterCount
);

// Before rendering:
const auto& stats = vfxBudget.GetStats();
if (showDebugOverlay) {
    DebugDraw::Printf("VFX: %u / %u (%.1f%%)",
        stats.particlesUsed,
        stats.totalBudget,
        stats.budgetUtilization * 100.0f
    );
}
```

## Configuration Examples

### Per-Emitter LOD Settings

In the `.uzvfx` JSON asset:

```json
{
  "name": "distant_firefly_swarm",
  "emitter": {
    "spawn_rate": 500,
    "max_particles": 2000
  },
  "lod_settings": {
    "distances": [30.0, 60.0, 150.0]
  },
  "modules": [
    { "type": "sprite_render" }
  ]
}
```

This emitter culls more aggressively than the default (30m instead of 50m for Full→Reduced transition).

### Per-Emitter Significance

At runtime:

```cpp
emitter->SetPriority(100);  // High priority (e.g., muzzle flash)
emitter->SetLodsSettings({ 20.0f, 50.0f, 100.0f });  // Aggressive culling
```

Or lower:

```cpp
emitter->SetPriority(10);  // Low priority (background dust)
emitter->SetLodSettings({ 100.0f, 200.0f, 400.0f });  // Graceful distance falloff
```

### Global Budget Configuration

In game settings:

```cpp
enum class VFXQualityMode {
    Low,     // 125k particles
    Medium,  // 250k particles
    High,    // 500k particles (default)
    Ultra    // 1M particles
};

void SetVFXQuality(VFXQualityMode mode) {
    vfxSystem.GetBudget().SetQualityLevel(
        static_cast<VFXQualityLevel>(mode)
    );
}
```

## Performance Guidelines

### Typical Frame Cost

- **Emitter counting & significance scoring** — O(E) where E = emitter count
  - 1000 emitters: <1ms on CPU
- **LOD evaluation per emitter** — O(1) per emitter
- **Budget enforcement** — O(E log E) due to sorting
- **Overall** — Negligible (<2ms) unless E > 10,000

### Memory Overhead

- `VFXLodSettings` — 12 bytes per emitter
- `VFXEmitterSignificance` — 40 bytes per emitter
- **Total for 1000 emitters** — ~50 KB (trivial)

### Optimization Tips

1. **Batch emitter collection** — Collect all emitters once per frame, not scattered
2. **Precompute LOD settings** — Don't recalculate thresholds each frame
3. **Cache camera position & frustum** — Reuse for all emitter evaluations
4. **Use conservative screen thresholds** — Default 4px works well; increase for low-end GPUs
5. **Avoid rapid quality changes** — Changing LOD per-frame causes visual popping; change per-second at most

## Common Issues

### Budget Exceeded, Nothing Culled

**Problem**: Despite high particle count, significance enforcement isn't culling enough.

**Solution**:
- Check `SetQualityLevel()` — is the budget correct?
- Verify emitter priorities — high priority emitters resist culling
- Lower priority on background effects or increase their distance thresholds

### Emitters Pop In/Out Too Aggressively

**Problem**: Emitters flicker between LOD levels as the camera moves.

**Solution**:
- Increase `VFXLodSettings::distances` to create wider LOD bands
- Disable screen-size culling if it's too aggressive: increase `minScreenPixels` (default 4.0)
- Implement hysteresis: add a 10–20% buffer before promoting an emitter to a higher LOD

### Off-Screen Emitters Dominate Budget

**Problem**: Background effects culled despite high priority.

**Solution**:
- The visibility multiplier (0.1 for off-screen) is aggressive by design
- If certain background effects are critical, increase their priority significantly
- Or manually set their LOD to Full regardless of significance score

## Related Documentation

- [VFX System Overview](01-overview.md)
- [Particle System](02-particle-system.md) — Emitter architecture
- [VFX Modules](03-vfx-modules.md) — How modules affect particle count
- [Mesh Particle Rendering](19-mesh-particle-rendering.md) — Mesh instances also use LOD

## Phase Roadmap

| Phase | Milestone | Status |
|-------|-----------|--------|
| 3.68.0 | LOD & budget system implementation | Planned |
| 3.69.0 | Hysteresis & temporal stability | Planned |
| 3.70.0 | Lua API for dynamic quality change | Planned |
