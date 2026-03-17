<!-- doc-meta -->
# UE5 Shadow Parity Debugging Checklist

**For**: Comparing DX11 vs DX12/Vulkan shadow darkness
**Urgency**: P0 (affects visual parity)
**Expected Time**: 2-4 hours with RenderDoc

---

## Quick Diagnosis Path

If DX12/Vulkan shadows are darker than DX11:

1. **Is receiver bias implemented at all?** (5 min)
   - Search code for `lerp(ProjectionDepthBiasParameters.z, 1.0, NoL)`
   - If not found: **THIS IS THE BUG**

2. **Is SkyLight ambient applied?** (5 min)
   - Check if scene has a SkyLightComponent active
   - Verify its `MinOcclusion` is 0.0 (default)

3. **RenderDoc attenuation buffer compare** (30 min)
   - Capture both DX11 and DX12 at same frame
   - Side-by-side comparison of light attenuation texture
   - If values differ significantly: encoding/decoding mismatch

4. **Receiver bias console var test** (15 min)
   - Add temporary CVar to enable/disable receiver bias
   - Test with `uz.Shadow.ReceiverBias 0` (no bias) vs `1` (max bias)
   - If darkness doesn't change: receiver bias not connected to TransitionScale

---

## Detailed Checklist

### Phase 1: Code Architecture Review

**File Locations to Check**:
- [ ] Shadow projection shader: `runtime/engine/renderer/passes/*/shadow_pass.hlsl` (or equivalent)
- [ ] Shadow filtering: `runtime/engine/renderer/passes/*/shadow_filtering.hlsli`
- [ ] Deferred lighting: `runtime/engine/renderer/passes/*/deferred_lighting.hlsl`
- [ ] SkyLight computation: `runtime/engine/renderer/core/*/skylight*.h/.cpp`

**Question 1: Is receiver bias calculated per-pixel?**

Search for this pattern (or equivalent):

```hlsl
const float NoL = saturate(dot(WorldNormal, LightDirection));
const float ReceiverBiasMin = ProjectionDepthBiasParameters.z;  // e.g., 0.1
float ModulatedBias = lerp(ReceiverBiasMin, 1.0, NoL);
```

- [ ] **YES** → Receiver bias is implemented, move to Phase 2
- [ ] **NO** → Code has constant bias instead
  ```hlsl
  float ModulatedBias = ReceiverBiasDirect;  // CONSTANT, wrong!
  ```
  **ACTION**: Implement NoL-modulated bias as shown in `ShadowProjectionPixelShader.usf:226-251`

**Question 2: Is receiver bias applied to TransitionScale?**

```hlsl
float TransitionScale = SoftTransitionScale.z * ModulatedBias;
```

- [ ] **YES** → TransitionScale is correctly modulated
- [ ] **NO** → TransitionScale is computed without bias
  **ACTION**: Multiply by receiver bias value

**Question 3: Does DX12/Vulkan code path have receiver bias?**

- [ ] Check if there's a `#ifdef DX12` or `#ifdef VULKAN` branch that SKIPS receiver bias
  **ACTION**: Ensure all backends use the same receiver bias logic

---

### Phase 2: SkyLight Ambient Floor

**Question 4: Is SkyLightComponent active in scene?**

- [ ] Manually add SkyLightComponent to test scene
  ```cpp
  ASkyLight* SkyLight = World->SpawnActor<ASkyLight>();
  USkyLightComponent* SkyComp = SkyLight->GetLightComponent();
  SkyComp->SetIntensity(1.0f);
  SkyComp->MarkRenderStateDirty();
  ```

**Question 5: Is MinOcclusion applied in shader?**

Search for:
```hlsl
float MinOcclusion = OcclusionTintAndMinOcclusion.w;
float AmbientFactor = lerp(1.0, BentNormalLength, MinOcclusion);
// Or similar pattern
```

- [ ] **YES** → MinOcclusion is applied
- [ ] **NO** → Ambient doesn't have a floor
  **ACTION**: Apply MinOcclusion as shown in `SkyLightingShared.ush:60-62`

**Question 6: Default value is correct?**

- [ ] Verify `MinOcclusion` default is `0.0f` (allows arbitrarily dark shadows)
- [ ] If code sets a different default (e.g., 0.1), shadows will be lighter

---

