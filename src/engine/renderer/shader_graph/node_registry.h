// node_registry.h -- Factory registry for shader graph node types.
//
// All node types must be registered before use. The registry provides
// factory creation by type name and category-filtered listing.

#pragma once

#include "engine/renderer/shader_graph/shader_node.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace uz::sg {

// Node category for grouping in UI / queries.
enum class NodeCategory : uint8_t {
    Output,
    Constant,
    Parameter,
    Math,
    Texture,
    Utility,
};

struct NodeTypeInfo {
    std::string   name;
    NodeCategory  category;
    std::function<std::unique_ptr<ShaderNode>()> factory;
};

class NodeRegistry {
public:
    static NodeRegistry& instance();

    void register_node(const std::string& name, NodeCategory cat,
                       std::function<std::unique_ptr<ShaderNode>()> factory);

    std::unique_ptr<ShaderNode> create(const std::string& name) const;
    const NodeTypeInfo* find(const std::string& name) const;
    const std::vector<NodeTypeInfo>& all() const { return types_; }
    std::vector<const NodeTypeInfo*> by_category(NodeCategory cat) const;

    // Reset for testing.
    void clear() { types_.clear(); }

private:
    NodeRegistry() = default;
    std::vector<NodeTypeInfo> types_;
};

// Registration macro — call in .cpp file at namespace scope.
#define UZ_REGISTER_SHADER_NODE(NodeClass, Name, Category)                  \
    static struct NodeClass##_Registrar {                                    \
        NodeClass##_Registrar() {                                           \
            uz::sg::NodeRegistry::instance().register_node(                 \
                Name, Category,                                             \
                []() -> std::unique_ptr<uz::sg::ShaderNode> {               \
                    return std::make_unique<NodeClass>();                    \
                });                                                         \
        }                                                                   \
    } s_##NodeClass##_registrar;

} // namespace uz::sg
