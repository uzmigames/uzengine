# UzEngine Sample Project

A demo project showcasing all UzEngine file formats and a typical content directory structure.

## Structure

```
sample/
├── demo.uzproject                      # Project configuration
└── content/
    ├── scenes/
    │   └── demo.uzscene                # Main scene (entities, hierarchy, lights, camera)
    ├── materials/
    │   ├── stone_floor.uzmaterial      # PBR stone with tiled textures
    │   ├── metal_rust.uzmaterial       # PBR rusty metal
    │   └── marble.uzmaterial           # PBR polished marble
    ├── meshes/
    │   ├── cube.uzmesh                 # Unit cube (JSON header → cube.bin)
    │   └── sphere.uzmesh              # Unit sphere (JSON header → sphere.bin)
    ├── prefabs/
    │   └── street_lamp.uzprefab        # Reusable lamp with child point light
    └── textures/                       # PBR texture maps (albedo, normal, roughness, AO, metallic)
```

## File Formats

| Extension | Type | Format |
|-----------|------|--------|
| `.uzproject` | Project config | JSON |
| `.uzscene` | Scene definition | JSON — entities, components, hierarchy |
| `.uzmaterial` | Material definition | JSON — PBR properties, texture references, tiling |
| `.uzmesh` | Mesh header | JSON — vertex layout, bounds, submeshes → binary `.bin` |
| `.uzprefab` | Reusable entity template | JSON — same structure as scene entities |

## Scene Overview

The demo scene contains:

- **Sun** — directional light with 4-cascade shadows
- **SkyLight** — ambient sky illumination
- **Floor** — scaled cube with tiled stone material
- **Hero** — sphere with rusty metal material, camera attached as child
- **Pillars** — two marble columns with warm/cool point lights
- **StreetLamp** — prefab instance

All formats are JSON — human-readable, diffable, and LLM-friendly.