### Phase 3: Attenuation Buffer Encoding/Decoding

**Question 7: Shadow value is sqrt-encoded before storage?**

In shadow projection shader:
```hlsl
OutColor = EncodeLightAttenuation(FadedShadow);
// Should expand to:
OutColor = sqrt(FadedShadow);
```

- [ ] **YES** → Encoding is applied
- [ ] **NO** → Shadow stored directly without sqrt
  **ACTION**: Apply sqrt encoding as in `Common.ush:1202-1207`

**Question 8: Attenuation buffer is decoded correctly?**

In deferred lighting shader:
```hlsl
float4 Attenuation = GetPerPixelLightAttenuation(ScreenUV);
float Shadow = Square(Attenuation.r);  // Decode: sqrt^2
```

- [ ] **YES** → Square() decode is applied
- [ ] **NO** → Using encoded value directly
  **ACTION**: Apply Square() decode before using shadow factor

**Question 9: Backend-specific decoding?**

- [ ] Check if DX12/Vulkan code paths have DIFFERENT decoding
- [ ] Ensure all backends use `Square(EncodedValue)`

---

### Phase 4: RenderDoc Capture & Analysis

**Setup**:
```bash
# DX11 capture
demo.exe --headless --renderdoc-capture 10 --frames 15
# → Examine: Light Attenuation Buffer (usually R8/R16F texture)

# DX12 capture
demo.exe --headless --renderdoc-capture 10 --frames 15
# → Examine: Light Attenuation Buffer (same texture slot)
```

**Visual Comparison**:

| Aspect | DX11 | DX12/Vulkan | Match? |
|--------|------|-------------|--------|
| Attenuation buffer values | 0.0-1.0 range | 0.0-1.0 range | [ ] |
| Shadow edge softness | Gradual | ? | [ ] |
| Receiver bias effect | Strong | ? | [ ] |
| Darkest shadow value | ~0.3 | ? | [ ] |
| Brightest lit value | ~1.0 | ? | [ ] |

**RenderDoc Python Script** (qrenderdoc --python):

```python
import renderdoc as rd

rd.InitialiseReplay(rd.GlobalEnvironment(), [])
cap = rd.OpenCaptureFile()
cap.OpenFile('capture_dx11.rdc', '', None)
result, ctrl = cap.OpenCapture(rd.ReplayOptions(), None)

# Find light attenuation buffer
for res in ctrl.GetResources():
    if 'Attenuation' in res.name or 'Shadow' in res.name:
        print(f"Found: {res.name}")

# Sample a shadowed pixel
px = ctrl.PickPixel(resource_id, x, y, rd.Subresource(0, 0, 0), rd.CompType.Float)
print(f"DX11 shadowed pixel: {px.floatValue[0]}")

ctrl.Shutdown()
cap.Shutdown()

# Repeat for DX12...
```

---

### Phase 5: Receiver Bias Console Variable Test

**Add to your codebase**:

```cpp
// In ShadowRendering.cpp or similar
static TAutoConsoleVariable<float> CVarReceiverBiasOverride(
    TEXT("uz.Test.ReceiverBiasOverride"),
    -1.0f,
    TEXT("Override receiver bias for testing (-1 = use default)"),
    ECVF_RenderThreadSafe);
```

**In shader**:

```hlsl
float ReceiverBias = ProjectionDepthBiasParameters.z;

#if USE_DEBUG_RECEIVER_BIAS_OVERRIDE
    if (DebugReceiverBiasOverride >= 0.0)
        ReceiverBias = DebugReceiverBiasOverride;
#endif

Settings.TransitionScale = SoftTransitionScale.z * lerp(ReceiverBias, 1.0, NoL);
```

**Test Cases**:

```bash
# Test 1: No receiver bias (hard edges, dark shadows)
uz.Test.ReceiverBiasOverride 0.0

# Test 2: Full receiver bias (soft edges, lighter shadows)
uz.Test.ReceiverBiasOverride 1.0

# Test 3: Default (should look correct)
uz.Test.ReceiverBiasOverride -1.0
```

- [ ] DX11 darkness changes with all 3 values?
- [ ] DX12/Vulkan changes identically to DX11?
- [ ] If DX12/Vulkan doesn't change: receiver bias not wired correctly

---

### Phase 6: TransitionScale Validation

**Add debug output**:

