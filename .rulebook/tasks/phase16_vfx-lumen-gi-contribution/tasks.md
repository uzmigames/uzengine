## 1. Implementation
- [ ] 1.1 Audit current DDGI pipeline and document where emissive light sources feed the probe update (files + entry points)
- [ ] 1.2 Design `VfxVirtualEmissiveLight` struct (position, radius, color, intensity) matching the DDGI virtual-light buffer layout
- [ ] 1.3 Add a GPU reduction pass that aggregates each emitter's emissive contribution into a single virtual light (position = bounds center, intensity = emissive × alive count × lifetime scale)
- [ ] 1.4 Publish the aggregated buffer to the DDGI probe update shader via an existing SRV slot
- [ ] 1.5 Update the probe-update CS to sample VFX virtual lights alongside real lights
- [ ] 1.6 Add CVar `r.VFX.DDGIInjection` (default 1) + `r.VFX.DDGIInjection.MaxEmitters` (default 64)
- [ ] 1.7 Early-out path: when the CVar is disabled or all emitters have zero alive particles, bypass the injection dispatch entirely
- [ ] 1.8 Capture Sponza fireball screenshot with and without injection — measure bounce light on walls
- [ ] 1.9 Validate the virtual-light approximation against a Falcor reference render; if PSNR fails the 35 dB bar, implement the probe cubemap rasterization path inside this same task

## 2. Tail (mandatory — enforced by rulebook v5.3.0)
- [ ] 2.1 Update or create documentation covering the implementation
- [ ] 2.2 Write tests covering the new behavior
- [ ] 2.3 Run tests and confirm they pass
