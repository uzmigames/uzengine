// SystemScheduler — tick-group-aware dependency scheduler.
//
// Implementation uses Kahn's algorithm per tick group:
// 1. Partition entries by TickGroup.
// 2. Within each group, build a directed graph from before/after constraints.
// 3. BFS from zero-in-degree nodes to produce a linear order.
// 4. If any node remains unvisited, a cycle exists and build() fails.
//
// Registration order is used as a stable tie-breaker when multiple nodes
// have zero in-degree simultaneously (FIFO queue preserves insertion order).

#include "engine/ecs/system_scheduler.h"

#include <algorithm>
#include <cstdint>
#include <queue>
#include <string>
#include <unordered_map>
#include <vector>

namespace uz {

// ---------------------------------------------------------------------------
// add_system
// ---------------------------------------------------------------------------
void SystemScheduler::add_system(SystemDesc desc) {
    if (!desc.system) {
        return;
    }

    desc.system->on_register();

    entries_.push_back(Entry{
        desc.system,
        desc.group,
        std::move(desc.before),
        std::move(desc.after),
    });
}

// ---------------------------------------------------------------------------
// build — topological sort per tick group
// ---------------------------------------------------------------------------
bool SystemScheduler::build() {
    execution_order_.clear();
    built_ = false;

    // Process groups in enum order.
    for (uint8_t g = 0; g < static_cast<uint8_t>(TickGroup::Count_); ++g) {
        const auto group = static_cast<TickGroup>(g);

        // Collect indices of entries in this group.
        std::vector<size_t> group_indices;
        for (size_t i = 0; i < entries_.size(); ++i) {
            if (entries_[i].group == group) {
                group_indices.push_back(i);
            }
        }
        if (group_indices.empty()) {
            continue;
        }

        // Map system name -> local index within this group.
        std::unordered_map<std::string, size_t> name_to_local;
        for (size_t local = 0; local < group_indices.size(); ++local) {
            const auto* sys = entries_[group_indices[local]].system;
            name_to_local[sys->name()] = local;
        }

        const size_t n = group_indices.size();

        // Adjacency list and in-degree for this group's sub-graph.
        std::vector<std::vector<size_t>> adj(n);
        std::vector<uint32_t> in_degree(n, 0);

        // Build edges from before/after constraints.
        for (size_t local = 0; local < n; ++local) {
            const auto& entry = entries_[group_indices[local]];

            // "A.before = [B]" means edge A -> B (A must run before B).
            for (const auto& target_name : entry.before) {
                auto it = name_to_local.find(target_name);
                if (it != name_to_local.end()) {
                    adj[local].push_back(it->second);
                    ++in_degree[it->second];
                }
                // Names not in this group are silently ignored — they may
                // refer to systems in other groups (cross-group deps are
                // meaningless since groups are strictly ordered).
            }

            // "A.after = [C]" means edge C -> A (C must run before A).
            for (const auto& dep_name : entry.after) {
                auto it = name_to_local.find(dep_name);
                if (it != name_to_local.end()) {
                    adj[it->second].push_back(local);
                    ++in_degree[local];
                }
            }
        }

        // Kahn's BFS — seed with zero-in-degree nodes in registration order.
        std::queue<size_t> q;
        for (size_t local = 0; local < n; ++local) {
            if (in_degree[local] == 0) {
                q.push(local);
            }
        }

        std::vector<size_t> sorted;
        sorted.reserve(n);

        while (!q.empty()) {
            const size_t cur = q.front();
            q.pop();
            sorted.push_back(cur);

            for (size_t neighbor : adj[cur]) {
                --in_degree[neighbor];
                if (in_degree[neighbor] == 0) {
                    q.push(neighbor);
                }
            }
        }

        if (sorted.size() != n) {
            // Cycle detected — abort.
            execution_order_.clear();
            return false;
        }

        for (size_t local : sorted) {
            execution_order_.push_back(entries_[group_indices[local]].system);
        }
    }

    built_ = true;
    return true;
}

// ---------------------------------------------------------------------------
// tick
// ---------------------------------------------------------------------------
void SystemScheduler::tick(float dt) {
    if (!built_) {
        return;
    }
    for (auto* sys : execution_order_) {
        sys->tick(dt);
    }
}

// ---------------------------------------------------------------------------
// shutdown — reverse execution order
// ---------------------------------------------------------------------------
void SystemScheduler::shutdown() {
    for (auto it = execution_order_.rbegin(); it != execution_order_.rend(); ++it) {
        (*it)->on_shutdown();
    }
    execution_order_.clear();
    built_ = false;
}

// ---------------------------------------------------------------------------
// get_execution_order
// ---------------------------------------------------------------------------
std::vector<const char*> SystemScheduler::get_execution_order() const {
    std::vector<const char*> names;
    names.reserve(execution_order_.size());
    for (const auto* sys : execution_order_) {
        names.push_back(sys->name());
    }
    return names;
}

// ---------------------------------------------------------------------------
// system_count
// ---------------------------------------------------------------------------
size_t SystemScheduler::system_count() const {
    return entries_.size();
}

} // namespace uz
