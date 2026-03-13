/// UzEngine Subsystem Registry — Implementation
///
/// Provides dependency-aware initialization via Kahn's topological sort,
/// phase-grouped startup, reverse-order shutdown, and tick-group dispatch.

#include "subsystem_registry.h"

#include "../assertions/engine_assert.h"
#include "../logging/log.h"
#include "../profiling/profiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <queue>
#include <string>
#include <unordered_map>

UZ_DEFINE_LOG_CATEGORY(Lifecycle, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

SubsystemRegistry::~SubsystemRegistry() {
    // Ensure shutdown is called if the user forgot.
    if (!shutdownCalled_ && !initOrder_.empty()) {
        ShutdownAll();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void SubsystemRegistry::Register(std::unique_ptr<ISubsystem> subsystem) {
    UZ_CHECK(subsystem != nullptr);

    const char* name = subsystem->GetName();
    UZ_CHECK(name != nullptr);

    // Reject duplicate names.
    for (const auto& existing : subsystems_) {
        UZ_CHECKF(std::strcmp(existing->GetName(), name) != 0,
                   "Duplicate subsystem name: %s", name);
    }

    UZ_LOG(Lifecycle, Info, "Registered subsystem: '{}'", name);
    subsystems_.push_back(std::move(subsystem));
}

// ─────────────────────────────────────────────────────────────────────────────
// Initialization — topological sort + phase grouping
// ─────────────────────────────────────────────────────────────────────────────

[[nodiscard]] bool SubsystemRegistry::InitAll() {
    UZ_PROFILE_FUNCTION();

    if (subsystems_.empty()) {
        UZ_LOG(Lifecycle, Warning, "InitAll called with no registered subsystems");
        return true;
    }

    UZ_LOG(Lifecycle, Info, "Initializing {} subsystems...", subsystems_.size());

    // Build name -> index map for dependency resolution.
    std::unordered_map<std::string, size_t> nameToIndex;
    for (size_t i = 0; i < subsystems_.size(); ++i) {
        nameToIndex[subsystems_[i]->GetName()] = i;
    }

    const size_t count = subsystems_.size();

    // Build adjacency list and in-degree for Kahn's algorithm.
    // Edge: dependency -> dependent (dependency must come first).
    std::vector<std::vector<size_t>> adjacency(count);
    std::vector<size_t> inDegree(count, 0);

    for (size_t i = 0; i < count; ++i) {
        const auto deps = subsystems_[i]->GetDependencies();
        for (const char* depName : deps) {
            auto it = nameToIndex.find(depName);
            if (it == nameToIndex.end()) {
                UZ_LOG(Lifecycle, Error,
                       "Subsystem '{}' depends on '{}' which is not registered",
                       subsystems_[i]->GetName(), depName);
                return false;
            }
            adjacency[it->second].push_back(i);
            ++inDegree[i];
        }
    }

    // Kahn's algorithm: process nodes with zero in-degree.
    std::queue<size_t> ready;
    for (size_t i = 0; i < count; ++i) {
        if (inDegree[i] == 0) {
            ready.push(i);
        }
    }

    std::vector<size_t> sortedIndices;
    sortedIndices.reserve(count);

    while (!ready.empty()) {
        size_t current = ready.front();
        ready.pop();
        sortedIndices.push_back(current);

        for (size_t neighbor : adjacency[current]) {
            if (--inDegree[neighbor] == 0) {
                ready.push(neighbor);
            }
        }
    }

    if (sortedIndices.size() != count) {
        UZ_LOG(Lifecycle, Error, "Circular dependency detected among subsystems:");
        for (size_t i = 0; i < count; ++i) {
            if (inDegree[i] > 0) {
                UZ_LOG(Lifecycle, Error, "  - '{}' (in-degree={})",
                       subsystems_[i]->GetName(), inDegree[i]);
            }
        }
        return false;
    }

    // Stable-sort by InitPhase while preserving topological order within each phase.
    std::stable_sort(sortedIndices.begin(), sortedIndices.end(),
                     [this](size_t a, size_t b) {
                         return static_cast<uint8_t>(subsystems_[a]->GetInitPhase()) <
                                static_cast<uint8_t>(subsystems_[b]->GetInitPhase());
                     });

    // Initialize in sorted order.
    initOrder_.clear();
    initOrder_.reserve(count);

    for (size_t idx : sortedIndices) {
        ISubsystem* sub = subsystems_[idx].get();
        const char* name = sub->GetName();

        UZ_LOG(Lifecycle, Info, "Initializing '{}' (phase={})...",
               name, static_cast<int>(sub->GetInitPhase()));

        auto start = std::chrono::high_resolution_clock::now();
        const bool ok = sub->Init();
        auto end = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(end - start).count();

        if (!ok) {
            UZ_LOG(Lifecycle, Error, "Subsystem '{}' failed to initialize (took {:.2f}ms)", name, ms);

            // Roll back: shut down already-initialized subsystems in reverse order.
            UZ_LOG(Lifecycle, Info, "Rolling back {} initialized subsystems...", initOrder_.size());
            for (auto it = initOrder_.rbegin(); it != initOrder_.rend(); ++it) {
                UZ_LOG(Lifecycle, Info, "  Shutting down '{}'...", (*it)->GetName());
                (*it)->Shutdown();
            }
            initOrder_.clear();
            return false;
        }

        UZ_LOG(Lifecycle, Info, "Initialized '{}' in {:.2f}ms", name, ms);
        initOrder_.push_back(sub);
    }

    UZ_LOG(Lifecycle, Info, "All {} subsystems initialized successfully", count);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Shutdown
// ─────────────────────────────────────────────────────────────────────────────

void SubsystemRegistry::ShutdownAll() {
    UZ_PROFILE_FUNCTION();

    if (shutdownCalled_) {
        return;
    }
    shutdownCalled_ = true;

    if (initOrder_.empty()) {
        return;
    }

    UZ_LOG(Lifecycle, Info, "Shutting down {} subsystems...", initOrder_.size());

    // Reverse init order.
    for (auto it = initOrder_.rbegin(); it != initOrder_.rend(); ++it) {
        ISubsystem* sub = *it;
        UZ_LOG(Lifecycle, Info, "Shutting down '{}'...", sub->GetName());

        auto start = std::chrono::high_resolution_clock::now();
        sub->Shutdown();
        auto end = std::chrono::high_resolution_clock::now();

        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        UZ_LOG(Lifecycle, Info, "Shut down '{}' in {:.2f}ms", sub->GetName(), ms);
    }

    initOrder_.clear();
    UZ_LOG(Lifecycle, Info, "All subsystems shut down");
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick
// ─────────────────────────────────────────────────────────────────────────────

void SubsystemRegistry::RunTickGroup(TickGroup group, float deltaTime) {
    for (ISubsystem* sub : initOrder_) {
        if (sub->GetTickGroup() == group) {
            sub->Tick(deltaTime);
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Query
// ─────────────────────────────────────────────────────────────────────────────

ISubsystem* SubsystemRegistry::Find(const char* name) const {
    UZ_CHECK(name != nullptr);

    for (const auto& sub : subsystems_) {
        if (std::strcmp(sub->GetName(), name) == 0) {
            return sub.get();
        }
    }
    return nullptr;
}

} // namespace uz
