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
/// Forward edges: file -> files it includes (direct).
/// Reverse edges: file -> files that include it (transitive via BFS).
///
/// # Thread Safety
/// NOT thread-safe. Must be externally synchronized if accessed from multiple threads.
///
/// # Lifetime
/// Standalone — does not hold references to other subsystems.
///
/// # Examples
/// ```cpp
/// ShaderDependencyGraph graph;
/// graph.SetDependencies("/engine/pbr.hlsl",
///     {"/engine/common.hlsli", "/engine/brdf.hlsli"});
/// graph.SetDependencies("/engine/shadow.hlsl",
///     {"/engine/common.hlsli"});
///
/// // When common.hlsli changes, find all affected root shaders:
/// auto dependents = graph.GetDependents("/engine/common.hlsli");
/// // dependents contains "/engine/pbr.hlsl" and "/engine/shadow.hlsl"
/// ```
class ShaderDependencyGraph {
public:
    ShaderDependencyGraph() = default;

    /// Sets the direct dependencies (includes) for a shader file.
    ///
    /// Replaces any existing dependency set for this file and rebuilds the
    /// reverse graph edges accordingly.
    ///
    /// # Arguments
    /// * `file` — Virtual path of the shader file.
    /// * `dependencies` — List of virtual paths this file directly includes.
    void SetDependencies(const std::string& file,
                          const std::vector<std::string>& dependencies);

    /// Removes a file and all its forward/reverse edges from the graph.
    ///
    /// # Arguments
    /// * `file` — Virtual path to remove.
    void RemoveFile(const std::string& file);

    /// Returns all direct dependencies (includes) of a file.
    ///
    /// # Arguments
    /// * `file` — Virtual path to query.
    ///
    /// # Returns
    /// Direct include paths. Empty vector if the file is not in the graph.
    [[nodiscard]] std::vector<std::string> GetDependencies(const std::string& file) const;

    /// Returns all files that transitively depend on the given file.
    ///
    /// This is the key query for hot-reload: "which root shaders need
    /// recompilation when this file changes?" Uses BFS over the reverse graph.
    ///
    /// # Arguments
    /// * `file` — Virtual path of the changed file.
    ///
    /// # Returns
    /// All transitive dependents (not including `file` itself). Order is unspecified.
    [[nodiscard]] std::vector<std::string> GetDependents(const std::string& file) const;

    /// Returns all files that directly depend on the given file (one level up).
    ///
    /// # Arguments
    /// * `file` — Virtual path to query.
    ///
    /// # Returns
    /// Only immediate (non-transitive) reverse dependents.
    [[nodiscard]] std::vector<std::string> GetDirectDependents(const std::string& file) const;

    /// Returns true if the graph contains any edges for this file.
    ///
    /// # Arguments
    /// * `file` — Virtual path to check.
    [[nodiscard]] bool Contains(const std::string& file) const;

    /// Returns the total number of tracked files (nodes in forward graph).
    [[nodiscard]] size_t GetFileCount() const;

    /// Returns the total number of edges in the forward graph.
    [[nodiscard]] size_t GetEdgeCount() const;

    /// Clears all tracked dependencies (both forward and reverse graphs).
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
