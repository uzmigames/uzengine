# Crimson Desert · Ray Reconstruction Integration

How BlackSpace integrates NVIDIA DLSS 3.5 Ray Reconstruction (and vendor
equivalents: AMD FSR "Ray Regeneration", Intel XeSS AI denoiser) as the
**primary denoiser** — replacing hand-tuned SVGF / NRD stacks.

---

## 1 · What Ray Reconstruction actually is

"Ray Reconstruction" (RR) is a marketing name for a class of AI-based
denoiser+upscalers. NVIDIA's DLSS 3.5 implementation is a transformer-based
neural network trained on pairs of `(1 spp noisy path-traced frame, 16K+ spp
offline reference)`.

Key design choices that matter for integration:

1. **Replaces BOTH denoising and upscaling in one network.** You cannot run RR "on top of" an existing SVGF — it subsumes SVGF.

2. **Requires demodulated inputs.** You pre-divide noisy shading by albedo, metallic, fresnel coefficient. The network outputs demodulated clean shading; you remultiply material outside.

3. **Requires separated diffuse and specular signals.** Mixed diffuse+specular goes badly because the two have wildly different noise characteristics and temporal behavior.

4. **Uses G-Buffer demodulation data.** Depth, normal, motion, albedo, roughness are part of the input tensor.

5. **Transformer architecture (DLSS 3.5+)** vs CNN (DLSS 3.0 and earlier) — better at handling disocclusion and moving light sources because attention spans the whole patch rather than a fixed kernel.

---

## 2 · Input buffers BlackSpace must produce

Per NVIDIA DLSS 3.5 SDK documentation and deduced from forum reports of buffer
names leaking from Crimson Desert's RenderDoc captures:

| Buffer | Format | Notes |
|--------|--------|-------|
| `InColor_Specular` | R16G16B16A16_FLOAT | Specular radiance, demodulated by fresnel×G. |
| `InColor_Diffuse`  | R16G16B16A16_FLOAT | Diffuse radiance, demodulated by albedo×(1-metallic). |
| `InDepth`          | R32_FLOAT | Linear depth (not reversed-Z raw). |
| `InMotionVectors`  | R16G16_SNORM | Screen-space, in pixels (negative = moved left). |
| `InNormals`        | R16G16_FLOAT | Octahedral-encoded world-space. |
| `InAlbedo`         | R8G8B8A8_SRGB | Base color. |
| `InRoughness`      | R8_UNORM | — |
| `InMetallic`       | R8_UNORM | — |
| `InSpecularHitDistance` | R16_FLOAT | For reflection ray length — critical for preserving reflection detail. |
| `InDiffuseHitDistance` | R16_FLOAT | — |
| `InExposure`       | R8 (texture or single float) | Current frame exposure scalar. |
| `InBias`           | R16G16_FLOAT | Jitter applied to camera this frame, in pixels. |

Outputs:

| Buffer | Format | Notes |
|--------|--------|-------|
| `OutColor` | R16G16B16A16_FLOAT | **Upsampled** to output resolution. Clean HDR. Ready for tonemap. |

RR is therefore BOTH the denoiser AND the upscaler. BlackSpace does not run
TAA or any post-process upscale separately — RR's output *is* the final
post-AA HDR.

---

## 3 · Pipeline integration point

```text
┌──── raw path-traced output (input res, e.g. 2560×1440) ─────────────┐
│                                                                    │
│  [diffuse] × albedo⁻¹         ← demodulate                         │
│  [specular] × fresnel⁻¹                                            │
│                                                                    │
└──────────────┬─────────────────────────────────────────────────────┘
               │
               ▼
  ┌─────────────────────────────┐
  │  DLSS 3.5 Ray Reconstruction │    GPU: Tensor cores
  │  (or FSR RR / XeSS AI)       │
  └─────────────┬────────────────┘
                │
                ▼
  clean HDR at OUTPUT res (3840×2160)
                │
                ├── × albedo      ← remodulate diffuse
                ├── × fresnel     ← remodulate specular
                │
                ▼
  tonemap / bloom / DoF / final post
```

If RR fails (older HW, user disabled it), BlackSpace falls back to a
traditional denoiser stack (SVGF diffuse + specular, then TSR upscale). The
engine supports **both code paths simultaneously** and swaps at runtime.

