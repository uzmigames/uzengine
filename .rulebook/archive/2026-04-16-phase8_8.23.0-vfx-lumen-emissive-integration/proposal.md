# Proposal: phase8_8.23.0-vfx-lumen-emissive-integration

## Why
Emissive particles (fire, lightning, magic) currently don't contribute to scene GI. Niagara emits particle emissive into Lumen's radiance cache so they light surrounding geometry. UzEngine uses DDGI, so the analogue is injecting particle emissive into DDGI probe updates.

## What Changes
Extend the DDGI probe-update pipeline to sample the most-emissive particles (reuses the 4.13 top-N light readback). Particle emissive contribution is integrated into the probe's irradiance result on the next update cycle.

## Impact
- Affected specs: docs/engine/25-vfx/
- Affected code: runtime/engine/renderer/lighting/ddgi + vfx readback path
- Breaking change: NO
- User benefit: Bright VFX events (explosions, muzzle flashes) light the scene.
- Estimated effort: L

## Source
UE5 FNiagaraLumenCommitter + FLumenSceneRadianceCache.
