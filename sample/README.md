# UzEngine Sample Project

Demo scenes showcasing UzEngine features via Lua scripting and `.uzscene` files.

## Running

### Standalone Runtime

Each demo has a `.uzproject` file in `projects/`. Run with `uzengine.exe`:

```bash
# From the UzEngine root directory:
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject

# With editor overlays (grid, stats, frame graph):
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject --editor

# With screenshot capture:
runtime/build/bin/uzengine.exe --project sample/projects/deferred_test.uzproject --screenshot output.png

# With quality preset (0=Low, 1=Medium, 2=High, 3=Ultra, 4=Cinematic):
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject --cvar quality=3

# Available demos:
runtime/build/bin/uzengine.exe --project sample/projects/pbr_showcase.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/damaged_helmet.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/deferred_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/camera_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/mesh_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/ibl_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/hdri_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/cornell_box.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/textured_quad.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/postprocess_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/instancing_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/material_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/fbx_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/vfx_showcase.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/multi_camera_demo.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/cloud_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/bloom_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/decal_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/local_shadows_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/lighting_reflection_test.uzproject
runtime/build/bin/uzengine.exe --project sample/projects/minimal_test.uzproject
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
| F5 | Toggle editor free-cam |
| 0-9 | Debug view modes (deferred_test only) |
| 1/2/3 | Switch cameras (multi_camera_demo) |
| Tab | Cycle debug views |
| Escape | Quit |

### Editor Mode Controls (`--editor`)

| Input | Action |
|-------|--------|
| `--editor` | Enables editor mode (grid, selection, gizmos) |
| Left-click | Select entity (editor mode) |
| W | Translate gizmo (editor mode) |
| E | Rotate gizmo (editor mode) |
| R | Scale gizmo (editor mode) |
| Space | Toggle world/local space (editor mode) |
| Ctrl+Drag | Snap to grid (editor mode) |

## Demos

| Demo | Description | Features |
|------|-------------|----------|
| `pbr_showcase` | Multi-material showcase (cubes + spheres) | Full PBR pipeline, multiple PBR texture sets |
| `damaged_helmet` | DamagedHelmet glTF model with HDRI environment | glTF import, material graph, multi-texture PBR, emissive |
| `deferred_test` | 5 Stonebricks cubes + debug views | PBR textures, GBuffer visualization |
| `camera_test` | Single white cube with FPS camera | Basic rendering, camera control |
| `mesh_test` | Single white cube (same as camera_test) | Mesh loading, transforms |
| `textured_quad` | Checker ground plane from above | Texture loading, UV mapping |
| `postprocess_test` | 3 white cubes + sun | Tone mapping, FXAA, bloom |
| `ibl_test` | 5 Stonebricks cubes + IBL | Image-based lighting |
| `hdri_test` | HDRI panorama + 7 PBR spheres | EXR loading, IBL reflections, varying metallic/roughness |
| `instancing_test` | 1000 cubes in 10x10x10 grid (Lua-spawned) | GPU instancing, InstanceBatcher |
| `material_test` | 5 spheres with different blend modes | Emissive, masked, translucent, additive, opaque |
| `cornell_box` | Classic GI test box (red/green walls + sphere) | Procedural mesh, solid materials, atmosphere, fog |
| `fbx_test` | Mushroom monster character on ground plane | FBX mesh import, ORM textures, reflection probe |
| `vfx_showcase` | GPU particle effects showcase | VFX compute pipeline, collision, trails, presets |
| `multi_camera_demo` | 3 cameras cycling every 2s (Front/Top/Side) | CameraManager, push/pop stack, hotkeys 1/2/3 |
| `cloud_test` | Volumetric clouds test scene | Cloud ray marching, detail noise, aerial perspective |
| `bloom_test` | Bloom post-process test | Bloom 6-mip pyramid, smooth knee, HDR composite |
| `decal_test` | Deferred decal projection | DBuffer decals, opacity mask, material-based |
| `local_shadows_test` | Point/spot light shadow testing | Local light shadows, shadow atlas |
| `lighting_reflection_test` | Lighting and reflection probe scene | IBL, reflection probes, multiple light types |
| `minimal_test` | Minimal empty scene | Baseline rendering, startup validation |

## Structure

```
sample/
├── projects/                    # 22 .uzproject files (one per demo)
│   ├── pbr_showcase.uzproject
│   ├── damaged_helmet.uzproject
│   ├── vfx_showcase.uzproject
│   ├── multi_camera_demo.uzproject
│   ├── cloud_test.uzproject
│   ├── bloom_test.uzproject
│   ├── decal_test.uzproject
│   ├── local_shadows_test.uzproject
│   ├── lighting_reflection_test.uzproject
│   ├── minimal_test.uzproject
│   └── ... (camera_test, mesh_test, deferred_test, ibl_test, etc.)
└── content/
    ├── scenes/                  # 22 .uzscene files (one per demo)
    ├── scripts/
    │   ├── fly_camera.lua               # Shared fly camera controller
    │   ├── cornell_box_setup.lua
    │   ├── cube_rotator.lua
    │   ├── decal_setup.lua
    │   ├── hdri_setup.lua
    │   ├── instancing_grid_setup.lua
    │   ├── load_hdri.lua                # HDRI environment loader
    │   ├── multi_camera_demo.lua        # 3-camera cycling demo
    │   └── test_component_parity.lua
    ├── materials/
    │   ├── damaged_helmet.uzmat         # glTF material graph (5 textures)
    │   ├── stone_floor.uzmaterial
    │   ├── marble.uzmaterial
    │   └── metal_rust.uzmaterial
    ├── meshes/
    │   ├── cube.uzmesh                  # Binary .uzmesh (UZMH format)
    │   ├── sphere.uzmesh
    │   ├── DamagedHelmet.uzmesh         # glTF-imported mesh
    │   └── MushroomMonster_Low.uzmesh   # FBX-imported mesh (auto-LOD)
    └── textures/
        ├── Stonebricks/                 # PBR texture set
        ├── Concrete/                    # PBR texture set
        ├── MetalCorroded/               # PBR texture set
        ├── MushroomMonster/             # FBX character textures
        ├── decals/                      # Decal textures
        ├── hdri/                        # HDRI panorama images (EXR)
        ├── Default_*.jpg                # Default PBR fallback textures
        └── checker.png                  # Debug checker pattern
