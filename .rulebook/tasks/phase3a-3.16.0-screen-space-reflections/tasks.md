## 1. Implementation

- [ ] 1.1 Implement Hi-Z mip chain generation (`hiz_generate_cs.hlsl`) — compute shader, 2×2 min-depth reduction per mip level (6-8 mips). Output R32_FLOAT per mip. Dispatch per-mip with barrier between levels.
- [ ] 1.2 Implement SSR ray trace compute shader (`ssr_trace_cs.hlsl`) — per pixel: reconstruct view-space position from depth, compute reflection direction from view + normal. Hi-Z march: start at mip 0, step ray, if ray behind depth go finer mip, if in front go coarser mip. Stop on hit or max steps.
- [ ] 1.3 Implement binary refinement in trace shader — after Hi-Z finds approximate hit, bisect 8 times between last-in-front and first-behind positions for sub-pixel precision.
- [ ] 1.4 Implement SSR resolve shader (`ssr_resolve_ps.hlsl`) — at hit UV: sample scene color (pre-tonemap HDR), apply Fresnel weighting `F0 + (1-F0)*pow(1-NdotV, 5)`, apply roughness-based mip for glossy blur, screen-edge fade `saturate((fadeEnd - edgeDist) / (fadeEnd - fadeStart))`.
- [ ] 1.5 Implement roughness cutoff — if pixel roughness > maxRoughness (0.6 default), skip SSR entirely for that pixel, output 0 (falls back to IBL). Smooth transition near threshold.
- [ ] 1.6 Implement IBL fallback blend — `finalReflection = lerp(iblReflection, ssrReflection, ssrConfidence)`. Confidence = 1.0 on solid hit, 0.0 on miss, smooth transition at screen edges.
- [ ] 1.7 Implement SSR temporal filtering (`ssr_temporal_cs.hlsl`) — reproject previous SSR using velocity buffer, blend with current frame. Reject history when velocity high or reflection direction changed significantly.
- [ ] 1.8 Implement depth thickness test — reject hits where `abs(rayDepth - sceneDepth) > thickness`. Prevents false reflections from objects far behind the hit point.
- [ ] 1.9 Add SSRPassDesc to PostProcessStack — enabled/disabled, quality (Off/Low/Medium/High/Epic), maxRoughness, intensity, maxSteps, binarySteps, fadeStart, fadeEnd, thickness.
- [ ] 1.10 Register SSR passes in render graph — Hi-Z gen → SSR trace → resolve → temporal. SSR output bound to lighting pass as reflection source.
- [ ] 1.11 Create SSR demo — reflective floor plane (roughness=0.05), various objects above it, camera showing clear reflections. Toggle SSR on/off, roughness slider.

## 2. Visual Verification

- [ ] 2.1 Screenshot: reflective floor with reflected cubes/spheres — clear mirror-like reflection
- [ ] 2.2 Screenshot: roughness gradient — smooth=sharp reflection, rough=blurred, very rough=IBL only
- [ ] 2.3 Screenshot: screen edge fade — reflections near edges fade smoothly, no hard cutoff
- [ ] 2.4 Screenshot: SSR OFF vs ON — clear difference on reflective surfaces
- [ ] 2.5 Screenshot: moving objects reflected in real-time (not static like cubemap)
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document SSR parameters with UE5 equivalents
- [ ] 3.2 Add Hi-Z ray marching algorithm overview
- [ ] 3.3 Document SSR-IBL fallback blending strategy
