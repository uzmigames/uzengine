## 1. Implementation

- [x] 1.1 Implement CoC computation shader (`dof_setup_cs.hlsl`) — thin-lens equation from depth buffer per UE5 DOFCommon.ush:209-226 SceneDepthToCocRadius. Output R16F signed CoC (negative=near, positive=far). Clamped to CocMinRadius/CocMaxRadius. 8 root constants, 8x8 thread groups, bindless UZ_BINDLESS support.
- [ ] 1.2 Implement focal region support — flat zero-CoC zone of width `focalRegion` centered at `focalDistance`. Transitions ramp linearly over `nearTransition` and `farTransition` widths. Matches UE5's DepthOfFieldFocalRegion behavior.
- [ ] 1.3 Implement near/far separation downsample (`dof_downsample_ps.hlsl`) — downsample scene to half-res, split into near layer (CoC < 0) and far layer (CoC > 0). Near layer includes alpha for compositing. Use max CoC in 2×2 block for near to prevent in-focus bleeding.
- [x] 1.4 Implement disk gather bokeh blur (`dof_blur_cs.hlsl`) — 5-ring concentric disc gather per UE5 DOFGatherKernel.ush:37-53 ring pattern (6*ringId samples/ring, half-ring offsets). CoC-weighted accumulation with near/far field separation. InterleavedGradientNoise per-pixel rotation. 8 root constants, 8x8 threads, bindless support.
- [ ] 1.5 Implement diaphragm blade shape — generate sample distribution matching N-blade aperture (N=bladeCount). 5 blades=pentagon bokeh, 6=hexagon, 8=octagon. Blade rotation angle configurable.
- [ ] 1.6 Implement tile classification — 16×16 tiles, classify as foreground/background/mixed based on CoC range. Skip gather on in-focus tiles. Reduces cost by 30-50%.
- [ ] 1.7 Implement recombine pass (`dof_recombine_ps.hlsl`) — composite: far blur → in-focus (passthrough) → near blur (alpha blend). Smooth CoC-based interpolation at boundaries.
- [ ] 1.8 Implement camera parameter derivation — compute focalLength from FOV and sensorWidth: `focalLength = sensorWidth / (2 * tan(fov/2))`. Compute cocScale from fstop and focalLength.
- [ ] 1.9 Add DoFPassDesc to PostProcessStack — enabled/disabled, quality, focalDistance, fstop, sensorWidth, focalRegion, nearTransition, farTransition, maxNearBlur, maxFarBlur, bladeCount.
- [x] 1.10 Register DoF passes in render graph — DOF_Setup (CoC computation) and DOF_Blur (gather blur) wired after motion blur, before tonemap. Disabled by default (m_dofEnabled=false, UE5 default). Only dispatched when m_dofEnabled && m_dofFocusDistance > 0. Pipelines created in PipelineLibrary::GetDofSetup/GetDofBlur. Resources created/destroyed in CreateDofResources/DestroyDofResources. Downsample/recombine passes not yet implemented (full-res single-pass gather).
- [ ] 1.11 Create DoF demo — scene with objects at varying distances, adjustable focus distance slider, f-stop slider, bokeh visible on specular highlights.

## 2. Visual Verification

- [ ] 2.1 Screenshot: shallow DoF (f/1.4) — sharp subject, blurred foreground + background
- [ ] 2.2 Screenshot: bokeh highlights — specular dots show pentagon shape (5 blades)
- [ ] 2.3 Screenshot: deep DoF (f/16) — nearly everything in focus
- [ ] 2.4 Screenshot: near vs far blur — foreground objects correctly blurred
- [ ] 2.5 Screenshot: focus distance sweep — smooth transition as focus changes
- [ ] 2.6 Regression: existing demos still work

## 3. Documentation

- [ ] 3.1 Document DoF parameters with UE5 equivalents and real camera terminology
- [ ] 3.2 Add bokeh shape configuration guide (blade count, rotation)
- [ ] 3.3 Document CoC computation formula and camera parameter derivation
