---
name: /gpu-debug
id: gpu-debug
category: Rendering
description: Systematic GPU debugging workflow for rendering issues. Uses RenderDoc/PIX capture analysis, LUT readback validation, and shader step-through instead of guessing.
---

<!-- RULEBOOK:START -->

# GPU Debug — Systematic Rendering Diagnosis

**NEVER guess at rendering bugs. ALWAYS follow this diagnostic workflow.**

## Step 1: Identify the Symptom Category

| Symptom | Likely Cause | Go To |
|---------|-------------|-------|
| Black screen / no output | Pipeline not bound, missing dispatch, wrong barriers | Step 2A |
| Wrong colors (too bright/dark/saturated) | Wrong units (m vs km), missing tonemap, bad CB values | Step 2B |
| White/gray sky | Transmittance = 1.0 everywhere (LUT not generated or not sampled) | Step 2B |
| Flickering / corruption | Race condition, missing barrier, UAV not flushed | Step 2C |
| Correct on frame 1, wrong later | Dirty flag not set, stale resource, double-buffer issue | Step 2D |

## Step 2A: Pipeline Validation (Black Screen)

1. **Check PIX markers exist** — every pass must have `PIXBeginEvent`/`PIXEndEvent`
2. **Verify dispatch dimensions** — compute: `Dispatch(ceil(W/threadX), ceil(H/threadY), 1)`
3. **Verify root signature bindings** — CB at correct slot, SRV/UAV at correct descriptor index
4. **Check barriers** — resource must be in `UnorderedAccess` state before compute write, `ShaderResource` before pixel shader read

## Step 2B: Value Validation (Wrong Colors / White Sky)

**This is the most common rendering bug. Use CPU readback to diagnose.**

1. **Add a debug readback path** (if not present):
   ```cpp
   // In the pass, after dispatch, add a conditional readback:
   if (cvarDebugDumpLUT) {
       // Transition LUT → CopySource
       // CopyTextureRegion to readback buffer
       // Map, read values, log with UZ_LOG
   }
   ```

2. **Validate known-good values** for the specific LUT:

   **Transmittance LUT (256x64 RGBA16F)**:
   | UV Position | Expected RGB | If Wrong |
   |------------|-------------|----------|
   | (1.0, 0.0) — zenith from ground | > (0.90, 0.95, 0.98) | Integration broken, or units wrong |
   | (0.5, 0.0) — horizon from ground | ~(0.3-0.5, 0.4-0.6, 0.5-0.7), B > R | Rayleigh scattering working |
   | (any, 1.0) — top of atmosphere | ~(1.0, 1.0, 1.0) | No atmosphere above = full transmission |
   | All zeros | LUT not dispatched or wrong UAV bound | |
   | All ones | Integration steps = 0 or density = 0 | |

3. **Check constant buffer values** — read back the CB and log every field:
   ```
   bottomRadiusKm=6360, topRadiusKm=6420   ← CORRECT (km)
   bottomRadiusKm=6360000, topRadiusKm=6420000  ← WRONG (meters!)
   rayleighBeta=(5.8e-3, 13.5e-3, 33.1e-3)  ← CORRECT (km^-1)
   rayleighBeta=(5.8e-6, 13.5e-6, 33.1e-6)  ← WRONG (m^-1)
   ```

4. **Isolate the pass** — render ONLY the LUT to screen as a fullscreen quad:
   ```hlsl
   // Temporary debug: output LUT directly
   return float4(transmittanceLut.SampleLevel(linearSampler, uv, 0).rgb, 1.0);
   ```

## Step 2C: Barrier / Sync Issues (Flickering)

1. Enable DX12 debug layer + GPU-Based Validation in debug builds
2. Check DRED breadcrumbs if device removed
3. Verify every resource transition: UAV→SRV, SRV→UAV, etc.

## Step 2D: Stale Resource (Correct then Wrong)

1. Check dirty flags — is the LUT regenerated when parameters change?
2. Check double-buffering — are you writing frame N but reading frame N-1?
3. Add frame counter logging to identify which frame goes wrong

