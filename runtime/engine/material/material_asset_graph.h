/// UzEngine Graph-Based Material Asset
///
/// A material asset backed by a node graph (MaterialGraph) instead of the
/// fixed texture-path / scalar-parameter set used by the traditional Material
/// struct. The graph is compiled via HlslMaterialTranslator to produce a
/// custom `ComputeMaterialOutputs()` HLSL function that the renderer can
/// inject into the deferred/forward pixel shader.
///
/// # Relationship to Material
/// `Material` (material_asset.h) uses fixed PBR fields (albedoPath, roughness, etc.).
/// `MaterialAssetGraph` uses a programmable node graph that can express procedural
/// patterns, parameter-driven effects, and complex material blending.
///
/// Both types can coexist in the same scene — the renderer checks the material
/// type and selects the appropriate shader/PSO.
///
/// # Equivalent
/// Owns expression graph + compiles to HLSL (UMaterial pattern)
///
/// # Thread Safety
/// NOT thread-safe. Modify from the game thread only. The renderer reads a
/// snapshot of the compile result when building draw commands.
///
/// # Examples
/// ```cpp
/// MaterialShaderCache cache;
/// cache.Init();
///
/// MaterialAssetGraph mat;
/// if (mat.LoadFromFile("assets/mi_procgrid.uzmat", cache)) {
///     // mat.IsCompiled() == true
///     // mat.GetGeneratedHlsl() contains the shader function
///     // mat.GetCompileResult() has parameter/texture metadata
/// }
/// ```

#pragma once

#include "material_graph.h"
#include "material_shader_cache.h"

#include <cstdint>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// MaterialAssetGraph
// ─────────────────────────────────────────────────────────────────────────────

/// A material asset backed by a node graph instead of fixed texture paths.
///
/// Owns a MaterialGraph, compiles it on load/change, and provides the
/// generated HLSL + parameter metadata to the renderer.
///
/// Owns expression graph, compiles to shader map.
class MaterialAssetGraph {
public:
    /// Load a material graph from a .uzmat JSON file and compile it.
    ///
    /// Reads the JSON, deserializes into a MaterialGraph, then compiles via
    /// the provided MaterialShaderCache. On cache hit, no re-compilation occurs.
    ///
    /// # Parameters
    /// - `path`: VFS-relative or absolute path to the .uzmat file.
    /// - `cache`: Shader cache for content-addressed compilation.
    ///
    /// # Returns
    /// True if the file was loaded and compiled successfully.
    [[nodiscard]] bool LoadFromFile(const std::string& path, MaterialShaderCache& cache);

    /// Recompile the graph after changes (e.g., editor node edits).
    ///
    /// Runs the graph through the MaterialShaderCache again. If the content
    /// hash has not changed since the last compilation, this is a no-op (cache hit).
    ///
    /// # Parameters
    /// - `cache`: Shader cache for content-addressed compilation.
    ///
    /// # Returns
    /// True if compilation succeeded.
    [[nodiscard]] bool Recompile(MaterialShaderCache& cache);

    /// Returns a mutable reference to the underlying node graph for editing.
    [[nodiscard]] MaterialGraph& GetGraph() noexcept { return graph_; }
    /// Returns a const reference to the underlying node graph.
    [[nodiscard]] const MaterialGraph& GetGraph() const noexcept { return graph_; }

    /// Access the compilation result (parameter metadata, texture slots, etc.).
    [[nodiscard]] const MaterialCompileResult& GetCompileResult() const noexcept { return compiled_; }

    /// Content hash of the compiled graph, for PSO cache keying.
    [[nodiscard]] uint64_t GetContentHash() const noexcept { return contentHash_; }

    /// The generated HLSL source (ComputeMaterialOutputs function body).
    [[nodiscard]] const std::string& GetGeneratedHlsl() const noexcept { return generatedHlsl_; }

    /// True if this material has been successfully compiled.
    [[nodiscard]] bool IsCompiled() const noexcept { return compiled_.success; }

    /// File path this material was loaded from (for hot-reload detection).
    [[nodiscard]] const std::string& GetFilePath() const noexcept { return filePath_; }

    /// Material name (from the graph metadata).
    [[nodiscard]] const std::string& GetName() const noexcept { return graph_.name; }

    /// Material domain (Surface, PostProcess, Decal, UI).
    [[nodiscard]] MaterialDomain GetDomain() const noexcept { return graph_.domain; }

    /// Material shading model (DefaultLit, Unlit, Subsurface).
    [[nodiscard]] MaterialShadingModel GetShadingModel() const noexcept { return graph_.shadingModel; }

private:
    MaterialGraph graph_;
    MaterialCompileResult compiled_;
    std::string generatedHlsl_;
    uint64_t contentHash_ = 0;
    std::string filePath_;
};

} // namespace uz
