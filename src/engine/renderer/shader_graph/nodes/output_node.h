// output_node.h -- MaterialOutputNode: final PBR material output (8 inputs).

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"

namespace uz::sg {

class MaterialOutputNode : public ShaderNodeFixed<8> {
public:
    MaterialOutputNode() { type_name = "MaterialOutput"; }

    std::span<const PinDesc> input_descs() const override;
    std::span<const PinDesc> output_descs() const override;
    int32_t compile(IShaderCompiler& compiler, int32_t output_index) override;
    std::unique_ptr<ShaderNode> clone() const override;
};

} // namespace uz::sg
