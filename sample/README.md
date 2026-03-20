# UzEngine Sample Project

Demo scenes showcasing UzEngine features via Lua scripting and `.uzscene` files.

## Running

Each demo has a `.uzproject` file. Run with `uzengine.exe`:

```bash
# From the UzEngine root directory:
runtime/build/bin/uzengine.exe --project sample/camera_test.uzproject

# With screenshot capture (auto-triggers after scene fully loads):
runtime/build/bin/uzengine.exe --project sample/deferred_test.uzproject --screenshot output.png

# Available demos:
runtime/build/bin/uzengine.exe --project sample/camera_test.uzproject
runtime/build/bin/uzengine.exe --project sample/mesh_test.uzproject
runtime/build/bin/uzengine.exe --project sample/deferred_test.uzproject
runtime/build/bin/uzengine.exe --project sample/ibl_test.uzproject
runtime/build/bin/uzengine.exe --project sample/cornell_box.uzproject
runtime/build/bin/uzengine.exe --project sample/pbr_showcase.uzproject
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
| `deferred_test` | 5 Stonebricks cubes + debug views | PBR textures, GBuffer visualization |
| `ibl_test` | 5 Stonebricks cubes + IBL | Image-based lighting |
| `cornell_box` | Classic GI test box (red/green walls + sphere) | Procedural mesh, solid materials, atmosphere, fog |
| `pbr_showcase` | Multi-material showcase | Full PBR pipeline, multiple materials |

## Structure

```
sample/
├── camera_test.uzproject
├── mesh_test.uzproject
├── deferred_test.uzproject
├── ibl_test.uzproject
├── cornell_box.uzproject
├── pbr_showcase.uzproject
└── content/
    ├── scenes/
    │   ├── camera_test.uzscene
    │   ├── mesh_test.uzscene
    │   ├── deferred_test.uzscene
    │   ├── ibl_test.uzscene
    │   ├── cornell_box.uzscene
    │   ├── pbr_showcase.uzscene
    │   └── demo.uzscene
    ├── scripts/
    │   ├── camera_test_controller.lua
    │   ├── deferred_test_controller.lua
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
        └── MetalCorroded/       # PBR texture set
```

## File Formats

| Extension | Type | Format |
|-----------|------|--------|
| `.uzproject` | Project config | JSON — window, render, default scene |
| `.uzscene` | Scene definition | JSON — entities, components, hierarchy |
| `.uzmesh` | Mesh data | Binary — UZMH header + vertex/index data |
| `.lua` | Script | Lua — `on_start`/`on_update`/`on_stop` lifecycle |

## Building

```bash
cd runtime && zig build    # Build the runtime (includes uzengine.exe)
```

The `uzengine.exe` binary is output to `runtime/build/bin/`.
