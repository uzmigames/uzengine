# Proposal: phase0_sponza-followup-05-stone-metallic-channel-audit

## Why

With neutral materials (multipliers 1.0, task 02) the metallic debug view showed stone reading as
metallic on **~18 % of the pixels** (`.screenshots/dbg-metallic.png`, `dbg-metallic-main.png`,
Camera A). The graphs wire the metallic input straight from a texture channel, e.g.
`sample/content/materials/sponza/arch_stone_wall_01.uzmat`: `PBROutput.Metallic ← expression 3, pin B`
(with `Roughness ← pin G`). Per `docs/engine/12-rendering/32-graph-material-draw-metadata.md`
"`Metallic` sampled from `B` means an ORM-packed map (sets bit 2 and binds it as the AO texture)".
If the bound texture is a plain roughness map (grey replicated in RGB) rather than a true ORM pack,
its B channel is roughness data and every rough stone pixel becomes partially metallic: F0 lerps
toward the base colour, diffuse energy drops, reflections tint. This is material authoring, not a
renderer defect, and it affects both repositories equally (Camera C parity captures show the same
black wall patches).

## What Changes

An audit script listing, per Sponza material, the texture feeding `Metallic`, its channel and the
channel's mean; then per-material corrections in the `.uzmat` files (constant 0 for stone, plaster,
fabric, wood; keep texture-driven metallic only where the source is a real ORM/metallic map — chains,
lion head, curtain rods if they are). Captures before/after in the metallic view and at Camera A/C.
The archived hand calibration under `sample/reference/` is not touched.

## Impact
- Affected specs: none
- Affected code: `scripts/audit_sponza_metallic.ps1` (new); content: `sample/content/materials/sponza/*.uzmat`
- Breaking change: NO (content only)
- User benefit: stone stops behaving like brushed metal; base colour and reflections match the reference intent