```hlsl
// In shadow projection shader
#if DEBUG_TRANSITION_SCALE
    float DebugColor = ModulatedBias;  // Green if high (soft), Red if low (hard)
    OutColor = float4(DebugColor, 1.0 - DebugColor, 0, 1);
#endif
```

**Expected Visualization**:
- Surfaces facing light (NoL ≈ 1.0): RED (hard shadow edge, TransitionScale ≈ SoftTransitionScale)
- Surfaces angled away (NoL ≈ 0.0): GREEN (soft shadow edge, TransitionScale ≈ SoftTransitionScale * 0.1)

- [ ] DX11 shows this color gradient?
- [ ] DX12/Vulkan shows same gradient?

---

### Phase 7: Light Attenuation Gradient Check

**Add debug visualization** (temporary):

```hlsl
// In deferred lighting, visualize encoded attenuation value
#if DEBUG_ATTENUATION_VALUE
    OutColor.rgb = float3(Attenuation.r, Attenuation.r, Attenuation.r);  // Grayscale
#endif
```

**Expected Result**:
- Fully lit areas: ~1.0 (white)
- Shadows with receiver bias: ~0.5-0.8 (light gray)
- Hardest shadows: ~0.3-0.5 (medium gray, NOT black due to MinOcclusion)

- [ ] DX11 attenuation values: 0.3-1.0 range?
- [ ] DX12/Vulkan attenuation values: 0.3-1.0 range?
- [ ] Or is DX12/Vulkan using 0.0-1.0 range (darker)?

---

## Root Cause Decision Tree

```
DX12/Vulkan shadows darker than DX11?
├─ YES
│  ├─ Receiver bias implemented?
│  │  ├─ NO → IMPLEMENT receiver bias (Step 1 of implementation guide)
│  │  └─ YES
│  │     ├─ Receiver bias connected to TransitionScale?
│  │     │  ├─ NO → WIRE receiver bias to TransitionScale
│  │     │  └─ YES
│  │     │     ├─ SkyLight MinOcclusion applied?
│  │     │     │  ├─ NO → APPLY MinOcclusion floor
│  │     │     │  └─ YES
│  │     │     │     ├─ Attenuation decode correct?
│  │     │     │     │  ├─ NO → FIX decoding (Square encode/decode)
│  │     │     │     │  └─ YES
│  │     │     │     │     └─ UNKNOWN BUG — use RenderDoc to debug further
│  └─ NO → Likely backend-specific IBL or ambient contribution difference
```

---

## Expected Outcomes

### If Receiver Bias Was Missing

After implementing:
- Shadows should be **lighter** on surfaces angled away from light
- Shadow edges should be **softer** (gradual falloff)
- Fully lit areas **unchanged** (NoL ≈ 1.0 → lerp → full TransitionScale)

### If MinOcclusion Was Not Applied

After implementing:
- Darkest shadows should be **lighter** by the MinOcclusion factor
- Shadow interior should have **visible ambient color tint**
- Default MinOcclusion=0.0 means no change (no ambient floor)

### If Decoding Was Wrong

After fixing to `Square()`:
- All shadow values should **increase** (sqrt < 1.0, so sqrt^2 recovers original)
- Shadows should be **lighter** overall
- Parity with DX11 should be achieved

---

## Success Criteria

- [ ] DX11 and DX12 shadows have identical darkness across all surfaces
- [ ] Receiver bias effect visible: surfaces at grazing angles have softer, lighter shadows
- [ ] RenderDoc attenuation buffer captures show matching pixel values
- [ ] Console variable test shows DX11/DX12 respond identically to receiver bias override
- [ ] No artifacts (banding, popping, hard edges) when adjusting receiver bias

---

## Time Estimates

| Task | Time | Prerequisites |
|------|------|---|
| Code review (Phase 1) | 30 min | None |
| SkyLight check (Phase 2) | 15 min | Phase 1 |
| Attenuation review (Phase 3) | 15 min | Phase 1 |
| RenderDoc capture (Phase 4) | 45 min | Phases 1-3 |
| CVar test (Phase 5) | 20 min | Phases 1-3 |
| Debug visualization (Phase 6) | 30 min | Phases 1-3 |
| Final validation (Phase 7) | 15 min | All phases |

**Total**: 2-4 hours (depending on findings)

