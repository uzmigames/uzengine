/// UzEngine Material Baker — CPU-Side Graph Evaluator for Texture Baking
///
/// Evaluates a MaterialGraph on the CPU for each texel of a target resolution,
/// producing RGBA8 pixel data suitable for texture upload. Supports UV-based
/// materials (procedural patterns, constants, math) without requiring GPU
/// compilation or PSO creation.
///
/// # Use Cases
/// - Material preview thumbnails in the editor
/// - Fallback rendering when graph PSO is not yet compiled
/// - Texture export for static (non-animated) procedural materials
/// - End-to-end verification of the material graph pipeline
///
/// # Limitations
/// Only evaluates expressions that depend on UV coordinates and constants.
/// Expressions requiring runtime GPU data (WorldPosition, Time, TextureSample,
/// CameraVector) return their default pin values.
///
/// # Thread Safety
/// Stateless — safe to call from any thread. Each BakeXxx call is independent.

#pragma once

#include "material_graph.h"

#include <cstdint>
#include <vector>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// BakeResult — Output of a material bake operation
// ─────────────────────────────────────────────────────────────────────────────

/// Result of baking a material graph channel to a pixel buffer.
///
/// On success, `pixels` contains row-major RGBA8 data (top-to-bottom, left-to-right).
/// Each pixel is 4 bytes: [R, G, B, A]. Alpha is always 255 for opaque channels.
///
/// On failure (e.g., graph has no PBROutput, invalid topology), `success` is false
/// and `pixels` is empty.
struct BakeResult {
    std::vector<uint8_t> pixels;  ///< RGBA8 pixel data (width * height * 4 bytes)
    uint32_t width  = 0;         ///< Baked texture width in texels
    uint32_t height = 0;         ///< Baked texture height in texels
    bool success = false;        ///< True if baking completed without errors
};

// ─────────────────────────────────────────────────────────────────────────────
// MaterialBaker — Static utility for CPU-side material graph evaluation
// ─────────────────────────────────────────────────────────────────────────────

/// Evaluates a material graph on the CPU and produces pixel data.
///
/// The baker walks the graph in topological order for each texel, evaluating
/// expression nodes with the texel's UV coordinates as input. Expressions that
/// require GPU-only data (WorldPosition, Time, TextureSample) return their
/// pin default values.
///
/// # Supported Expressions
/// - **Constants**: ScalarConstant, Vector2Constant, Vector3Constant, Vector4Constant, ColorConstant
/// - **Coordinates**: TexCoord (UV from texel position)
/// - **Math**: Add, Subtract, Multiply, Divide, Lerp, Clamp, Saturate, Abs, Pow,
///   Sqrt, OneMinus, Frac, Floor, Ceil, Min, Max, Dot, Normalize, Length, Step, SmoothStep
/// - **Procedural**: CheckerPattern, NoisePerlin (Perlin 2D), GradientLinear, GradientRadial
/// - **Utility**: ComponentMask, Append, Fresnel (uses default normal/view)
/// - **Parameters**: ScalarParameter, VectorParameter (use default values)
///
/// # Unsupported Expressions (return default pin values)
/// - TextureSample (requires GPU texture binding)
/// - WorldPosition, ObjectPosition (requires mesh geometry)
/// - CameraVector, ScreenPosition (requires camera state)
/// - Time (requires frame clock)
/// - TriPlanarMapping (requires world-space normals)
/// - Panner (requires time)
/// - TextureParameter (requires GPU texture)
class MaterialBaker {
public:
    /// Bakes the BaseColor output of a material graph to an RGBA8 texture.
    ///
    /// Evaluates the expression connected to PBROutput pin 0 (BaseColor, Float3)
    /// for each texel. The result is clamped to [0,1] and quantized to 8-bit.
    /// Alpha channel is always 255 (fully opaque).
    ///
    /// # Arguments
    /// * `graph` — The material graph to evaluate. Must contain a PBROutput node.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with RGBA8 pixel data on success. Check `.success` before use.
    [[nodiscard]] static BakeResult BakeBaseColor(const MaterialGraph& graph,
                                                  uint32_t width, uint32_t height);

    /// Bakes the Roughness output of a material graph to an RGBA8 texture.
    ///
    /// Evaluates the expression connected to PBROutput pin 2 (Roughness, Float)
    /// for each texel. The scalar value is replicated to R=G=B channels.
    /// Alpha channel is always 255.
    ///
    /// # Arguments
    /// * `graph` — The material graph to evaluate. Must contain a PBROutput node.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with RGBA8 pixel data on success.
    [[nodiscard]] static BakeResult BakeRoughness(const MaterialGraph& graph,
                                                  uint32_t width, uint32_t height);

    /// Bakes the Metallic output of a material graph to an RGBA8 texture.
    ///
    /// Evaluates the expression connected to PBROutput pin 1 (Metallic, Float)
    /// for each texel. The scalar value is replicated to R=G=B channels.
    /// Alpha channel is always 255.
    ///
    /// # Arguments
    /// * `graph` — The material graph to evaluate. Must contain a PBROutput node.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with RGBA8 pixel data on success.
    [[nodiscard]] static BakeResult BakeMetallic(const MaterialGraph& graph,
                                                 uint32_t width, uint32_t height);

    /// Bakes the Normal output of a material graph to an RGBA8 texture.
    ///
    /// Evaluates the expression connected to PBROutput pin 3 (Normal, Float3).
    /// Output is in tangent space [0,1] encoding: (normal * 0.5 + 0.5).
    /// Alpha channel is always 255.
    ///
    /// # Arguments
    /// * `graph` — The material graph to evaluate. Must contain a PBROutput node.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with RGBA8 pixel data on success.
    [[nodiscard]] static BakeResult BakeNormal(const MaterialGraph& graph,
                                               uint32_t width, uint32_t height);

    /// Bakes the AO output of a material graph to an RGBA8 texture.
    ///
    /// Evaluates the expression connected to PBROutput pin 6 (AO, Float).
    /// The scalar value is replicated to R=G=B channels. Alpha is always 255.
    ///
    /// # Arguments
    /// * `graph` — The material graph to evaluate. Must contain a PBROutput node.
    /// * `width` — Output texture width in texels. Must be > 0.
    /// * `height` — Output texture height in texels. Must be > 0.
    ///
    /// # Returns
    /// `BakeResult` with RGBA8 pixel data on success.
    [[nodiscard]] static BakeResult BakeAO(const MaterialGraph& graph,
                                           uint32_t width, uint32_t height);
};

} // namespace uz
