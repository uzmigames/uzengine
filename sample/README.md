# UzEngine Sample Project

Demo scenes showcasing UzEngine features via Lua scripting and `.uzscene` files.

## Running

Each demo has a `.uzproject` file in `projects/`. Run with `uzengine.exe`:

```bash
# From the UzEngine root directory:
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject

# With editor overlays (grid, stats, frame graph):
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor

# With screenshot capture:
runtime/build/bin/uzengine.exe --project sample/projects/deferred_test.uzproject --screenshot output.png

# Available demos:
runtime/build/bin/uzengine.exe --project sample/projects/camera_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/mesh_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/deferred_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/ibl_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/hdri_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/cornell_box.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/textured_quad.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/postprocess_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/instancing_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/material_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/fbx_test.uzproject
```

## Controls (all demos)

| Key | Action |
|-----|--------|
| WASD | Move camera (XY plane) |
| E/Q | Move up/down (Z axis) |
| Right-click + Mouse | Look around |
| Scroll wheel | Adjust camera speed |
| F1 | Deferred render path |
| F2 | Forward render path |
| 0-9 | Debug view modes (deferred_test only) |
| Tab | Cycle debug views |
| Escape | Quit |

## Demos

| Demo | Description | Features |
|------|-------------|----------|
| `camera_test` | Single white cube with FPS camera | Basic rendering, camera control |
| `mesh_test` | Single white cube (same as camera_test) | Mesh loading, transforms |
| `textured_quad` | Checker ground plane from above | Texture loading, UV mapping |
| `postprocess_test` | 3 white cubes + sun | Tone mapping, FXAA, bloom |
| `deferred_test` | 5 Stonebricks cubes + debug views | PBR textures, GBuffer visualization |
| `ibl_test` | 5 Stonebricks cubes + IBL | Image-based lighting |
| `hdri_test` | HDRI panorama + 7 PBR spheres | EXR loading, IBL reflections, varying metallic/roughness |
| `instancing_test` | 1000 cubes in 10x10x10 grid (Lua-spawned) | GPU instancing, InstanceBatcher |
| `material_test` | 5 spheres with different blend modes | Emissive, masked, translucent, additive, opaque |
| `cornell_box` | Classic GI test box (red/green walls + sphere) | Procedural mesh, solid materials, atmosphere, fog |
| `fbx_test` | Mushroom monster character on ground plane | FBX mesh import, ORM textures, reflection probe |
| `pbr_showcase` | Multi-material showcase (cubes + spheres) | Full PBR pipeline, multiple PBR texture sets |

## Structure

```
sample/
├── projects/
│   ├── camera_test.uzproject
│   ├── mesh_test.uzproject
│   ├── textured_quad.uzproject
│   ├── postprocess_test.uzproject
│   ├── deferred_test.uzproject
│   ├── ibl_test.uzproject
│   ├── hdri_test.uzproject
│   ├── instancing_test.uzproject
│   ├── material_test.uzproject
│   ├── cornell_box.uzproject
│   ├── fbx_test.uzproject
│   ├── pbr_showcase.uzproject
│   └── demo.uzproject
└── content/
    ├── scenes/
    │   ├── camera_test.uzscene
    │   ├── mesh_test.uzscene
    │   ├── textured_quad.uzscene
    │   ├── postprocess_test.uzscene
    │   ├── deferred_test.uzscene
    │   ├── ibl_test.uzscene
    │   ├── hdri_test.uzscene
    │   ├── instancing_test.uzscene
    │   ├── material_test.uzscene
    │   ├── cornell_box.uzscene
    │   ├── pbr_showcase.uzscene
    │   └── demo.uzscene
    ├── scripts/
    │   ├── camera_test_controller.lua
    │   ├── deferred_test_controller.lua
    │   ├── hdri_setup.lua
    │   ├── instancing_grid_setup.lua
    │   ├── cornell_box_setup.lua
    │   ├── pbr_showcase_controller.lua
    │   ├── pbr_controller.lua
    │   └── cube_rotator.lua
    ├── meshes/
    │   ├── cube.uzmesh          # Binary .uzmesh (UZMH format)
    │   └── sphere.uzmesh        # Binary .uzmesh (UZMH format)
    └── textures/
        ├── Stonebricks/         # PBR texture set
        ├── Concrete/            # PBR texture set
        ├── MetalCorroded/       # PBR texture set
        ├── MushroomMonster/     # FBX character textures
        ├── hdri/                # HDRI panorama images (EXR)
        └── checker.png          # Debug checker pattern
```

## File Formats

| Extension | Type | Format |
|-----------|------|--------|
| `.uzproject` | Project config | JSON — window, render, default scene |
| `.uzscene` | Scene definition | JSON — entities, components, hierarchy |
| `.uzmesh` | Mesh data | Binary — UZMH header + vertex/index data |
| `.lua` | Script | Lua — `on_start`/`on_update`/`on_stop` lifecycle |

## C++ Demo Template (RHI-level)

One C++ demo remains as a template for prototyping new features in C++ before
migrating to the sample format:

```bash
cd runtime/demos/triangle && zig build && zig-out/bin/triangle.exe
```

## Building

```bash
cd runtime && zig build    # Build the runtime (includes uzengine.exe)
```

The `uzengine.exe` binary is output to `runtime/build/bin/`.
