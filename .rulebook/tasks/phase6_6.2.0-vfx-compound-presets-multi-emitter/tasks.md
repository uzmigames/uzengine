## Phase 6.2.0 — Compound Presets & Multi-Emitter Support

Port the standalone's compound preset system (Fire/Explosion/Magic multi-emitter
scenes) to the Application. Support spawning multiple VfxEmitterComponent entities
from a single compound preset selection, with per-emitter startDelay and cycling.

## 1. Multi-Emitter Infrastructure
- [ ] 1.1 Support multiple emitter entities in vfx_viewport_app (dynamic create/destroy)
- [ ] 1.2 Add compound preset enum: Single, Fire (3 emitters), Explosion (3), Magic (3)
- [ ] 1.3 Ctrl+Tab cycles compound presets (destroys old entities, creates new set)
- [ ] 1.4 Ctrl+1..8 selects active emitter for editor overlay editing

## 2. Compound Preset: Fire
- [ ] 2.1 Flame emitter (center, continuous, warm orange, additive)
- [ ] 2.2 HeatHaze emitter (center, low rate, distortion-like, alpha blend)
- [ ] 2.3 Smoke emitter (center+offset, delayed start, curl noise, alpha blend)

## 3. Compound Preset: Explosion
- [ ] 3.1 Flash/Burst emitter (instant, high emissive, particle light, short life)
- [ ] 3.2 Smoke emitter (delayed 0.1s, rising, curl, large size growth)
- [ ] 3.3 Debris emitter (instant, ground collision bounce, velocity stretch)

## 4. Compound Preset: Magic
- [ ] 4.1 Core Glow emitter (center, sphere spawn, additive, emissive)
- [ ] 4.2 Orbiting emitter (vortex tangential, no gravity, glow)
- [ ] 4.3 Dust emitter (low rate, curl noise, subtle)

## 5. Burst Mode
- [ ] 5.1 Add burstMode and burstPending fields to ParticleEmitter
- [ ] 5.2 Add burstMode to VfxEmitterComponent
- [ ] 5.3 Wire burst in spawn logic (emit burstCount particles in one frame, then stop)
- [ ] 5.4 Insert key toggles burst/continuous mode

## 6. Build & Test
- [ ] 6.1 Build runtime + demo, zero compile errors
- [ ] 6.2 Ctrl+Tab cycles Single → Fire → Explosion → Magic
- [ ] 6.3 Fire compound: 3 emitters visible at same position with staggered timing
- [ ] 6.4 Explosion compound: flash + smoke + debris sequence
- [ ] 6.5 Insert key switches between burst and continuous emission

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
