# uzcook — Asset Cook Tool

Batch-imports source assets (HLSL shaders) into engine-native formats (`.uzshader`), stores them in the content-addressed cache (`.cache/`), writes import sidecars (`.uzimport`), and optionally packs everything into a `.uzpak` archive with a `.uzmanifest` for runtime loading.

> **Source**: `runtime/tools/uzcook.cpp` | **Platform**: Windows only (requires DXC)

---

## Usage

```bash
uzcook --source <dir> --output <dir> [--pak <name>] [--verbose]
```

| Flag | Description |
|------|-------------|
| `--source <dir>` | Directory containing source assets (HLSL, etc.) — scanned recursively |
| `--output <dir>` | Output directory for `.cache/`, `.uzimport` sidecars, `.uzpak` |
| `--pak <name>` | Pack all cooked assets into `<name>.uzpak` + `<name>.uzmanifest` |
| `--verbose` | Enable debug-level logging |

---

## Cook Pipeline

For each importable file found under `--source`:

1. **Scan** — recursively find files matching registered importer extensions (`.hlsl`)
2. **Sidecar check** — read `.uzimport` sidecar; skip if source hash and importer version match
3. **Import** — compile via registered importer (`ShaderImporter` → DXC → DXIL bytecode)
4. **Cache** — store compiled output in `.cache/imported/<hash>.bin` (content-addressed)
5. **Sidecar write** — create/update `.uzimport` JSON sidecar with hash, version, outputs
6. **Pack** (optional) — if `--pak` specified, bundle all outputs into `.uzpak` + `.uzmanifest`

### Incremental Builds

uzcook uses content-addressed caching via xxHash64. On subsequent runs:
- Files whose source hash hasn't changed are **skipped** (no recompilation)
- Only modified or new files are re-imported
- The `.uzimport` sidecar tracks: importer name, version, source hash, options, and output paths

---

## Shader Stage Detection

The shader stage is inferred from the filename:

| Filename Pattern | Stage |
|-----------------|-------|
| `*_vs.hlsl`, `*_vert.hlsl` | `vertex` |
| `*_ps.hlsl`, `*_frag.hlsl`, `*_pixel.hlsl` | `pixel` |
| `*_cs.hlsl`, `*_comp.hlsl` | `compute` |

Files that don't match any pattern are skipped with an error.

---

## Output Formats

### .uzshader

Engine-native compiled shader bytecode (DXIL). Produced by `ShaderImporter` via DXC compilation at SM6.6.

### .uzimport

JSON sidecar written next to the source file:

```json
{
  "importer": "ShaderImporter",
  "version": 1,
  "sourceHash": "a1b2c3d4e5f6...",
  "options": {
    "stage": "vertex",
    "model": "sm6_6",
    "entry_point": "main"
  },
  "outputs": {
    "sm6_6": ".cache/imported/deadbeef12345678.bin"
  },
  "dependencies": []
}
```

### .uzpak

Binary archive containing all cooked assets. Produced when `--pak` is specified. Paired with a `.uzmanifest` that lists logical paths, sizes, types, and dependencies for runtime loading.

---

## Examples

### Cook all shaders (no pak)

```bash
uzcook --source shaders/ --output build/cooked/
```

Output:
```
build/cooked/.cache/imported/a1b2c3d4.bin    # Cached DXIL bytecode
shaders/editor/grid_vs.hlsl.uzimport         # Sidecar (next to source)
shaders/editor/grid_ps.hlsl.uzimport
```

### Cook and pack into archive

```bash
uzcook --source shaders/ --output build/cooked/ --pak engine_shaders
```

Additional output:
```
build/cooked/engine_shaders.uzpak            # Binary archive
build/cooked/engine_shaders.uzmanifest       # Asset manifest
```

### Incremental rebuild

```bash
# Second run — only modified files are re-imported
uzcook --source shaders/ --output build/cooked/ --verbose
# [Cook|Info] Cook complete: imported=1, skipped=6, failed=0
```

---

## Logging

uzcook uses the engine logging system (`UZ_LOG`) with the `Cook` category:

```
[Cook|Info]  Cook starting: source='shaders/', output='build/cooked/'
[Cook|Info]  Found 7 importable source files
[Cook|Info]  Importing: 'editor/grid_vs.hlsl' (stage=vertex, model=sm6_6)
[Cook|Debug] Up-to-date, skipping: 'editor/grid_ps.hlsl'
[Cook|Info]  Cook complete: imported=1, skipped=6, failed=0
```

Use `--verbose` to see `Debug`-level messages (skip reasons, hash details).

---

## Dependencies

uzcook links against the full engine runtime library:
- `ShaderCompiler` (DXC wrapper)
- `AssetImporterRegistry` + `ShaderImporter`
- `AssetCache` (content-addressed storage)
- `ImportSidecar` (JSON read/write)
- `PakFormat` + `AssetManifest` (archive generation)

Unlike `uzembed` (standalone, no engine deps), uzcook requires the engine library and DXC DLLs (`dxcompiler.dll`, `dxil.dll`).
