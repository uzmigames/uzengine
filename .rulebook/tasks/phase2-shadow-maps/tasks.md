## 1. Shadow Atlas

- [ ] 1.1 Create D32_FLOAT DSV texture 2048×2048 (or 4096 for quality=high)
- [ ] 1.2 Divide atlas into 3 horizontal tiles (each 2048 wide × 682 tall)
- [ ] 1.3 Viewport rectangles per cascade stored in per-frame CBV

## 2. Cascade Split Calculation (`src/renderer/shadow_system.cpp`)

- [ ] 2.1 Compute cascade far-plane distances: practical split scheme (blend uniform + log)
- [ ] 2.2 Default splits: 0–20m, 20–80m, 80–300m (configurable via `project.uzproj`)
- [ ] 2.3 Build light-space frustum for each cascade (tight fit = minimal shadow aliasing)
- [ ] 2.4 Stabilize cascade frustums: snap to texel grid (eliminate swimming artifacts)

## 3. Shadow Depth Pass

- [ ] 3.1 Create `shaders/builtin/shadow_depth.hlsl` — VS-only pass (no pixel shader)
- [ ] 3.2 Render each cascade with its own view-proj (3 draw calls per mesh with instancing or geometry shader)
- [ ] 3.3 Depth bias: constant + slope-scaled (`rasterizer_state.DepthBias`, `SlopeScaledDepthBias`)
- [ ] 3.4 Cull back faces for shadow pass (reduces acne)

## 4. Shadow Sampling in Lighting Shader

- [ ] 4.1 Pass cascade split distances to lighting shader via per-frame CBV
- [ ] 4.2 Cascade selection: test view-space depth against cascade split distances
- [ ] 4.3 PCF 3×3: sample with `SampleCmpLevelZero`, 9 samples
- [ ] 4.4 Blend between cascades: lerp at 10% of each cascade range (smooth transition)
- [ ] 4.5 Shadow receiver offset: pushes surface slightly away from shadow map (reduces acne)

## 5. Debug Visualization

- [ ] 5.1 Cascade color overlay: cascade 0=red tint, 1=green, 2=blue (toggle via `renderer.debugShadows` IPC)
- [ ] 5.2 Shadow map preview: render each cascade tile to a 2D overlay in viewport corner

## 6. Testing

- [ ] 6.1 Directional light + ground plane: sharp shadow at cascade 0, visible at cascade 2
- [ ] 6.2 No shadow acne on flat surfaces (check depth bias settings)
- [ ] 6.3 No Peter-Panning (objects don't float above shadow)
- [ ] 6.4 Cascade transition: no hard seam at cascade boundaries
- [ ] 6.5 Performance: 3-cascade shadow pass < 2ms GPU on RTX 3070
