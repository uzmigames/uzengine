# Changelog — sample

## [0.3.0] — 2026-03-24

### Changed
- Moved all `.uzproject` files to `sample/projects/` for cleaner directory structure
- Updated `content_root` in all projects from `"content/"` to `"../content/"`
- PBR Showcase: increased cube scale from 0.8 to 1.5 for better debug visibility
- PBR Showcase: increased sphere scale from 1.0 to 1.8 and pushed back for better layout
- PBR Showcase: spread cubes wider (-6 to +6 instead of -4 to +4)
- PBR Showcase: pulled camera back (Y=-12, Z=5) to frame the larger scene

### Added
- PBR Showcase: volumetric clouds entity (coverage 0.35, density 0.6, 7km base altitude)
- Updated `README.md` with new `sample/projects/` paths and `--editor` flag documentation

## [0.2.0] — 2026-03-10

### Added
- Sample project structure (`demo.uzproject`)
- Demo scene with directional light, sky light, floor, hero, pillars, point lights (`demo.uzscene`)
- PBR materials: stone floor, rusty metal, marble (`.uzmaterial`)
- Primitive meshes: cube, sphere (`.uzmesh` JSON headers)
- Street lamp prefab with child point light (`.uzprefab`)
- PBR texture set (albedo, normal, roughness, AO, metallic)
