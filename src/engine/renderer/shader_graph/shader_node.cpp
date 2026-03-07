// shader_node.cpp -- ShaderNode base class serialization.

#include "engine/renderer/shader_graph/shader_node.h"

#include <nlohmann/json.hpp>

namespace uz::sg {

void ShaderNode::serialize(nlohmann::json& j) const {
    j["id"]   = id;
    j["type"] = type_name;
    j["x"]    = pos_x;
    j["y"]    = pos_y;

    // Serialize input default values.
    auto& inputs_arr = j["inputs"];
    inputs_arr = nlohmann::json::array();
    for (int32_t i = 0; i < input_count(); ++i) {
        const auto& inp = get_input(i);
        nlohmann::json inp_j;
        inp_j["default"] = {inp.default_value[0], inp.default_value[1],
                            inp.default_value[2], inp.default_value[3]};
        inputs_arr.push_back(std::move(inp_j));
    }
}

void ShaderNode::deserialize(const nlohmann::json& j) {
    if (j.contains("x")) pos_x = j["x"].get<float>();
    if (j.contains("y")) pos_y = j["y"].get<float>();

    if (j.contains("inputs")) {
        const auto& inputs_arr = j["inputs"];
        for (int32_t i = 0; i < input_count() && i < static_cast<int32_t>(inputs_arr.size()); ++i) {
            const auto& inp_j = inputs_arr[i];
            if (inp_j.contains("default")) {
                const auto& def = inp_j["default"];
                auto& inp = get_input(i);
                for (int k = 0; k < 4 && k < static_cast<int>(def.size()); ++k) {
                    inp.default_value[k] = def[k].get<float>();
                }
            }
        }
    }
}

} // namespace uz::sg
