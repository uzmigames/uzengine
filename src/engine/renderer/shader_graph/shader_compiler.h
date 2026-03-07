// shader_compiler.h -- IShaderCompiler interface (visitor-compiler pattern).
//
// Nodes call typed methods on this interface during compilation.
// The concrete implementation (HlslTranslator) is in compiler/ (Phase 6).
// Each method returns a code chunk index into the compiler's SSA-like IR.

#pragma once

#include <cstdint>

namespace uz::sg {

class IShaderCompiler {
public:
    virtual ~IShaderCompiler() = default;

    // Constants.
    virtual int32_t constant(float v) = 0;
    virtual int32_t constant_vec(const float* v, int32_t count) = 0;

    // Arithmetic.
    virtual int32_t add(int32_t a, int32_t b) = 0;
    virtual int32_t subtract(int32_t a, int32_t b) = 0;
    virtual int32_t multiply(int32_t a, int32_t b) = 0;
    virtual int32_t divide(int32_t a, int32_t b) = 0;

    // Math functions.
    virtual int32_t lerp(int32_t a, int32_t b, int32_t alpha) = 0;
    virtual int32_t dot(int32_t a, int32_t b) = 0;
    virtual int32_t normalize(int32_t v) = 0;
    virtual int32_t power(int32_t base, int32_t exp) = 0;

    // Texture.
    virtual int32_t texture_sample(int32_t tex_param_chunk, int32_t uv) = 0;

    // Swizzle / component operations.
    virtual int32_t component_mask(int32_t v, const char* mask) = 0;
    virtual int32_t append(int32_t a, int32_t b) = 0;

    // Built-in inputs.
    virtual int32_t texcoord(int32_t index) = 0;
    virtual int32_t time() = 0;
    virtual int32_t world_position() = 0;
    virtual int32_t world_normal() = 0;
    virtual int32_t camera_position() = 0;

    // Type promotion: emit splatting code (e.g., float1 -> float3(x,x,x)).
    virtual int32_t promote(int32_t chunk, int32_t target_components) = 0;

    // Parameters (create a cbuffer reference).
    virtual int32_t scalar_param(const char* name, float default_val) = 0;
    virtual int32_t vector_param(const char* name, const float* default_val, int32_t count) = 0;
    virtual int32_t texture_param(const char* name) = 0;

    // Compile a connected input. Called by nodes to compile upstream.
    virtual int32_t compile_input(uint32_t node_id, int32_t output_index) = 0;
};

} // namespace uz::sg
