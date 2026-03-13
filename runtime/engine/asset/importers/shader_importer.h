/// UzEngine Shader Importer
///
/// Converts HLSL source files into .uzshader binary format via DXC compilation.
/// Wraps the existing `ShaderCompiler` (dynamic DXC loading) and packages the
/// compiled bytecode into the .uzshader binary layout.
///
/// # Supported Extensions
/// `.hlsl`, `.hlsli` (include files are not directly importable)
///
/// # Options
/// - `entryPoint`: HLSL entry function name (default: "main")
/// - `stage`: "vertex", "pixel", "compute", "geometry", "hull", "domain", "amplification", "mesh"
/// - `model`: "sm5_1", "sm6_0" .. "sm6_7" (default: "sm6_6")
/// - `format`: "dxil" or "spirv" (default: "dxil")
/// - `defines`: preprocessor defines (e.g., "USE_PBR=1")
/// - `debug`: include debug info
///
/// # Platform
/// Windows only — DXC (dxcompiler.dll) is required for compilation.

#pragma once

#ifdef _WIN32

#include "asset_importer.h"

#include <string>
#include <vector>

namespace uz {

class ShaderCompiler;

/// Imports HLSL source into .uzshader binary format.
///
/// Delegates compilation to `ShaderCompiler` (DXC), then packages the
/// resulting bytecode into the .uzshader binary layout with header + bytecode.
class ShaderImporter final : public IAssetImporter {
public:
    /// Constructs a ShaderImporter bound to an external ShaderCompiler.
    ///
    /// # Arguments
    /// * `compiler` - Reference to an initialized `ShaderCompiler` instance.
    ///   Must outlive this importer. Must have `IsInitialized() == true`.
    ///
    /// # Safety
    /// The compiler is stored by reference — the caller must ensure it is
    /// not destroyed while this importer is in use.
    explicit ShaderImporter(ShaderCompiler& compiler);

    /// Returns `"shader"` — the unique importer name.
    [[nodiscard]] const char* Name() const override;

    /// Returns the current importer version (currently `1`).
    ///
    /// Bump when compilation logic or .uzshader output format changes.
    [[nodiscard]] uint32_t Version() const override;

    /// Returns `{".hlsl"}` — the file extensions this importer handles.
    [[nodiscard]] const std::vector<std::string>& Extensions() const override;

    /// Compiles HLSL source into .uzshader binary format via DXC.
    ///
    /// # Arguments
    /// * `sourceData` - Raw HLSL source bytes (UTF-8).
    /// * `sourceName` - Filename for DXC error messages (e.g., "pbr_vs.hlsl").
    /// * `options` - Import configuration. Reads the following fields:
    ///   - `stage`: "vertex", "pixel", "compute", etc. (default: "vertex")
    ///   - `model`: "sm5_1" .. "sm6_7" (default: "sm6_6")
    ///   - `format`: "dxil" or "spirv" (default: "dxil")
    ///   - `entryPoint`: HLSL entry function (default: "main")
    ///   - `defines`: preprocessor defines
    ///   - `debug`: include debug symbols
    ///
    /// # Returns
    /// On success: `result.data` contains the complete .uzshader binary
    /// (header + compiled bytecode). On failure: `result.error` contains
    /// the DXC error output.
    ///
    /// # Errors
    /// Logs `Asset::Error` with the source name and error on failure.
    [[nodiscard]] AssetImportResult Import(
        const std::vector<uint8_t>& sourceData,
        const std::string& sourceName,
        const AssetImportOptions& options) override;

private:
    ShaderCompiler& m_compiler;
    std::vector<std::string> m_extensions;
};

} // namespace uz

#endif // _WIN32
