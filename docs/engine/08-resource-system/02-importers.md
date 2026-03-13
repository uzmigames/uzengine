# Resource System — Asset Importers

> **Status**: Done (Milestone 1.2)
> **Source**: `runtime/engine/asset/importers/` (base interfaces and implementations), `runtime/engine/asset/formats/` (binary format specs)
> **Depends on**: None (pure interfaces)

## Purpose

Asset importers convert source files (HLSL, PNG, glTF) into engine-native runtime formats (.uzshader, .uztexture, .uzmesh). Each importer is a **pure function**: given the same source bytes and options, it always produces identical output bytes. This determinism is what makes content-addressed caching reliable.

## IAssetImporter Interface

```cpp
class IAssetImporter {
public:
    virtual const char* Name() const = 0;       // "shader", "texture", "mesh"
    virtual uint32_t Version() const = 0;        // Bump to invalidate all cached outputs
    virtual const std::vector<std::string>& Extensions() const = 0;  // {".hlsl", ".glsl"}
    virtual AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) = 0;
};
```

### Contract

1. **Deterministic** — no RNG, no timestamps, no environment-dependent behavior
2. **No side effects** — does not access the cache or filesystem (logging is OK)
3. **Version discipline** — `Version()` must be bumped whenever import logic changes

### AssetImportResult

```cpp
struct AssetImportResult {
    bool success;                // true if import completed without errors
    std::vector<uint8_t> data;   // Runtime-ready bytes (empty on failure)
    std::string error;           // Human-readable error message (empty on success)
};
```

### AssetImportOptions

```cpp
struct AssetImportOptions {
    std::string entryPoint;              // "main" (shader entry function)
    std::string stage;                   // "vertex", "pixel", "compute"
    std::string model;                   // "sm5_1", "sm6_0", ..., "sm6_7"
    std::string format;                  // "dxil" or "spirv"
    std::vector<std::string> defines;    // {"USE_PBR=1", "DEBUG"}
    bool debug = false;                  // Include debug symbols
};
```

Not all fields apply to every importer — unused fields are silently ignored.

## AssetImporterRegistry

Central registry mapping file extensions to importers:

```cpp
AssetImporterRegistry registry;
registry.Register(std::make_unique<ShaderImporter>());
// registry.Register(std::make_unique<TextureImporter>());  // Future
// registry.Register(std::make_unique<MeshImporter>());     // Future

// Lookup by extension
auto* importer = registry.FindByExtension(".hlsl");

// Lookup by name
auto* importer = registry.FindByName("shader");
```

### Registration Rules

- Each extension can only be claimed by **one** importer (no conflicts)
- Each importer name must be **unique**
- Registration fails (returns false, logs error) on name or extension conflict
- Registration must happen at startup before any imports (not thread-safe)
- Lookup is thread-safe after registration is complete

## Current Importers

### ShaderImporter

Compiles HLSL source into .uzshader binary format:

| Extension | Output Format | Pipeline |
|-----------|--------------|----------|
| `.hlsl` | `.uzshader` | HLSL → DXC → DXIL/SPIR-V bytecode |

The ShaderImporter uses the engine's `ShaderCompiler` (section 05) internally.

### Planned Importers

| Importer | Extensions | Output | Notes |
|----------|-----------|--------|-------|
| TextureImporter | `.png`, `.jpg`, `.tga`, `.exr`, `.hdr` | `.uztexture` | stb_image → BC7/ASTC/ETC2 compression |
| MeshImporter | `.gltf`, `.glb`, `.fbx`, `.obj` | `.uzmesh` | fastgltf/assimp → optimized vertex buffers |
| AudioImporter | `.wav`, `.ogg`, `.mp3` | `.uzaudio` | Resampling + platform codec selection |
| AnimationImporter | `.gltf` (animations) | `.uzanim` | Keyframe curves + event markers |

## Implementing a New Importer

```cpp
class TextureImporter final : public IAssetImporter {
public:
    const char* Name() const override { return "texture"; }
    uint32_t Version() const override { return 1; }  // Bump on logic changes
    const std::vector<std::string>& Extensions() const override {
        static std::vector<std::string> exts = {".png", ".jpg", ".tga", ".exr"};
        return exts;
    }

    AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override
    {
        // 1. Decode source image (stb_image)
        // 2. Generate mipmaps
        // 3. Compress to BC7 (desktop) or ASTC (mobile)
        // 4. Serialize to .uztexture format
        // 5. Return AssetImportResult{true, outputBytes, ""}
    }

private:
    // No mutable state — pure function
};
```

## Engine Comparison

| Aspect | UE5 | Godot | UzEngine |
|--------|-----|-------|----------|
| Import interface | `UFactory` + `UAssetImportData` | `ResourceFormatLoader` + `EditorImportPlugin` | `IAssetImporter` (pure function) |
| Registry | `UFactory::GetSupportedClass()` + CDO discovery | `ClassDB::register_class<>()` | `AssetImporterRegistry::Register()` |
| Versioning | `FAssetRegistryVersion` (global) + per-format versions | None (reimport on schema change) | `IAssetImporter::Version()` per importer |
| Determinism | Not guaranteed (editor state can leak in) | Not guaranteed | **Enforced by contract** |

## Rules

1. **ALWAYS implement `Import()` as a pure function** — no filesystem access, no cache access, no mutable state
2. **ALWAYS bump `Version()` when changing import logic** — cache invalidation depends on this
3. **NEVER claim an extension already registered by another importer** — one extension = one importer
4. **ALWAYS provide descriptive error messages in `AssetImportResult::error`** — users need to know why import failed
5. **ALWAYS register importers before starting any import operations** — registration is not thread-safe
