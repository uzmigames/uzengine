// node_registry.cpp -- NodeRegistry singleton implementation.

#include "engine/renderer/shader_graph/node_registry.h"

#include <algorithm>

namespace uz::sg {

NodeRegistry& NodeRegistry::instance() {
    static NodeRegistry reg;
    return reg;
}

void NodeRegistry::register_node(const std::string& name, NodeCategory cat,
                                  std::function<std::unique_ptr<ShaderNode>()> factory) {
    // Overwrite if already registered (supports hot-reload).
    for (auto& t : types_) {
        if (t.name == name) {
            t.category = cat;
            t.factory = std::move(factory);
            return;
        }
    }
    types_.push_back({name, cat, std::move(factory)});
}

std::unique_ptr<ShaderNode> NodeRegistry::create(const std::string& name) const {
    auto* info = find(name);
    if (!info) return nullptr;
    auto node = info->factory();
    if (node) node->type_name = name;
    return node;
}

const NodeTypeInfo* NodeRegistry::find(const std::string& name) const {
    for (auto& t : types_) {
        if (t.name == name) return &t;
    }
    return nullptr;
}

std::vector<const NodeTypeInfo*> NodeRegistry::by_category(NodeCategory cat) const {
    std::vector<const NodeTypeInfo*> result;
    for (auto& t : types_) {
        if (t.category == cat) result.push_back(&t);
    }
    return result;
}

} // namespace uz::sg
