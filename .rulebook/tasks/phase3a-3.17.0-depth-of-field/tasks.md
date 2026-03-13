## 1. Implementation

- [ ] 1.1 Implement CoC computation shader (`dof_coc_ps.hlsl`) — thin-lens equation from depth buffer. Output R16F: positive=far blur, negative=near blur, zero=in-focus. Clamp to maxNearBlur/maxFarBlur in pixels.
- [ ] 1.2 Implement focal region support — flat zero-CoC zone of width `focalRegion` centered at `focalDistance`. Transitions ramp linearly over `nearTransition` and `farTransition` widths. Matches UE5's DepthOfFieldFocalRegion behavior.
- [ ] 1.3 Implement near/far separation downsample (`dof_downsample_ps.hlsl`) — downsample scene to half-res, split into near layer (CoC < 0) and far layer (CoC > 0). Near layer includes alpha for compositing. Use max CoC in 2×2 block for near to prevent in-focus bleeding.
- [ ] 1.4 Implement disk gather bokeh blur (`dof_bokeh_cs.hlsl`) — for each pixel, gather `gatherSamples` points on a disk of radius=abs(CoC). Weight by CoC of sampled texel (larger CoC = more contribution). Supports circular disk (fast) and diaphragm polygon (quality).
- [ ] 1.5 Implement diaphragm blade shape — generate sample distribution matching N-blade aperture (N=bladeCount). 5 blades=pentagon bokeh, 6=hexagon, 8=octagon. Blade rotation angle configurable.
- [ ] 1.6 Implement tile classification — 16×16 tiles, classify as foreground/background/mixed based on CoC range. Skip gather on in-focus tiles. Reduces cost by 30-50%.
- [ ] 1.7 Implement recombine pass (`dof_recombine_ps.hlsl`) — composite: far blur → in-focus (passthrough) → near blur (alpha blend). Smooth CoC-based interpolation at boundaries.
- [ ] 1.8 Implement camera parameter derivation — compute focalLength from FOV and sensorWidth: `focalLength = sensorWidth / (2 * tan(fov/2))`. Compute cocScale from fstop and focalLength.
- [ ] 1.9 Add DoFPassDesc to PostProcessStack — enabled/disabled, quality, focalDistance, fstop, sensorWidth, focalRegion, nearTransition, farTransition, maxNearBlur, maxFarBlur, bladeCount.
- [ ] 1.10 Register DoF passes in render graph — CoC → downsample → near blur → far blur → recombine. After SSAO/SSR, before motion blur.
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
