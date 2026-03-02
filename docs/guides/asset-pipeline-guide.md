# Asset Pipeline Guide — UzEngine

Import, configure, and manage game assets (meshes, textures, audio) efficiently. UzEngine uses a **project-relative import system** with automated caching and optional offline compilation.

> **Status**: Asset import system begins in Phase 2. Basic file layout exists from Phase 1.

---

## Overview

### Key Concepts

| Concept | Meaning |
|---------|---------|
| **res:// path** | Project-relative path (e.g., `res://assets/player.glb`) |
| **.import/** | Cached import metadata and converted assets |
| **.uzpak** | Packed asset archive (for distribution) |
| **UzBuild.exe** | Offline asset compiler (Phase 2+) |
| **Hot-reload** | FileWatcher detects asset changes → auto-reimport |

### Directory Structure

```
project/
├── project.uzproj           # Project config
├── assets/                  # Source assets (raw files)
│   ├── meshes/
│   │   ├── player.glb
│   │   ├── enemy.fbx
│   │   └── terrain.obj
│   ├── textures/
│   │   ├── player_diffuse.png
│   │   ├── player_normal.png
│   │   └── metal_rough.hdr
│   ├── audio/
│   │   ├── footstep.wav
│   │   ├── explosion.ogg
│   │   └── music.mp3
│   └── .import/             # Generated metadata + cache
│       ├── player.glb.import.json
│       ├── player_diffuse.png.import.json
│       └── explosion.ogg.import.json
├── materials/
│   └── player_skin.uzmat
├── shaders/
│   └── pbr.hlsl
└── scenes/
    └── main.uzscene
```

---

## Supported File Formats

### Mesh Formats

| Format | Extension | Notes | Phase |
|--------|-----------|-------|-------|
| **glTF 2.0** | `.glb`, `.gltf` | Recommended; embeds textures, materials, LODs | 2+ |
| **FBX** | `.fbx` | Autodesk format; animations, rigging | 2+ |
| **OBJ** | `.obj` | Simple static meshes | 2+ |
| **Wavefront** | `.mesh` | Custom binary format (future) | 3+ |

**Recommendation:** Use **glTF 2.0** (.glb) for new projects. It's the modern standard and plays nicely with AI workflows.

### Texture Formats

| Format | Extension | Best For | Notes |
|--------|-----------|----------|-------|
| **PNG** | `.png` | LDR (color, normal maps) | Lossless, no alpha issues |
| **JPEG** | `.jpg`, `.jpeg` | Photos, detailed LDR | Lossy; avoid alpha channel |
| **TGA** | `.tga` | Legacy; alpha channel | Uncompressed; large files |
| **HDR** | `.hdr` | IBL, environment maps | High dynamic range; floating-point |
| **Basis** | `.basis` | GPU-compressed (future) | Fast decode; multiformat |

**Best practices:**
- **Diffuse color** → PNG
- **Normal map** → PNG (stores XYZ in RGB)
- **Metallic/Roughness** → PNG (separate channels)
- **Emissive** → PNG (if needed)
- **Environment** → HDR (for IBL)

### Audio Formats

| Format | Extension | Codec | Notes |
|--------|-----------|-------|-------|
| **WAV** | `.wav` | PCM | Uncompressed; good for SFX |
| **MP3** | `.mp3` | MPEG Audio | Compressed; streaming |
| **OGG** | `.ogg` | Vorbis | High quality; compressed |
| **FLAC** | `.flac` | Lossless | Large; rarely used in games |

**Rules:**
- **SFX** < 2 MB → embed in `.uzpak`
- **SFX** >= 2 MB → stream from disk (Phase 3+)
- **Music** → always MP3 or OGG (usually streamed)

---

## Auto-Import System

### How It Works

1. **You save** a file in `assets/` folder
2. **FileWatcher** detects the change (VSCode integrated)
3. **Import dialog** appears with default settings
4. **You confirm** or adjust settings
5. **Engine imports** → generates `.import.json` + cached formats
6. **Asset ready** to use in scenes via `res://assets/...`

### Import Metadata (.import.json)

```json
{
  "version": 1,
  "source": "player.glb",
  "import_settings": {
    "format": "glb",
    "scale": 1.0,
    "flip_uv_x": false,
    "flip_uv_y": true,
    "materials": "embed",
    "animations": true,
    "import_skeletal_mesh": true
  },
  "generated_assets": [
    "player.uzmesh",
    "player_materials.uzmat"
  ],
  "lod_info": {
    "enabled": true,
    "levels": 4,
    "screen_coverage": [100, 50, 25, 10]
  }
}
```

---

## Texture Compression

UzEngine automatically compresses textures for GPU efficiency. Different formats for different use cases:

| Compression | Format | Use Case | Ratio |
|-------------|--------|----------|-------|
| **BC7** | `DXC_BC7` | Color, diffuse maps | 6:1 |
| **BC5** | `DXC_BC5` | Normal maps (XY stored) | 2:1 |
| **BC4** | `DXC_BC4` | Grayscale (height, AO) | 2:1 |
| **BC6H** | `DXC_BC6H` | HDR environment maps | 6:1 |
| **UNCOMPRESSED** | `R8G8B8A8` | (editor only; slow) | 1:1 |

### Specifying Compression

In `.import.json`:
```json
{
  "import_settings": {
    "texture_format": "bc7"  # or "bc5", "bc4", "bc6h"
  }
}
```

### Decompression at Runtime

When loading textures, the engine:
1. **Reads compressed .dds** (GPU-native format)
2. **Uploads to GPU** (no CPU decompression)
3. **Shader reads** directly

Compression is automatic; you don't need to think about it.

---

## Mesh LODs (Level of Detail)

Automatic LOD generation reduces geometry for distant objects, improving performance.

### How LODs Are Generated

**meshoptimizer** automatically creates 4 LOD levels:

```
Original mesh: 50,000 triangles
  ↓ meshoptimizer
LOD0: 100%  (50,000 tris)  — draw when screen coverage > 50%
LOD1: 50%   (25,000 tris)  — draw when coverage 25-50%
LOD2: 25%   (12,500 tris)  — draw when coverage 10-25%
LOD3: 10%   (5,000 tris)   — draw when coverage < 10%
```

### Screen Coverage

**Screen coverage** = object size as percentage of screen height.

- **100%** = object fills entire screen
- **50%** = object half screen height
- **25%** = object quarter screen
- **10%** = object very small
- **< 5%** = too small to see; culled

### Configuring LODs

In `.import.json`:
```json
{
  "lod_info": {
    "enabled": true,
    "levels": 4,
    "screen_coverage": [100, 50, 25, 10]
  }
}
```

Or in .uzscene:
```ini
[node id=1 type=StaticMesh name="Building" parent=0]
mesh = "res://assets/building.uzmesh"
lod_bias = 0.5  ; Use higher LOD (2x cost); -0.5 = use lower LOD
```

---

## Using Assets in Scenes

### Mesh and Material References

In `.uzscene` files, reference assets by their `res://` path:

```ini
[node id=1 type=StaticMesh name="Player" parent=0]
mesh = "res://assets/player.uzmesh"
material = "res://materials/player_skin.uzmat"
cast_shadow = true
receive_shadow = true
```

### Skeletal Meshes (Phase 3)

For animated characters:

```ini
[node id=2 type=SkeletalMesh name="Enemy" parent=0]
mesh = "res://assets/goblin.uzmesh"
material = "res://materials/goblin_skin.uzmat"
skeleton = "res://assets/goblin.uzskeleton"
```

Then play animations in Lua:
```lua
ctx.skeletal:play_animation(self.node_ref, "walk", {
    looping = true,
    speed = 1.0
})
```

### Dynamic Material Instances

Create custom materials at runtime:

```lua
function M:on_start(ctx)
    -- Create an instance of the base material
    local mat = ctx.assets:create_material_instance("res://materials/base.uzmat")

    -- Override parameters
    mat:set_color("albedo", {1, 0, 0, 1})  -- Red
    mat:set_float("roughness", 0.5)

    -- Apply to node
    ctx.scene:set_property(self.node_ref, "material", mat)
end
```

---

## Using Assets in Lua

### Load and Use Meshes

```lua
function M:spawn_prop(ctx, prop_name, position)
    local mesh = ctx.assets:load_mesh("res://assets/" .. prop_name .. ".uzmesh")

    local prop = ctx.scene:add_node({
        type = "StaticMesh",
        name = prop_name,
        mesh = mesh,
        position = position
    })

    return prop
end
```

### Load and Use Materials

```lua
function M:on_start(ctx)
    local mat = ctx.assets:load_material("res://materials/player_skin.uzmat")
    ctx.scene:set_property(self.node_ref, "material", mat)
end
```

### Load Audio Files

```lua
function M:play_jump_sound(ctx)
    local sound = ctx.assets:load_audio("res://assets/sounds/jump.wav")
    ctx.audio:play_2d(sound, {volume = 0.8})
end
```

---

## Asset Cooking (UzBuild.exe)

**Asset cooking** is the offline compilation process that prepares your game for distribution.

### What UzBuild Does

1. **Imports** all assets in `assets/`
2. **Compiles** shaders (DXC)
3. **Generates** LODs (meshoptimizer)
4. **Compresses** textures (BC7/BC5/BC4/BC6H)
5. **Encodes** audio (MP3/Vorbis)
6. **Packs** everything into `.uzpak` archive

### Running UzBuild

**Manual build (development):**
```bash
UzBuild.exe --project E:\UzEngine\myproject --output dist/
```

**Automatic (Phase 2+):**
- FileWatcher detects changes in `assets/`
- UzBuild auto-runs in background
- Updated `.uzpak` ready instantly

### Output Structure

```
dist/
├── UzEngine.exe
├── game.dll
├── game.uzpak          ← All imported assets, compressed
├── turbojpeg.dll
└── (other runtime DLLs)
```

The `.uzpak` file is your game's asset archive. It includes:
- All mesh LODs
- All compressed textures
- All audio files
- All material definitions
- Metadata for streaming decisions

---

## Import Settings Reference

Complete `.import.json` settings for different asset types:

### Mesh Import Settings

```json
{
  "source": "player.glb",
  "import_settings": {
    "scale": 1.0,
    "flip_uv_x": false,
    "flip_uv_y": true,
    "materials": "embed",        # or "ignore", "reference"
    "animations": true,
    "import_skeletal_mesh": true,
    "split_non_manifold": false,
    "optimize": true
  },
  "lod_info": {
    "enabled": true,
    "levels": 4,
    "screen_coverage": [100, 50, 25, 10]
  }
}
```

### Texture Import Settings

```json
{
  "source": "diffuse.png",
  "import_settings": {
    "compress": "bc7",           # or "bc5", "bc4", "bc6h"
    "mipmap": true,
    "filter": "lanczos",         # or "box", "cubic"
    "srgb": true,                # true for color, false for normals/data
    "flip_y": false
  }
}
```

### Audio Import Settings

```json
{
  "source": "music.wav",
  "import_settings": {
    "codec": "mp3",              # or "vorbis"
    "bitrate_kbps": 192,
    "streaming": false,          # true if >= 2MB
    "loops": false,
    "mono": false
  }
}
```

---

## Common Import Patterns

### Importing a Character

1. **Prepare in Blender:**
   - Export as `.glb` with Armature, Animations
   - Check scale (1 unit = 1 meter)
   - Ensure UV unwrapped

2. **Save to project:**
   ```
   assets/characters/player.glb
   assets/textures/player_diffuse.png
   assets/textures/player_normal.png
   ```

3. **Import triggers automatically:**
   - Mesh LODs generated
   - Textures compressed to BC7 (diffuse) and BC5 (normal)
   - `.import.json` created

4. **Use in scene:**
   ```ini
   [node id=1 type=SkeletalMesh name="Player" parent=0]
   mesh = "res://assets/characters/player.uzmesh"
   material = "res://materials/player_skin.uzmat"
   ```

### Importing Terrain

1. **Create heightmap:**
   - PNG, grayscale, power-of-2 resolution
   - Each pixel = height sample

2. **Create normal map** (or let engine generate):
   - Export as `terrain_normal.png`

3. **Save:**
   ```
   assets/terrain/heightmap.png
   assets/terrain/grass_diffuse.png
   assets/terrain/grass_normal.png
   ```

4. **Import settings:**
   ```json
   {
     "import_settings": {
       "type": "heightfield",
       "heightmap": "terrain/heightmap.png",
       "scale_height": 100
     }
   }
   ```

5. **Use in scene:**
   ```ini
   [node id=1 type=StaticMesh name="Terrain" parent=0]
   mesh = "res://assets/terrain/heightmap.uzmesh"
   material = "res://materials/grass.uzmat"
   ```

### Importing Environment (IBL)

1. **Get HDR environment map:**
   - `.hdr` file (e.g., from Poly Haven)

2. **Save:**
   ```
   assets/environments/forest.hdr
   ```

3. **Engine generates:**
   - Irradiance map
   - Specular cubemap (BC6H)

4. **Use in project settings:**
   ```toml
   [rendering]
   ibl_environment = "res://assets/environments/forest.hdr"
   ```

---

## Hot-Reload in Editor

When you save a file, the editor automatically:

1. **Detects change** (FileWatcher)
2. **Re-imports** the asset
3. **Regenerates** LODs/compression
4. **Reloads** in running engine (no restart)

Example:
```
1. Edit texture in Photoshop
2. File → Export As → diffuse.png (save over existing)
3. VSCode FileWatcher detects change
4. UzBuild re-imports → compresses to BC7
5. Engine reloads texture
6. See new appearance instantly in viewport
```

No engine restart needed. Perfect for rapid iteration.

---

## Asset Distribution

### Single-File Distribution

For release, everything ships in `game.uzpak`:

```bash
# Build process
UzBuild.exe --project myproject --output release/
  → creates game.uzpak

# Ship these files
release/
├── UzEngine.exe
├── game.dll
├── game.uzpak
├── turbojpeg.dll
└── README.txt
```

Player downloads one `.exe`, one `.dll`, one `.uzpak`. Done.

### Optional: External Asset Packs

For DLC or mods (Phase 4):

```bash
# Create addon pack
UzBuild.exe --project addon1 --output addon1.uzpak

# Player downloads and installs
C:\Users\...\AppData\LocalLow\UzEngine\MyGame\addons\addon1.uzpak
```

Engine auto-discovers and loads.

---

## Troubleshooting

### "Asset not found: res://..."

**Cause:** Asset doesn't exist or import failed.

**Fix:**
1. Check file exists: `assets/meshes/player.glb`
2. Verify import: `.import/player.glb.import.json` exists
3. Check path spelling (case-sensitive on Linux)

### "Texture is pink/magenta"

**Cause:** Texture failed to load or compress.

**Fix:**
1. Check file format (PNG, JPEG, TGA, HDR)
2. Check dimensions (power-of-2 recommended)
3. Re-import: delete `.import.json`, re-save source file

### "Mesh looks mangled"

**Cause:** Scale or rotation import settings incorrect.

**Fix:**
1. Open `.import.json`
2. Adjust `scale` (1.0 = no change)
3. Try `flip_uv_y: true` for UV issues
4. Re-import

### "Audio doesn't play"

**Cause:** Format not supported or streaming threshold issue.

**Fix:**
1. Check audio format (WAV, MP3, OGG support)
2. Check sample rate (44.1 kHz or 48 kHz)
3. For files > 2MB, check `"streaming": true`

---

## Related Documentation

- [Asset Pipeline Spec](../specs/asset-pipeline.md) (Phase 2 spec)
- [Lua Scripting Guide](lua-scripting-guide.md) — Load assets in scripts
- [Getting Started](getting-started.md) — First project setup
- [Build Guide](build.md) — UzBuild.exe and deployment

---

## FAQ

**Q: Can I use assets from other projects?**
A: Not directly. Copy the source file to your project's `assets/` folder and import fresh.

**Q: How do I re-import an asset?**
A: Delete the `.import.json` file and save the source file again (or touch it to trigger FileWatcher).

**Q: Can I import multiple formats of the same mesh?**
A: Yes! Import both `.glb` and `.fbx`. The engine treats them as separate assets.

**Q: What happens to assets when I rebuild game.dll?**
A: They're untouched. `.uzpak` is separate from engine binary.

**Q: Can I use the same material on different meshes?**
A: Yes! Materials are reusable. Point multiple meshes to the same `.uzmat`.

**Q: How do I edit textures after import?**
A: Edit the PNG/HDR source file, save over it. Engine detects change → re-imports → reloads.
