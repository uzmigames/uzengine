<!-- doc-meta
title: Niagara Audit — Start Here
author: Claude Code
date: 2026-03-30
status: guide
subsystem: vfx
-->

# Niagara VFX Feature Audit — Start Here

**TL;DR**: UzEngine is missing 15 Niagara modules. The top 4 (Drag, Camera Offset, Size by Speed, Sprite Facing) take 10 hours and transform particle quality. See the ranked list below.

---

## Quick Facts

- **15 missing features** identified in UE5 Niagara source
- **Top 4 = 10 hours work**, massive visual impact
- **All features ranked** by (visual impact, implementation effort)
- **Complete source maps** — exact UE5 file paths and line numbers
- **Ready to implement** — copy pattern, follow checklist

---

## The Top 5 Missing Features

### ⭐ TIER 1: Do These First (10 hours total)

| Feature | What It Does | Visual Gain | Work |
|---------|-------------|------------|------|
| **1. Drag** | Velocity damping (particles slow naturally) | Heavy smoke feel | 2h |
| **2. Camera Offset** | Softness near camera + depth sorting | Closeup glow | 1h |
| **3. Size by Speed** | Particles scale with velocity (motion blur) | Speed effect | 3h |
| **4. Sprite Facing** | 5 billboard modes (camera, flat, custom, position, blend) | Visual variety | 4h |

**After these 4**: Particles feel like real physics, not ballistic points.

### ⭐ TIER 2: Dramatic Effects (6-8 hours each)

| Feature | Visual | Work |
|---------|--------|------|
| **5. Orbit/Rotate** | Swirling vortex, halos, spirals | 6h |
| **6. Particle Lights** | Glowing particles, dynamic lighting | 8h |
| **7. Sub-UV** | Flipbook animation on texture atlas | 4h |
| **8. Mesh Index** | Multiple mesh variants per emitter | 5h |

### TIER 3: Polish (1-2 hours each)

Scale Color, Gravity Force, Acceleration Force, Curl Noise Force, Decal Attributes, Dynamic Material Params, Mesh Rotation Rate

---

## Read These (in order)

1. **NIAGARA-AUDIT-SUMMARY.md** (2 min) — overview
2. **quick-reference-missing-vfx.md** (3 min) — ranked list + checklist
3. **ue5-niagara-source-map.md** (5 min) — source file reference
4. **niagara-missing-features-audit.md** (full feature specs, as needed)

---

## How to Implement One Feature (Using Drag as Example)

### Step 1: Understand the Feature (10 min)
1. Read quick-reference → understand Drag visual goal
2. Read niagara-missing-features-audit.md → find Drag parameters + defaults
3. Note: `DragDistribution` (default 1.0) — scales velocity each frame

### Step 2: Read UE5 Source (10 min)
1. Open `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara\Internal\Stateless\Modules\NiagaraStatelessModule_Drag.h`
2. Note the structure:
   ```cpp
   class UNiagaraStatelessModule_Drag : public UNiagaraStatelessModule {
       FNiagaraDistributionRangeFloat DragDistribution = 1.0f;
       void BuildEmitterData(...) override;
       // That's it!
   };
   ```

### Step 3: Create Rulebook Task (5 min)
```
Title: "Add Drag module to VFX system"
Checklist:
  [ ] Read NiagaraStatelessModule_Drag.h
  [ ] Add DragDistribution UPROPERTY to VfxEmitterModule
  [ ] Add GPU compute code: velocity *= (1.0 - drag * dt)
  [ ] Add editor UI slider: "Drag Coefficient"
  [ ] Test visually: particles should slow down
  [ ] Update tasks.md
```

### Step 4: Implement (2 hours)
1. **C++ setup** (30 min)
   - Add `float dragCoeff = 1.0f;` to emitter module struct
   - Add editor UI (slider 0.0-10.0)

2. **GPU update** (30 min)
   - In particle_update_cs.hlsl: `velocity *= (1.0 - dragCoeff * deltaTime);`
   - Pass dragCoeff via constant buffer

3. **Test** (1 hour)
   - Load demo, spawn particles with drag=0 (instant deceleration)
   - Spawn particles with drag=2 (instant deceleration)
   - Spawn particles with drag=0.5 (slow deceleration)
   - Screenshot each, verify visual difference
   - Adjust formula if needed

### Step 5: Verify and Mark Done (10 min)
- Build succeeds
- Demo runs, particles behave correctly
- screenshots show drag effect
- Update tasks.md: `[x] Add Drag module — velocity damping implemented`