```

## File Formats

| Extension | Type | Format |
|-----------|------|--------|
| `.uzproject` | Project config | JSON — window, render, default scene |
| `.uzscene` | Scene definition | JSON — entities, components, hierarchy |
| `.uzmesh` | Mesh data | Binary — UZMH v2 header + vertex/index/LOD data |
| `.uzmat` | Material graph | JSON — node-based material (PBR textures, channel mapping) |
| `.uzmaterial` | Simple material | JSON — fixed-function PBR parameters |
| `.uztexture` | Cooked texture | Binary — pre-processed texture with mips |
| `.lua` | Script | Lua — `on_start`/`on_update`/`on_stop` lifecycle |

## C++ Demo Template (RHI-level)

One C++ demo remains as a template for prototyping new features in C++ before
migrating to the sample format:

```bash
cd runtime/demos/triangle && zig build && zig-out/bin/triangle.exe
```

### VSCode Extension

To run demos inside the VSCode extension:

1. Build the VSCode extension: `scripts/build.ps1`
2. Press F5 to launch Extension Development Host
3. Command palette: `uzengine.openScene` → select `sample/projects/pbr_showcase.uzproject`
4. Viewport panel opens with native HWND rendering (DX12 swapchain)

## Building

For standalone runtime:

```bash
cd runtime && zig build -Ddll=true    # Build the runtime (includes uzengine.exe)
```

For full VSCode extension:

```bash
scripts/build.ps1                      # Builds runtime + N-API addon + extension
```

The `uzengine.exe` binary is output to `runtime/build/bin/`.
The `.node` addon and DLLs are deployed to `extension/native/`.