---

## 4 · Why demodulation is non-negotiable

Without demodulation, the neural net sees a signal like
`albedo(x,y) × (brightness + noise)`. Different textures have different
albedo, so the noise level varies by texture. The net would need to learn
"denoise more on dark materials" — fighting a frequency spectrum that varies
per pixel.

With demodulation, the noise is uniform across the frame (pure lighting
noise). The net learns one denoising prior that works everywhere.

UzEngine's current SSGI does NOT demodulate. Adding demodulation is a
must-do even before we look at neural denoising.

---

## 5 · Hit-distance as a denoiser hint

Modern denoisers (both classical NRD and neural RR) use **ray hit distance**
as a hint for kernel footprint:

- Short-distance reflection ray → sharp, small kernel (mirror)
- Long-distance reflection ray → blurry, wide kernel (rough)

NVIDIA's "NRD diffuse/specular" denoisers use this. RR expects it as an
input. If you don't provide it, reflections blur uniformly and rough surfaces
look crunchy while mirrors look mushy.

UzEngine's current SSR writes hit UVs but not hit distance. Easy add.

---

## 6 · AMD / Intel equivalents

### 6.1 FSR 3.1 + Ray Regeneration (AMD)

AMD's equivalent technology. Architecturally very similar:

- Demodulated diffuse + specular inputs
- Motion vectors + depth + normal
- Transformer-based (FSR 4 uses a shader-model transformer on plain compute)
- No tensor cores → runs on compute units, slightly slower but universal

### 6.2 XeSS + AI Denoiser (Intel)

Intel's equivalent. XeSS 2 introduces neural denoising on XMX (Arc GPUs) with
a DP4a fallback for non-Arc hardware.

Important note for Steam Deck / mobile APUs: these fallback modes will
still run, so BlackSpace's architecture gracefully degrades.

---

## 7 · What fails without RR

Forum reports on Crimson Desert when users disable RR:

- **GI low-frequency ghosting.** Without the neural net's temporal attention,
  the engine's fallback SVGF-style filter over-smooths GI, killing fine detail.
- **Reflections boiling.** 1 spp reflection without RR is visibly noisy on
  rough-metal surfaces.
- **Soft shadow penumbra flickers.** ReSTIR DI's shadow output is 1 ray per
  pixel; the neural net's attention to shadow edges smooths this. Classical
  denoise loses the edge.
- **Caustics / bright direct-hit pixels** fail to converge because 1 spp
  doesn't reliably find bright light sources; the neural net's prior "knows"
  what a caustic looks like and reconstructs it.

These failure modes are fundamental to 1 spp path tracing, not a bug in the
engine. The neural net is doing ~5× the perceived-quality work that the ray
tracer is.

---

## 8 · Implications for UzEngine

We do not need RR to ship a great renderer. Lumen ships without RR. But we do
need:

1. **Demodulated diffuse / specular split**. Mandatory for any denoiser (classical or neural) past a certain quality bar.

2. **Per-ray hit distance output**. Needed for spatial-kernel-size adaptation in the denoiser.

3. **NGX / FSR / XeSS SDK integration slot** in the post pipeline that *can* be RR but *falls back to* classical SVGF + TSR.

4. **TSR-style upscaler** that can consume noisy input (our current TAA history is designed for clean raster, not noisy signals).

See [../uzengine/aaa-default-pipeline.md](../uzengine/aaa-default-pipeline.md)
for where these slot into the new pipeline.

---

## 9 · References

- NVIDIA DLSS 3.5 "Ray Reconstruction" — nvidia.com/geforce/news/nvidia-dlss-3-5-ray-reconstruction/
- NVIDIA NGX SDK — developer.nvidia.com/rtx/dlss
- NVIDIA NRD (Real-time Denoising) — github.com/NVIDIA-RTX/NRD
- Christoph Schied et al., "Spatiotemporal Variance-Guided Filtering" (HPG 2017) — the SVGF denoiser classical stack that RR replaces.
- AMD FSR 3.1 / 4 documentation (gpuopen.com).
- Intel XeSS 2 documentation (intel.com/content/www/us/en/developer/articles/guide/get-started-xess-v2.html).
- Steam community forum thread "DLSS / RR / Visual bugs" (Crimson Desert app 3321460) — empirical observations of RR failure modes.