**Total: 2 hours**

---

## Integration Points (Where Changes Go)

| Feature Type | Files to Change |
|--------------|-----------------|
| Velocity damping (Drag, Gravity, Accel) | `particle_update_cs.hlsl` + emitter struct |
| Billboard variety (Sprite Facing) | `particle_billboard_vs.hlsl` + vertex shader permutations |
| Size scaling (Size by Speed) | `particle_billboard_vs.hlsl` + emitter struct |
| Camera offset | `particle_billboard_vs.hlsl` |
| Particle lights | New compute pass + light culling |
| Sub-UV | `particle_billboard_ps.hlsl` + UV offset |
| Mesh variant | Indirect draw args + mesh index buffer |

All files in `runtime/shaders/vfx/` and `runtime/engine/vfx/`

---

## The Niagara Pattern (All Modules Use This)

Every feature follows the same structure:

```cpp
// 1. Define parameters (editor UI)
struct VfxModuleParams {
    float param1 = defaultValue;
    Vec3 param2 = defaultVector;
    // ... etc
};

// 2. Update shader (apply the feature)
// In compute shader:
particle.velocity *= some_function(params, particle);

// 3. Editor UI (slider, color picker, etc)
// In editor code:
ImGui::SliderFloat("Param1", &params.param1, min, max);
```

**That's it.** Every feature is this simple.

---

## Common Gotchas

1. **Drag coefficient scale** — UE5 default is 1.0, not 0.1 or 10.0
2. **Coordinate spaces** — Some features use World/Simulation space (read source)
3. **Distribution system** — UE5 has FNiagaraDistributionFloat for curves/ranges (we use LUT ramps)
4. **Feature masks** — Some modules enable special renderer behavior (e.g., Particle Lights)

**Solution**: Always read the UE5 source header first. Don't guess.

---

## Decision Tree: Which Feature First?

```
Want particles to feel more natural?
  → Yes: Drag (2h) + Gravity (2h) = 4h
  
Want better visual variety?
  → Yes: Sprite Facing (4h)
  
Want motion blur / speed effects?
  → Yes: Size by Speed (3h)
  
Want glowing particles?
  → Yes: Particle Lights (8h)
  
Want spiral/swirling effects?
  → Yes: Orbit (6h)
```

**Recommended order**: Drag → Camera Offset → Size by Speed → Sprite Facing (10h total)

---

## Success Metrics

After implementing Tier 1 (4 features, 10h):

- [ ] Particles decelerate naturally when Drag > 0
- [ ] Particles fade to soft glow near camera (Camera Offset)
- [ ] Particles scale larger/smaller based on speed
- [ ] Can switch between 5 different billboard modes
- [ ] Demo runs smooth, no visual artifacts
- [ ] All 4 features appear in editor UI with sliders

---

## Questions?

1. **"How do I know what parameters to use?"**  
   → Read the UE5 source module header. It lists all parameters + defaults.

2. **"What if I can't figure out the math?"**  
   → UE5 shader source is in `.ush` files. Copy the formula, adapt to UzEngine.

3. **"Do I need to implement all 15 features?"**  
   → No. Tier 1 (4 features) is sufficient for high visual impact. Tier 2-3 are optional polish.

4. **"How long does each feature take?"**  
   → See the ranked table. Drag is 2h, Sprite Facing is 4h, Particle Lights is 8h.

5. **"Where are the UE5 sources?"**  
   → `U:\UnrealGITHUB\UnrealEngine\Engine\Plugins\FX\Niagara\Source\Niagara\Internal\Stateless\Modules\`

---

## Files in This Analysis

| File | Purpose |
|------|---------|
| **NIAGARA-AUDIT-SUMMARY.md** | Executive overview, implementation order |
| **quick-reference-missing-vfx.md** | Ranked list, checklist template, ROI |
| **ue5-niagara-source-map.md** | UE5 file paths, pattern templates, binding system |
| **niagara-missing-features-audit.md** | Complete feature specs (parameters, defaults, integration) |
| **START-HERE.md** (this file) | Quick start guide |

---

## Next Action

1. Read NIAGARA-AUDIT-SUMMARY.md (5 min)
2. Pick a feature from Tier 1 (e.g., Drag)
3. Follow the "How to Implement One Feature" example above
4. Create a Rulebook task
5. Start coding

**Estimated timeline**: Tier 1 complete in 1 working session (10 hours)

Good luck! 🚀

