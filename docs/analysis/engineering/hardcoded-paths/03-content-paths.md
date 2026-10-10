# 03 — Hardcoded Content Paths, DLL Names, Directory Conventions

**Severity:** HIGH
**File focus:** Renderer init, config system, tools, RHI DLL loading

The runtime bakes in directory conventions (`"content/"`, `"textures/"`, `"meshes/"`) and DLL paths that assume the sample project layout. A custom project with a different folder structure either crashes on init or silently fails to hot-reload.

## Findings

| # | Category | File:Line | Snippet | Why Bad | Fix |
|---|----------|-----------|---------|---------|-----|
| 3.1 | VFX hot-reload root | [scene_renderer_init.cpp:837](../../../runtime/engine/renderer/scene/scene_renderer_init.cpp#L837) | `m_vfxHotReload->Initialize("content/", m_vfxSystem.get(), nullptr);` | Runtime hardcodes sample content dir | Read from `AppConfig::contentRoot` or `.uzproj` |
| 3.2 | Config content_root default | [config.cpp:196](../../../runtime/engine/config/config.cpp#L196) | `{"content_root", "content/"}` | Default assumes sample project dir layout | Derive from `.uzproj` location, not static |
| 3.3 | Cloud shape asset path | [cloud_pass.cpp:358](../../../runtime/engine/renderer/passes/atmosphere/cloud_pass.cpp#L358) | `"runtime/content/atmosphere/shape_detail.bin"` | Engine loads content from `runtime/content/` — engine depends on sample content layout | Ship as `engine/content/atmosphere/` built-in; override via `.uzproj`  **Resolved 2026-10-08** (`phase0_clouds-noise-assets`): build.zig installs `bin/content/atmosphere`; `CloudPass` reads the project VFS, then `ResolveEngineResource("content/...")`. |
| 3.4 | Uzcook cache dir | [uzcook.cpp:355](../../../runtime/tools/uzcook.cpp#L355) | `cacheDir = args.outputDir + "/.cache"` | Cache location not configurable | `--cache-dir <path>` CLI flag |
| 3.5 | Uzcook sidecar ext | [uzcook.cpp:414](../../../runtime/tools/uzcook.cpp#L414) | `sidecarPath = sourcePath + ".uzimport"` | Sidecar extension baked in | CLI `--sidecar-suffix`; allow symlinks |
| 3.6 | DXC compiler DLL path | [dx12_pso_creation.cpp:386](../../../runtime/engine/renderer/rhi/dx12_pso_creation.cpp#L386) | `"\\dxcompiler.dll"` | Backslash-only; brittle path concatenation | `fs::path(GetExeDir()) / "dxcompiler.dll"` |
| 3.7 | D3D12 DLL load | [dx12_device.cpp:328-330](../../../runtime/engine/renderer/rhi/dx12_device.cpp#L328-L330) | `LoadLibraryA("d3d12.dll")` no error context | Failure reports nothing about which DLL was missing | `GetLastError()` + descriptive log |
| 3.8 | Mesh output dir convention | [uzmesh.cpp:1234](../../../runtime/tools/uzmesh.cpp#L1234) | `meshRelDir = "meshes/" + sceneName` | Forces all cooked meshes under `meshes/<scene>/` | Configurable pattern `--scene-output-dir meshes/{scene}` |
| 3.9 | glTF texture prefix strip | [uzmesh.cpp:656-657](../../../runtime/tools/uzmesh.cpp#L656) | `if (uri.rfind("textures/", 0) == 0) uri = uri.substr(9);` | Blindly strips `textures/` prefix; breaks nested paths | Don't strip; let VFS resolve by full path |

## Directory Convention Audit

These directory names appear hardcoded in multiple places and should be configurable or derived from `.uzproj`:

- `content/` — app root for project assets
- `meshes/` — cooked mesh output
- `textures/` — glTF texture URI convention
- `atmosphere/` — cloud/sky runtime assets

### Proposed `.uzproj` schema
```json
{
  "project": {
    "contentRoot": "content",
    "outputDirs": {
      "meshes": "meshes/{scene}",
      "textures": "textures",
      "shaders": "shaders"
    },
    "cache": {
      "path": ".cache",
      "sidecarSuffix": ".uzimport"
    }
  }
}
```

### Engine Built-in Assets
Content shipped WITH the engine (shaders, default cloud noise, error textures) should live under `engine/content/` and be resolved via a dedicated VFS mount point:

```
vfs://engine/atmosphere/shape_detail.bin   (ships with engine)
vfs://project/content/meshes/...           (project-specific)
```

`cloud_pass.cpp` currently violates this — it reads from `runtime/content/`, which is neither "engine" nor "project". Move to `engine/content/atmosphere/`.

## DLL Loading Hygiene

Both `d3d12.dll` and `dxcompiler.dll` are loaded inline without:

1. **Error reporting** — no message distinguishes "missing DLL" from "wrong version"
2. **Search path control** — relies on default Windows DLL search order (security issue: DLL hijack)
3. **Version check** — no runtime check that DXC version matches what shaders were compiled against

### Recommended pattern
```cpp
auto dllPath = fs::path(GetExeDir()) / "dxcompiler.dll";
HMODULE h = LoadLibraryExW(
    dllPath.c_str(),
    nullptr,
    LOAD_LIBRARY_SEARCH_APPLICATION_DIR  // safe search
);
if (!h) {
    DWORD err = GetLastError();
    UZ_LOG(RHI, Error, "Failed to load {}: Win32 error {}", dllPath.string(), err);
    return false;
}
```

## Path Separator Consistency

Found mixed `\\` / `/` across tools. Since the engine targets cross-platform (Windows + future macOS/Linux via Metal/Vulkan backends), all paths MUST use `fs::path` or forward slashes only.

**Action:** grep for `"\\\\"` string literals in `runtime/` and migrate to `fs::path`.
