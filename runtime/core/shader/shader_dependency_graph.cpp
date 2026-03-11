/// ShaderDependencyGraph Implementation
///
/// Forward graph is directly set by the caller (from ShaderSourceManager's
/// include resolution). Reverse graph is rebuilt each time forward edges change.

#include "shader_dependency_graph.h"
#include "uz_log.h"

#include <algorithm>
#include <queue>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Set / Remove Dependencies
// ─────────────────────────────────────────────────────────────────────────────

void ShaderDependencyGraph::SetDependencies(const std::string& file,
                                              const std::vector<std::string>& dependencies) {
    // Remove old reverse edges for this file
    auto oldIt = m_forward.find(file);
    if (oldIt != m_forward.end()) {
        for (const auto& dep : oldIt->second) {
            auto revIt = m_reverse.find(dep);
            if (revIt != m_reverse.end()) {
                auto& vec = revIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), file), vec.end());
                if (vec.empty()) m_reverse.erase(revIt);
            }
        }
    }

    // Set new forward edges
    m_forward[file] = dependencies;

    // Add new reverse edges
    for (const auto& dep : dependencies) {
        m_reverse[dep].push_back(file);
    }

    UZ_LOG(IO, Trace, "ShaderDepGraph: '{}' depends on {} files", file, dependencies.size());
}

void ShaderDependencyGraph::RemoveFile(const std::string& file) {
    // Remove forward edges and their reverse counterparts
    auto fwdIt = m_forward.find(file);
    if (fwdIt != m_forward.end()) {
        for (const auto& dep : fwdIt->second) {
            auto revIt = m_reverse.find(dep);
            if (revIt != m_reverse.end()) {
                auto& vec = revIt->second;
                vec.erase(std::remove(vec.begin(), vec.end(), file), vec.end());
                if (vec.empty()) m_reverse.erase(revIt);
            }
        }
        m_forward.erase(fwdIt);
    }

    // Remove reverse edges where this file is a dependency
    auto revIt = m_reverse.find(file);
    if (revIt != m_reverse.end()) {
        m_reverse.erase(revIt);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Queries
// ─────────────────────────────────────────────────────────────────────────────

std::vector<std::string> ShaderDependencyGraph::GetDependencies(const std::string& file) const {
    auto it = m_forward.find(file);
    if (it == m_forward.end()) return {};
    return it->second;
}

std::vector<std::string> ShaderDependencyGraph::GetDirectDependents(const std::string& file) const {
    auto it = m_reverse.find(file);
    if (it == m_reverse.end()) return {};
    return it->second;
}

void ShaderDependencyGraph::CollectDependents(const std::string& file,
                                                std::unordered_set<std::string>& visited) const {
    auto it = m_reverse.find(file);
    if (it == m_reverse.end()) return;

    for (const auto& dependent : it->second) {
        if (visited.insert(dependent).second) {
            CollectDependents(dependent, visited);
        }
    }
}

std::vector<std::string> ShaderDependencyGraph::GetDependents(const std::string& file) const {
    std::unordered_set<std::string> visited;
    CollectDependents(file, visited);

    std::vector<std::string> result(visited.begin(), visited.end());
    std::sort(result.begin(), result.end());
    return result;
}

bool ShaderDependencyGraph::Contains(const std::string& file) const {
    return m_forward.count(file) > 0;
}

size_t ShaderDependencyGraph::GetFileCount() const {
    // Count unique files appearing in either direction
    std::unordered_set<std::string> files;
    for (const auto& [file, deps] : m_forward) {
        files.insert(file);
        for (const auto& dep : deps) files.insert(dep);
    }
    return files.size();
}

size_t ShaderDependencyGraph::GetEdgeCount() const {
    size_t count = 0;
    for (const auto& [file, deps] : m_forward) {
        count += deps.size();
    }
    return count;
}

void ShaderDependencyGraph::Clear() {
    m_forward.clear();
    m_reverse.clear();
}

} // namespace uz
