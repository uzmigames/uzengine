#pragma once

/// VFX Module Compositor -- stitches module HLSL snippets into compute shaders.
///
/// Takes a list of VFX modules and concatenates their shader snippets into a
/// complete compute shader with standard buffer declarations and a dispatch
/// wrapper. Produces both update shaders (simulation) and spawn shaders.
///
/// Each composed shader gets a cache key (FNV-1a hash of module names + order)
/// so the pipeline state cache can avoid redundant recompilation.
///
/// # Thread Safety
/// All methods are const and stateless -- safe to call from any thread.

#include "vfx_module.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// ComposedShader -- result of module composition
// ---------------------------------------------------------------------------

/// Output of the module compositor: complete HLSL source + cache key.
///
/// The `hlslSource` contains a self-contained compute shader ready for
/// compilation. The `cacheKey` uniquely identifies the module combination
/// and order, allowing the PSO cache to skip recompilation.
struct ComposedShader {
    /// Complete HLSL compute shader source code.
    std::string hlslSource;

    /// FNV-1a hash of the module names in order.
    /// Two ComposedShaders with the same cacheKey produce identical HLSL.
    uint64_t cacheKey = 0;
};

// ---------------------------------------------------------------------------
// ModuleCompositor -- shader composition engine
// ---------------------------------------------------------------------------

/// Composes VFX module HLSL snippets into complete compute shaders.
///
/// The compositor is stateless -- it reads module shader code via
/// `IVFXModule::GetShaderCode()` and concatenates them with standard
/// buffer declarations and a dispatch wrapper.
///
/// # Shader Structure (Update)
/// 1. Common header (`#include "vfx_common.hlsli"`)
/// 2. Standard buffer declarations (positions, velocities, ages, etc.)
/// 3. Per-module HLSL snippets (in order)
/// 4. `[numthreads(256,1,1)] void CSMain(...)` dispatch wrapper
///
/// # Examples
/// ```cpp
/// ModuleCompositor compositor;
/// auto shader = compositor.ComposeUpdateShader({gravityModule, dragModule});
/// // shader.hlslSource is ready for DXC compilation
/// // shader.cacheKey identifies this exact module combination
/// ```
class ModuleCompositor {
public:
    ModuleCompositor() = default;
    ~ModuleCompositor() = default;

    /// Composes a particle update (simulation) compute shader.
    ///
    /// Concatenates module snippets in order, wrapped in standard buffer
    /// declarations and a `[numthreads(256,1,1)]` dispatch function.
    /// Each module's snippet executes sequentially per particle.
    ///
    /// # Parameters
    /// - `modules`: Ordered list of modules to compose. Must not be empty.
    ///   Null pointers in the list are skipped with a warning.
    ///
    /// # Returns
    /// Composed shader with complete HLSL and a stable cache key.
    [[nodiscard]] ComposedShader ComposeUpdateShader(
        const std::vector<IVFXModule*>& modules) const;

    /// Composes a particle spawn compute shader.
    ///
    /// Similar to `ComposeUpdateShader` but uses spawn-specific buffer
    /// declarations and dispatch wrapper. Spawn modules typically initialize
    /// particle attributes (position, velocity, color, size, lifespan).
    ///
    /// # Parameters
    /// - `spawnModules`: Ordered list of spawn modules. Must not be empty.
    ///   Null pointers in the list are skipped with a warning.
    ///
    /// # Returns
    /// Composed shader with complete HLSL and a stable cache key.
    [[nodiscard]] ComposedShader ComposeSpawnShader(
        const std::vector<IVFXModule*>& spawnModules) const;
};

} // namespace uz
