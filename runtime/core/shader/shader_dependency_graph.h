/// UzEngine Shader Dependency Graph
///
/// Tracks forward (file → its includes) and reverse (file → all files that
/// include it) dependencies between shader source files.
///
/// # Purpose
/// When a shader file changes, the reverse graph instantly tells us every
/// root shader that needs recompilation — without scanning all sources.
///
/// # Usage
/// ```cpp
/// ShaderDependencyGraph graph;
/// graph.SetDependencies("/engine/pbr.hlsl", {"/engine/common.hlsli", "/engine/brdf.hlsli"});
///
/// // When common.hlsli changes:
/// auto dependents = graph.GetDependents("/engine/common.hlsli");
/// // dependents = {"/engine/pbr.hlsl"}
/// ```

#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace uz {

/// Bidirectional dependency graph for shader source files.
///
/// Forward edges: file → files it includes (direct).
/// Reverse edges: file → files that include it (transitive).
class ShaderDependencyGraph {
public:
    ShaderDependencyGraph() = default;

    /// Sets the direct dependencies (includes) for a shader file.
    ///
    /// Replaces any existing dependency set for this file.
    /// Automatically rebuilds the reverse graph.
    void SetDependencies(const std::string& file,
                          const std::vector<std::string>& dependencies);

    /// Removes a file and all its edges from the graph.
    void RemoveFile(const std::string& file);

    /// Returns all direct dependencies (includes) of a file.
    [[nodiscard]] std::vector<std::string> GetDependencies(const std::string& file) const;

    /// Returns all files that transitively depend on the given file.
    ///
    /// This is the key query for hot-reload: "which root shaders need
    /// recompilation when this file changes?"
    [[nodiscard]] std::vector<std::string> GetDependents(const std::string& file) const;

    /// Returns all files that directly depend on the given file (one level up).
    [[nodiscard]] std::vector<std::string> GetDirectDependents(const std::string& file) const;

    /// Returns true if the graph contains any edges for this file.
    [[nodiscard]] bool Contains(const std::string& file) const;

    /// Returns the total number of tracked files (nodes).
    [[nodiscard]] size_t GetFileCount() const;

    /// Returns the total number of edges in the forward graph.
    [[nodiscard]] size_t GetEdgeCount() const;

    /// Clears all tracked dependencies.
    void Clear();

private:
    /// Collects all transitive reverse dependents via BFS.
    void CollectDependents(const std::string& file,
                            std::unordered_set<std::string>& visited) const;

    /// Forward graph: file → its direct includes.
    std::unordered_map<std::string, std::vector<std::string>> m_forward;

    /// Reverse graph: file → files that directly include it.
    std::unordered_map<std::string, std::vector<std::string>> m_reverse;
};

} // namespace uz
