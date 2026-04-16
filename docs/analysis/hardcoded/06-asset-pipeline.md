# 06 — Asset Pipeline Conventions (Naming, Units, Axes, Extensions)

**Severity:** MEDIUM
**File focus:** `runtime/tools/uzcook.cpp`, `tools/uzfbx.cpp`, `tools/uzmesh.cpp`, `tools/uzpack.cpp`, `tools/uzengine.cpp`

The asset cooker bakes in conventions about texture naming, shader entry points, coordinate systems, and pak sizing. These conventions match the sample project but aren't configurable. Projects deviating from them fail silently.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 6.1 | Texture slot detection | [uzcook.cpp:176-274](../../../runtime/tools/uzcook.cpp#L176-L274) | `if (lower.find("basecolor") != npos ... "albedo" ... "diffuse" ... "normal" ... "_orm" ...)` | 10+ hardcoded keywords; non-EN or custom names default to BC7 sRGB (wrong for normal maps) | Load from `texture_slots.json` config |
| 6.2 | Shader stage by filename | [uzcook.cpp:473-486](../../../runtime/tools/uzcook.cpp#L473-L486) | `if (logicalPath.find("_vs") != npos) ... "_ps" ... "_cs"` | `shader_vertex.hlsl` (using `_vertex`) fails silently | Read stage from shader pragma or CLI metadata |
| 6.3 | Shader target hardcoded | [uzcook.cpp:471-472](../../../runtime/tools/uzcook.cpp#L471-L472) | `format = "dxil"; model = "sm6_6"` | Locks to DX12 / SM6.6; Vulkan/older GPUs can't use cooked blobs | CLI `--shader-format {dxil,spirv}`, `--shader-model` |
| 6.4 | Shader entry hardcoded | [uzcook.cpp:470](../../../runtime/tools/uzcook.cpp#L470) | `entryPoint = "main"` | Forces `main` (actually matches engine rule — see note below) | Keep as default but allow `--entry-point` override |
| 6.5 | Importer output ext map | [uzcook.cpp:436-437](../../../runtime/tools/uzcook.cpp#L436-L437) + [524-541] | `if (importerName == "mesh") companionExt = ".uzmesh"` — repeated 5+ places | Magic strings scattered; adding importer needs 5 edits | Central `ImporterRegistry` with ext per name |
| 6.6 | Importer name comparisons | [uzcook.cpp:436,469,489,524-541](../../../runtime/tools/uzcook.cpp#L436) | Multiple `strcmp(importer->Name(), "mesh")` etc. | Typo-prone magic strings | `enum class ImporterType { Mesh, Fbx, Texture, Shader };` |
| 6.7 | Unit scale (cm) | [uzfbx.cpp:88](../../../runtime/tools/uzfbx.cpp#L88) + [uzmesh.cpp:91,226](../../../runtime/tools/uzmesh.cpp#L91) | `opts.target_unit_meters = 0.01f` | glTF in meters → triple-scaled; meter-based projects broken | `.uzproj` unit scale config; default cm |
| 6.8 | Axis conversion (Y→Z) | [uzfbx.cpp:149-150](../../../runtime/tools/uzfbx.cpp#L149-L150) + [uzmesh.cpp:802](../../../runtime/tools/uzmesh.cpp#L802) | `v.position = {pos.z, pos.x, pos.y}` | Assumes all input Y-up; Maya Z-up rig yields garbage | `axes: {source, target}` config per importer |
| 6.9 | UV flip (V inversion) | [uzfbx.cpp:162](../../../runtime/tools/uzfbx.cpp#L162) | `v.texcoord = {uv.x, 1.0f - uv.y}` | Hardcoded DX-convention flip; breaks OpenGL-origin exports | `uv_convention: {source, target}` config |
| 6.10 | glTF texture prefix | [uzmesh.cpp:656-657](../../../runtime/tools/uzmesh.cpp#L656) | Strips leading `"textures/"` from URIs | Breaks `assets/textures/x.png` paths | Don't strip; VFS resolves full paths |
| 6.11 | Pak max size | [uzpack.cpp:63,156](../../../runtime/tools/uzpack.cpp#L63) | `maxPakMB = 500` default | One-size tradeoff; mobile / console differ | `--pak-profile {mobile,console,pc}` presets |
| 6.12 | Pak alignment | [pak_format.h:54-55](../../../runtime/engine/asset/pak/pak_format.h#L54-L55) | `kPakAlignment = 64` | DX12-centric; ARM/mobile cache lines differ | Backend-aware default |
| 6.13 | Mesh output dir | [uzmesh.cpp:1234](../../../runtime/tools/uzmesh.cpp#L1234) | `meshRelDir = "meshes/" + sceneName` | Forces rigid `meshes/<scene>/` layout | `--scene-output-dir` pattern |
| 6.14 | Scene output ext | [uzcook.cpp:414](../../../runtime/tools/uzcook.cpp#L414) | `sidecarPath = sourcePath + ".uzimport"` | Sidecar ext non-configurable | `--sidecar-suffix` flag |

## Configurable Texture Slot Registry

Replace `SelectTextureFormat()` hardcoded keyword matching with a data-driven registry:

```json
// engine/defaults/texture_slots.json (project can override)
{
  "slots": [
    {
      "name": "color",
      "format": "BC7_SRGB",
      "keywords": ["basecolor", "albedo", "diffuse", "col", "color"],
      "channels": 3
    },
    {
      "name": "normal",
      "format": "BC5_UNORM",
      "keywords": ["normal", "nrm", "norm", "n"],
      "channels": 2,
      "reconstructZ": true
    },
    {
      "name": "orm",
      "format": "BC7_UNORM",
      "keywords": ["orm", "occlusion_roughness_metallic", "arm"],
      "channels": 3
    },
    {
      "name": "emissive",
      "format": "BC6H_UF16",
      "keywords": ["emissive", "emit", "em"],
      "channels": 3,
      "hdr": true
    }
  ]
}
```

Projects add overrides in `.uzproj`:
```json
{
  "asset": {
    "textureSlots": "project/texture_slots.json"
  }
}
```

## Shader Stage Detection — Multi-Strategy

```cpp
enum class ShaderStage { Vertex, Pixel, Compute, Mesh, Task, RayGen, ... };

ShaderStage DetectShaderStage(const fs::path& file, const std::string& source) {
    // 1. CLI explicit (highest priority): --stage pixel
    if (cli.stage.has_value()) return cli.stage.value();
    
    // 2. Source pragma: //! stage=pixel
    if (auto s = ParsePragmaStage(source)) return *s;
    
    // 3. Entry function attribute: [shader("pixel")] void main(...)
    if (auto s = ParseShaderAttribute(source)) return *s;
    
    // 4. Filename suffix (last resort): foo_ps.hlsl
    if (file.stem().ends_with("_ps")) return ShaderStage::Pixel;
    if (file.stem().ends_with("_vs")) return ShaderStage::Vertex;
    if (file.stem().ends_with("_cs")) return ShaderStage::Compute;
    
    throw std::runtime_error("Cannot detect shader stage for " + file.string());
}
```

## Unit / Axis / UV Conversion Pipeline

All three conventions should be declarative per-importer:

```json
// project.uzproj
{
  "asset": {
    "importers": {
      "fbx": {
        "unitScale": { "from": "cm", "to": "cm" },
        "axes":      { "source": "y-up-rh", "target": "z-up-lh" },
        "uv":        { "source": "opengl",  "target": "directx" }
      },
      "gltf": {
        "unitScale": { "from": "m", "to": "cm" },
        "axes":      { "source": "y-up-rh", "target": "z-up-lh" },
        "uv":        { "source": "opengl",  "target": "directx" }
      }
    }
  }
}
```

Importer runtime:
```cpp
Mat4 ComputeImportTransform(const ImporterConfig& cfg) {
    Mat4 m = Mat4::Identity();
    m = AxisConvertMatrix(cfg.axes.source, cfg.axes.target) * m;
    m = ScaleMatrix(cfg.unitScale.ratio) * m;
    return m;
}
```

## Pak Profile Presets

```json
// uzpack.exe --pak-profile mobile
{
  "mobile":  { "maxPakMB": 100, "alignment": 32 },
  "console": { "maxPakMB": 1024, "alignment": 64 },
  "pc":      { "maxPakMB": 2048, "alignment": 64 }
}
```

## Note on Shader Entry Point

Finding 6.4 (`entryPoint = "main"`) is **correct by design** per [.claude/rules/shader-entry-point.md](../../../.claude/rules/shader-entry-point.md). However it should still be overridable via CLI for integration with existing codebases that use `VSMain`/`PSMain`. Keep `main` as default.
