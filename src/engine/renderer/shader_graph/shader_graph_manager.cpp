// shader_graph_manager.cpp -- ShaderGraphManager implementation.

#include "engine/renderer/shader_graph/shader_graph_manager.h"
#include "engine/core/platform.h"

#include <nlohmann/json.hpp>
#include <cstdlib>

namespace uz::sg {

NodeFactory ShaderGraphManager::make_factory() {
    return [](const std::string& type) -> std::unique_ptr<ShaderNode> {
        return NodeRegistry::instance().create(type);
    };
}

std::string ShaderGraphManager::load(const std::string& path) {
    // Read file.
    size_t size = 0;
    char* data = platform::file_read_all(path.c_str(), &size);
    if (!data || size == 0) return {};

    std::string content(data, size);
    free(data);

    // Parse JSON.
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(content);
    } catch (...) {
        return {};
    }

    // Deserialize graph.
    auto graph = ShaderGraph::deserialize(j, make_factory());
    if (!graph) return {};

    ManagedGraph mg;
    mg.graph = std::move(graph);
    mg.ops = std::make_unique<GraphOps>(*mg.graph);
    mg.file_path = path;
    mg.dirty = false;

    graphs_[path] = std::move(mg);
    return path;
}

bool ShaderGraphManager::save(const std::string& path) {
    auto it = graphs_.find(path);
    if (it == graphs_.end()) return false;

    auto j = it->second.graph->serialize();
    std::string content = j.dump(2);

    bool ok = platform::file_write_all(path.c_str(), content.c_str(), content.size());
    if (ok) {
        it->second.dirty = false;
    }
    return ok;
}

std::string ShaderGraphManager::create_new(const std::string& path) {
    auto graph = std::make_unique<ShaderGraph>();
    auto ops = std::make_unique<GraphOps>(*graph);

    // Add a MaterialOutput node.
    ops->create_node("MaterialOutput", 400.f, 200.f);

    ManagedGraph mg;
    mg.graph = std::move(graph);
    mg.ops = std::move(ops);
    mg.file_path = path;
    mg.dirty = true;

    graphs_[path] = std::move(mg);
    return path;
}

ManagedGraph* ShaderGraphManager::get(const std::string& path) {
    auto it = graphs_.find(path);
    return it != graphs_.end() ? &it->second : nullptr;
}

bool ShaderGraphManager::has(const std::string& path) const {
    return graphs_.count(path) > 0;
}

void ShaderGraphManager::unload(const std::string& path) {
    graphs_.erase(path);
}

bool ShaderGraphManager::reload(const std::string& path) {
    unload(path);
    return !load(path).empty();
}

std::vector<std::string> ShaderGraphManager::list() const {
    std::vector<std::string> paths;
    paths.reserve(graphs_.size());
    for (auto& [path, _] : graphs_) {
        paths.push_back(path);
    }
    return paths;
}

} // namespace uz::sg
