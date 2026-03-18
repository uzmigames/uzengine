/// UzEngine GPU Material Baker — Compute Shader Material Graph Evaluator
///
/// Replaces CPU-side MaterialBaker for production use. Takes generated HLSL
/// from a MaterialGraph (the `ComputeMaterialOutputs()` function), wraps it
/// in a compute shader, and dispatches on the GPU to produce RGBA8 textures.
///
/// Eliminates the ~8 second CPU bottleneck of per-texel graph evaluation by
/// running all texels in parallel on the GPU via a single compute dispatch.
///
/// # Relationship to MaterialBaker
/// `MaterialBaker` (material_baker.h) evaluates graphs on the CPU and returns
/// pixel buffers. `MaterialBakerGpu` produces GPU textures directly — no
/// readback, no PNG round-trip, no re-upload. The CPU baker remains as a
/// fallback for headless/offline scenarios.
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render/init thread (same
/// constraint as ShaderPipeline and IRHIDevice resource creation).
///
/// # Examples
/// ```cpp
/// auto& pipeline = shaderMgr->GetPipeline();
/// auto result = MaterialBakerGpu::Bake(
///     device, pipeline, material.GetGeneratedHlsl(), 0, 1024, 1024);
/// if (result.success) {
///     mat.albedoSrvIndex = result.srvIndex;
///     mat.gpuResourcesLoaded = true;
/// }
/// ```

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <string>

namespace uz {

class IRHIDevice;
class ShaderPipeline;

// ─────────────────────────────────────────────────────────────────────────────
// MaterialBakerGpu — Static utility for GPU-side material graph baking
// ─────────────────────────────────────────────────────────────────────────────

/// Bakes material graph outputs to GPU textures via compute shader dispatch.
///
/// The baker generates a compute shader at runtime by wrapping the material
/// graph's generated HLSL in a dispatch kernel. Each thread writes one texel
/// of the output texture. The shader is compiled via ShaderPipeline (with
/// caching — second runs are near-instant).
class MaterialBakerGpu {
public:
    /// Output pin indices for selecting which PBR channel to bake.
    ///
    /// Matches the pin order of the material graph's PBROutput node.
    enum class OutputPin : uint32_t {
        BaseColor  = 0, ///< RGB base color (alpha = 1.0)
        Metallic   = 1, ///< Scalar replicated to RGB (alpha = 1.0)
        Roughness  = 2, ///< Scalar replicated to RGB (alpha = 1.0)
        Normal     = 3, ///< Tangent-space normal, encoded [0,1] via (n * 0.5 + 0.5)
        AO         = 4, ///< Scalar replicated to RGB (alpha = 1.0)
    };

    /// Result of a GPU material bake operation.
    ///
    /// On success, `texture` holds the GPU resource and `srvIndex` is the
    /// bindless SRV index ready for shader binding. On failure, `success`
    /// is false and the other fields are invalid.
    struct BakeResult {
        RHITextureHandle texture;              ///< GPU texture (owned by the device)
        uint32_t srvIndex = UINT32_MAX;        ///< SRV descriptor index in global heap
        bool success = false;                  ///< True if bake completed without errors
    };

    /// Bakes a material graph output to a GPU texture via compute shader.
    ///
    /// Generates a compute shader from the material's HLSL, compiles it,
    /// creates an RGBA8 UAV+SRV texture, dispatches the compute kernel,
    /// transitions to ShaderResource state, and returns the result.
    ///
    /// The compute shader is cached by ShaderPipeline — subsequent calls
    /// with the same HLSL source skip compilation entirely.
    ///
    /// # Arguments
    /// * `device` — RHI device for resource creation and command submission.
    /// * `pipeline` — ShaderPipeline for compiling the generated compute shader.
    /// * `materialHlsl` — Generated HLSL containing `ComputeMaterialOutputs()`.
    /// * `outputPin` — Which PBR output channel to bake.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with GPU texture and SRV index on success.
    [[nodiscard]] static BakeResult Bake(IRHIDevice* device,
                                         ShaderPipeline& pipeline,
                                         const std::string& materialHlsl,
                                         OutputPin outputPin,
                                         uint32_t width, uint32_t height);
};

} // namespace uz
