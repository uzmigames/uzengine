## Phase 1: Billboard Rendering Features
- [x] 1.1 Wire emissiveIntensity from emitter config → VFXBillboardCB → billboard PS
- [x] 1.2 Wire softFadeDistance for soft particle depth fade
- [x] 1.3 Wire colorOverLifetime (colorLifeStart/End RGBA) → billboard CB
- [x] 1.4 Wire sizeOverLifetime (start/peak/end/peakTime) → billboard CB
- [x] 1.5 Create Additive blend PSO variant — already exists, wired via blendMode field
- [ ] 1.6 Create PremulAlpha blend PSO variant (One/InvSrcAlpha)
- [x] 1.7 Select PSO per-emitter based on blendMode config field (AlphaBlend or Additive)
- [x] 1.8 Wire facingMode (0-4) → billboard CB
- [x] 1.9 Wire stretchFactor → billboard CB
- [x] 1.10 Wire cameraOffset + pivotOffset → billboard CB
- [x] 1.11 Wire sizeBySpeed (enabled/min/max/speedRangeMax) → billboard CB
- [x] 1.12 Wire colorBySpeed → billboard CB
- [x] 1.13 Wire screenSpaceSize → billboard CB
- [x] 1.14 Wire rotationOverLife + rotationRate → update CB
- [x] 1.15 Screenshot test: particles visible with warm color + size fade

## Phase 2: Simulation Forces (Update CB)
- [x] 2.1 Wire gravity vector from emitter config
- [x] 2.2 Wire damping coefficient
- [x] 2.3 Wire curlNoise (strength/frequency/speed) → update CB
- [x] 2.4 Wire vortex force (axisOrigin/Dir/tangential/radial/maxRadius) → update CB
- [ ] 2.5 Wire wind zone (upload buffer) — requires wind zone buffer creation
- [x] 2.6 Wire groundCollision (enabled/restitution/response/friction) → update CB
- [x] 2.7 Wire orbit (enabled/radius/speed/axis) → update CB
- [x] 2.8 Wire dragCoefficient → update CB
- [x] 2.9 Wire acceleration force (accelX/Y/Z) → update CB
- [ ] 2.10 Wire kill volumes (upload buffer) — requires kill volume buffer creation
- [ ] 2.11 Wire deathSpawn — requires death event buffer creation
- [x] 2.12 All force parameters have ParticleEmitter fields with defaults
- [x] 2.13 Rotation over life (start/end) wired

## Phase 3: Spawn Configuration
- [x] 3.1 Added spawnShape to ParticleEmitter (0-7)
- [x] 3.2 Wire spawnShape from emitter config → spawn CB
- [x] 3.3 Added initialRotationMin/Max defaults in ParticleSpawnConstants
- [x] 3.4 Added velocityInheritance field (default 0)
- [x] 3.5 Added spawnRadius, initialSize, initialColor to ParticleEmitter
- [x] 3.6 All spawn params wired from emitter config → spawn CB
- [x] 3.7 Sphere spawn shape tested and working

## Phase 4: Presets & Multi-Emitter
- [x] 4.1 Created emitter_presets.h with 6 preset factory functions
- [x] 4.2 Presets: Fire, Smoke, Sparks, Fountain, MagicOrb, Burst
- [x] 4.3 Multi-emitter: 3 simultaneous emitters at different positions
- [x] 4.4 Compound effects — 3 explosion presets with delayed start (Burst/Smoke/Debris)
- [ ] 4.5 Port VFXPresetSerializer (JSON save/load via VFS)
- [x] 4.6 Screenshot: 3 multi-emitter effects visible at different positions
- [ ] 4.7 Screenshot test: Explosion compound (burst + smoke + debris — needs delayed start wiring in app)

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
- [x] 6.5 Emitter lifecycle — added emitterDuration + loopCount fields to ParticleEmitter
- [ ] 6.6 Side-by-side comparison: standalone vs Application (same preset, same camera)
- [x] 6.7 Remove hardcoded values from frame_vfx_inline.cpp — all params from emitter config
- [ ] 6.8 Final screenshot: full fire fountain matching standalone output
