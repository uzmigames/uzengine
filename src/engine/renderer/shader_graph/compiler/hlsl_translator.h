// hlsl_translator.h -- HLSL code generator for shader graphs.
//
// Walks the graph from MaterialOutput, compiles each node via IShaderCompiler,
// builds an SSA-like IR of code chunks, then assembles final VS/PS HLSL text.

#pragma once

#include "engine/renderer/shader_graph/shader_compiler.h"
#include "engine/renderer/shader_graph/shader_graph_types.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uz::sg {

class ShaderGraph;

// Result of compiling a shader graph to HLSL.
struct CompileResult {
    bool ok = false;
    std::string vertex_hlsl;
    std::string pixel_hlsl;
    std::vector<std::string> errors;
};

// A code chunk in the SSA-like IR.
struct CodeChunk {
    int32_t index = -1;
    std::string expr;           // The HLSL expression (e.g. "a + b")
    ValueType type = ValueType::None;
    bool is_inline = false;     // true = inline into use sites; false = named local var
    std::string var_name;       // Generated variable name (empty if inline)
};

class HlslTranslator : public IShaderCompiler {
public:
    // Compile a shader graph to HLSL. Entry point for Phase 6.
    CompileResult compile(ShaderGraph& graph);

    // --- IShaderCompiler interface ---

    // Constants.
    int32_t constant(float v) override;
    int32_t constant_vec(const float* v, int32_t count) override;

    // Arithmetic.
    int32_t add(int32_t a, int32_t b) override;
    int32_t subtract(int32_t a, int32_t b) override;
    int32_t multiply(int32_t a, int32_t b) override;
    int32_t divide(int32_t a, int32_t b) override;

    // Math functions.
    int32_t lerp(int32_t a, int32_t b, int32_t alpha) override;
    int32_t dot(int32_t a, int32_t b) override;
    int32_t normalize(int32_t v) override;
    int32_t power(int32_t base, int32_t exp) override;

    // Texture.
    int32_t texture_sample(int32_t tex_param_chunk, int32_t uv) override;

    // Swizzle / component operations.
    int32_t component_mask(int32_t v, const char* mask) override;
    int32_t append(int32_t a, int32_t b) override;

    // Built-in inputs.
    int32_t texcoord(int32_t index) override;
    int32_t time() override;
    int32_t world_position() override;
    int32_t world_normal() override;
    int32_t camera_position() override;

    // Type promotion.
    int32_t promote(int32_t chunk, int32_t target_components) override;

    // Parameters.
    int32_t scalar_param(const char* name, float default_val) override;
    int32_t vector_param(const char* name, const float* default_val, int32_t count) override;
    int32_t texture_param(const char* name) override;

    // Compile a connected input (recursive graph walk).
    int32_t compile_input(uint32_t node_id, int32_t output_index) override;

    // Access chunks (for testing).
    const std::vector<CodeChunk>& chunks() const { return chunks_; }

private:
    // Add a new code chunk. Returns its index.
    int32_t add_chunk(const std::string& expr, ValueType type, bool is_inline);

    // Get the expression string for a chunk (variable name or inline expr).
    std::string get_code(int32_t index) const;

    // Get the type of a chunk.
    ValueType get_type(int32_t index) const;

    // Create a unique symbol name.
    std::string create_symbol();

    // Assemble the final pixel shader HLSL.
    std::string assemble_pixel_shader(ShaderGraph& graph);

    // Assemble the final vertex shader HLSL.
    std::string assemble_vertex_shader(ShaderGraph& graph);

    // Reset state for a new compilation.
    void reset();

    // Expression cache: (node_id, output_index) -> chunk index.
    std::unordered_map<uint64_t, int32_t> expr_cache_;

    // Cycle detection: nodes currently being compiled.
    std::unordered_set<uint64_t> compilation_stack_;

    // The graph being compiled (set during compile()).
    ShaderGraph* graph_ = nullptr;

    // Code chunks (SSA IR).
    std::vector<CodeChunk> chunks_;

    // Symbol counter for unique variable names.
    int32_t symbol_counter_ = 0;

    // Collected scalar parameters: name -> {default_val, chunk_index}.
    struct ScalarParamInfo { float default_val; int32_t chunk; };
    std::unordered_map<std::string, ScalarParamInfo> scalar_params_;

    // Collected vector parameters: name -> {default_vals, count, chunk_index}.
    struct VectorParamInfo { float default_val[4]; int32_t count; int32_t chunk; };
    std::unordered_map<std::string, VectorParamInfo> vector_params_;

    // Collected texture parameters: name -> chunk_index.
    std::unordered_map<std::string, int32_t> texture_params_;

    // Texture register counter.
    int32_t tex_register_ = 0;

    // Compilation errors.
    std::vector<std::string> errors_;

    // Chunk indices for MaterialOutput pin inputs (filled during compilation).
    int32_t output_chunks_[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
};

} // namespace uz::sg
