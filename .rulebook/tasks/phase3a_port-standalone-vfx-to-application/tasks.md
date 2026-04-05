## Phase 1: Billboard Rendering Features
- [ ] 1.1 Wire emissiveIntensity from emitter config → VFXBillboardCB → billboard PS
- [ ] 1.2 Wire softFadeDistance for soft particle depth fade
- [ ] 1.3 Wire colorOverLifetime (colorLifeStart/End RGBA) → billboard CB
- [ ] 1.4 Wire sizeOverLifetime (start/peak/end/peakTime) → billboard CB
- [ ] 1.5 Create Additive blend PSO variant (One/One) alongside existing AlphaBlend
- [ ] 1.6 Create PremulAlpha blend PSO variant (One/InvSrcAlpha)
- [ ] 1.7 Select PSO per-emitter based on blendMode config field
- [ ] 1.8 Wire facingMode (0-4) → billboard CB for camera/velocity/plane/custom/position modes
- [ ] 1.9 Wire stretchFactor → billboard CB for velocity-aligned stretching
- [ ] 1.10 Wire cameraOffset + pivotOffset → billboard CB
- [ ] 1.11 Wire sizeBySpeed (enabled/min/max/speedRangeMax) → billboard CB
- [ ] 1.12 Wire colorBySpeed (enabled flag, reuses colorLifeStart/End) → billboard CB
- [ ] 1.13 Wire screenSpaceSize (enabled + pixelSize) → billboard CB
- [ ] 1.14 Wire rotationOverLife (start/end rates) + rotationRate → update CB → billboard
- [ ] 1.15 Screenshot test: emissive glow particle at origin (compare with standalone)

## Phase 2: Simulation Forces (Update CB)
- [ ] 2.1 Wire gravity vector from emitter config (already done, verify)
- [ ] 2.2 Wire damping coefficient (already done, verify)
- [ ] 2.3 Wire curlNoise (strength/frequency/speed) → update CB
- [ ] 2.4 Wire vortex force (axisOrigin/Dir/tangential/radial/maxRadius) → update CB
- [ ] 2.5 Wire wind zone (direction/strength) → update CB + upload wind zone buffer
- [ ] 2.6 Wire groundCollision (enabled/restitution/response/friction) → update CB
- [ ] 2.7 Wire orbit (enabled/radius/speed/axis) → update CB
- [ ] 2.8 Wire dragCoefficient → update CB
- [ ] 2.9 Wire acceleration force (accelX/Y/Z) → update CB
- [ ] 2.10 Wire kill volumes (upload buffer + activeCount) → update CB
- [ ] 2.11 Wire deathSpawn (enabled/count/spread/speedScale) → update CB + death event buffers
- [ ] 2.12 Screenshot test: curl noise smoke effect
- [ ] 2.13 Screenshot test: ground collision bounce

## Phase 3: Spawn Configuration
- [ ] 3.1 Add spawnShape/spawnOnSurface/shapeParam1-3 to VfxEmitterComponent or ParticleEmitter
- [ ] 3.2 Wire all 8 spawn shapes from emitter config → spawn CB
- [ ] 3.3 Wire initialRotationMin/Max → spawn CB
- [ ] 3.4 Wire velocityInheritance + emitter velocity tracking → spawn CB
- [ ] 3.5 Add spawnRadius, initialSize, initialColor to ParticleEmitter/VfxEmitterComponent
- [ ] 3.6 Screenshot test: Sphere spawn shape (radius=200cm)
- [ ] 3.7 Screenshot test: Ring spawn shape

## Phase 4: Presets & Multi-Emitter
- [ ] 4.1 Port VFXEmitterConfig presets from standalone (fire, explosion, smoke, sparks, magic, etc.)
- [ ] 4.2 Create preset registry accessible from VfxEmitterComponent (string name → config)
- [ ] 4.3 Support multiple VfxEmitterComponent entities simultaneously
- [ ] 4.4 Implement compound effects (multiple emitters with delayed start timers)
- [ ] 4.5 Port VFXPresetSerializer (JSON save/load) to engine VFS path
- [ ] 4.6 Screenshot test: Fire compound (core flame + sparks)
- [ ] 4.7 Screenshot test: Explosion compound (burst + smoke + debris)

## Phase 5: Editor UI
- [ ] 5.1 Port clickable menu bar (TextRenderer-based) to Application viewport
- [ ] 5.2 Port parameter panel (sliders for emitter config fields)
- [ ] 5.3 Port preset browser/selector dropdown
- [ ] 5.4 Port module stack toggle UI (enable/disable forces)
- [ ] 5.5 Port color ramp LUT editor (256x1 texture upload)
- [ ] 5.6 Wire F10 parameter panel toggle
- [ ] 5.7 Screenshot test: UI visible with preset selector

## Phase 6: Polish & Parity
- [ ] 6.1 Lit particle mode (NdotL directional + ambient)
- [ ] 6.2 Particle light emission (point light at emitter position)
- [ ] 6.3 Particle light flicker (sinusoidal modulation)
- [ ] 6.4 Color ramp LUT integration (256x1 RGBA16F texture)
- [ ] 6.5 Emitter lifecycle (duration, loop count)
- [ ] 6.6 Side-by-side comparison: standalone vs Application (same preset, same camera)
- [ ] 6.7 Remove remaining hardcoded values from frame_vfx_inline.cpp
- [ ] 6.8 Final screenshot: full fire fountain matching standalone output
