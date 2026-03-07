// texture_nodes.h -- TextureSampleNode: samples a Texture2D with UV coordinates.

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"

namespace uz::sg {

// TextureSample: 2 inputs (Tex, UV) -> 5 outputs (RGB, R, G, B, A).
class TextureSampleNode : public ShaderNodeFixed<2> {
public:
    TextureSampleNode() { type_name = "TextureSample"; }
    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& c, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
