// shader_graph_manager.h -- Manages shader graph instances: load, save, cache.
//
// Handles .uzshader file format, hot-reload on file change, and integration
// with the MaterialManager (graph:path shader names).

#pragma once

#include "engine/renderer/shader_graph/shader_graph.h"
#include "engine/renderer/shader_graph/graph_ops.h"
#include "engine/renderer/shader_graph/node_registry.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace uz::sg {

struct ManagedGraph {
    std::unique_ptr<ShaderGraph> graph;
    std::unique_ptr<GraphOps> ops;
    std::string file_path;
    bool dirty = false;  // Has unsaved changes.
};

class ShaderGraphManager {
public:
    ShaderGraphManager() = default;

    // Load a shader graph from a .uzshader file.
    // Returns the graph ID (file path used as key), or empty on failure.
    std::string load(const std::string& path);

    // Save a shader graph to its file path.
    bool save(const std::string& path);

    // Create a new empty graph with a MaterialOutput node.
    std::string create_new(const std::string& path);

    // Get a managed graph by path.
    ManagedGraph* get(const std::string& path);

    // Check if a graph exists.
    bool has(const std::string& path) const;

    // Remove a graph from the cache.
    void unload(const std::string& path);

    // Hot-reload: re-read from disk if modified externally.
    bool reload(const std::string& path);

    // List all loaded graph paths.
    std::vector<std::string> list() const;

private:
    static NodeFactory make_factory();

    std::unordered_map<std::string, ManagedGraph> graphs_;
};

} // namespace uz::sg
