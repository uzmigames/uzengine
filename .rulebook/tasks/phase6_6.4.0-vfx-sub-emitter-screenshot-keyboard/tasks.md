## Phase 6.4.0 — Sub-Emitter, Screenshot, & Keyboard Parity

Port remaining standalone features: CPU sub-emitter on restart, screenshot capture,
background presets, and missing keyboard shortcuts.

## 1. Sub-Emitter (CPU Timer)
- [ ] 1.1 Add subEmitterEnabled, subEmitterTimer, subEmitterDelay fields to VFXViewportApp
- [ ] 1.2 On R/Delete restart: if subEmitter enabled, start countdown timer
- [ ] 1.3 When timer expires, spawn secondary sparks burst at same position
- [ ] 1.4 E key toggles sub-emitter on/off

## 2. Screenshot Capture
- [ ] 2.1 Wire F5 key to Application's screenshot system (RequestScreenshot)
- [ ] 2.2 Save screenshot to .screenshots/ directory with timestamp filename
- [ ] 2.3 Support --screenshot-stdout for headless capture (already works via Application)

## 3. Background Presets
- [ ] 3.1 Add C key to cycle background clear colors (Sky, Dark Gray, Black, White, Neutral)
- [ ] 3.2 Store m_bgPreset index, apply as clear color override in render pass

## 4. Keyboard Shortcuts
- [ ] 4.1 J key: toggle lit particles (flip litMode 0↔1 on active emitter)
- [ ] 4.2 B key: toggle bloom pass
- [ ] 4.3 N key: toggle curl noise (flip curlStrength 0↔saved)
- [ ] 4.4 Insert key: toggle burst/continuous mode
- [ ] 4.5 H key: print all keyboard shortcuts to log
- [ ] 4.6 F1-F8: debug view modes (albedo, normal, depth, etc.)

## 5. VFX Material Cycling
- [ ] 5.1 M key cycles VFX material variants (blend mode + emissive + tint combos)
- [ ] 5.2 Apply globalColorTint from material variant to billboard CB

## 6. Build & Test
- [ ] 6.1 Build runtime + demo, zero compile errors
- [ ] 6.2 F5 saves screenshot to disk
- [ ] 6.3 E enables sub-emitter, R triggers delayed sparks burst
- [ ] 6.4 C cycles background colors
- [ ] 6.5 J toggles lit mode on active preset

## 7. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 7.1 Update or create documentation covering the implementation
- [ ] 7.2 Write tests covering the new behavior
- [ ] 7.3 Run tests and confirm they pass