## Step 3: Use External GPU Debuggers

### RenderDoc (Preferred — works with all GPUs)
1. Launch demo from RenderDoc: `renderdoc <exe path>`
2. Capture frame (F12)
3. Find the compute dispatch in Event Browser (search for PIX marker name)
4. Click dispatch → Pipeline State → CS tab → check bound CBV/SRV/UAV
5. Click UAV texture → Texture Viewer → inspect values (toggle channels, adjust range)
6. Right-click pixel → shows exact float values
7. Right-click dispatch thread → "Debug this thread" → step through HLSL

### PIX for Windows (DX12-only, best for shader debugging)
1. Launch with PIX: Attach → select process, or Launch → select exe
2. Take GPU capture
3. Find dispatch, click it → see full pipeline state with decoded CB fields
4. Right-click dispatch → "Debug Compute Shader" → enter thread ID → step through HLSL

### Compile Shaders for Debug
For step-through to work, compile with:
```
-Zi  (debug info)
-Od  (no optimization)
```
Add a `ShaderCompileFlags::Debug` mode that always passes these to DXC.

## Step 4: Atmosphere-Specific Checklist

- [ ] All atmosphere coordinates use **kilometers** (not meters)
- [ ] `bottomRadiusKm = 6360.0`, `topRadiusKm = 6420.0` (Earth defaults)
- [ ] Rayleigh coefficients: `(5.802e-3, 13.558e-3, 33.1e-3)` km^-1 at sea level
- [ ] Mie coefficient: `3.996e-3` km^-1 at sea level
- [ ] Rayleigh scale height: `8.0` km
- [ ] Mie scale height: `1.2` km
- [ ] UV mapping follows Bruneton 2017 non-linear parameterization (NOT linear cosine)
- [ ] `ClampCosine()` applied to prevent NaN from `sqrt(1 - mu*mu)`
- [ ] Integration direction: from sample point TOWARD top of atmosphere (not downward)
- [ ] LUT texture format: RGBA16F (not RGBA8 — values are in [0,1] float range)
- [ ] Compute shader entry point: `main` (not `CSMain` — UzEngine convention)

## Step 5: Automated Validation (CI-Ready)

For LUT validation without visual inspection:

```cpp
// After LUT dispatch + readback:
auto ValidateTransmittanceLUT(const float* data, uint32_t w, uint32_t h) -> bool {
    // Sample zenith from ground: pixel (w-1, 0)
    float r = data[0*4+0], g = data[0*4+1], b = data[0*4+2];
    if (r < 0.85f || g < 0.90f || b < 0.95f) {
        UZ_LOG(Atmosphere, Error, "LUT zenith transmittance too low: ({},{},{})", r, g, b);
        return false;
    }
    // Sample top of atmosphere: pixel (w/2, h-1)
    size_t topIdx = ((h-1) * w + w/2) * 4;
    float tr = data[topIdx+0], tg = data[topIdx+1], tb = data[topIdx+2];
    if (tr < 0.99f || tg < 0.99f || tb < 0.99f) {
        UZ_LOG(Atmosphere, Error, "LUT top-of-atm not ~1.0: ({},{},{})", tr, tg, tb);
        return false;
    }
    return true;
}
```

## Reference Tools

| Tool | Platform | Cost | Best For |
|------|----------|------|----------|
| RenderDoc | All GPUs | Free | Frame inspection, texture viewing, general debug |
| PIX | DX12 only | Free | Shader step-through, CB decode, perf |
| Nsight Graphics | NVIDIA only | Free | NVIDIA-specific perf, shader debug |
| NVIDIA Aftermath | NVIDIA only | Free | GPU crash dumps |
| DX12 Debug Layer | All DX12 | Built-in | API validation, barrier errors |
| GPU-Based Validation | All DX12 | Built-in | Descriptor access validation (slow) |
| DRED | All DX12 | Built-in | Device-removed crash breadcrumbs |

<!-- RULEBOOK:END -->
