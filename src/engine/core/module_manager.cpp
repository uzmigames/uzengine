#include "engine/core/module_manager.h"

#include <algorithm>
#include <cstdio>
#include <stdexcept>

namespace uz {

// ============================================================================
// ~ModuleManager
// ============================================================================

ModuleManager::~ModuleManager() {
    // Safety net: if the user forgot to call shutdown_all(), do it now.
    if (started_) {
        shutdown_all();
    }
}

// ============================================================================
// register_module
// ============================================================================

void ModuleManager::register_module(ModuleInterface* mod) {
    assert(mod != nullptr && "ModuleManager: cannot register null module");
    assert(!started_ && "ModuleManager: cannot register modules after startup");

    // Check for duplicate registration.
    for (const auto& entry : entries_) {
        if (entry.mod == mod) {
            std::fprintf(stderr, "[ModuleManager] WARNING: module '%s' already registered, ignoring\n",
                         mod->name());
            return;
        }
    }

    entries_.push_back({mod, ModuleState::Registered});
    sorted_ = false;
}

// ============================================================================
// startup_all
// ============================================================================

bool ModuleManager::startup_all() {
    if (entries_.empty()) return false;
    assert(!started_ && "ModuleManager: startup_all() called twice");

    // Sort by priority (descending) — highest priority starts first.
    if (!sorted_) {
        std::stable_sort(entries_.begin(), entries_.end(),
            [](const Entry& a, const Entry& b) {
                return a.mod->priority() > b.mod->priority();
            });
        sorted_ = true;
    }

    started_ = true;

    for (size_t i = 0; i < entries_.size(); ++i) {
        Entry& entry = entries_[i];
        entry.state = ModuleState::Starting;

        try {
            std::fprintf(stderr, "[ModuleManager] Starting module '%s' (priority=%d)\n",
                         entry.mod->name(), entry.mod->priority());
            entry.mod->startup();
            entry.state = ModuleState::Running;
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[ModuleManager] ERROR: module '%s' startup failed: %s\n",
                         entry.mod->name(), e.what());
            entry.state = ModuleState::Stopped;

            // Reverse-shutdown all modules that already started (indices 0..i-1).
            for (size_t j = i; j > 0; --j) {
                Entry& prev = entries_[j - 1];
                if (prev.state == ModuleState::Running) {
                    prev.state = ModuleState::ShuttingDown;
                    try {
                        std::fprintf(stderr, "[ModuleManager] Rolling back module '%s'\n",
                                     prev.mod->name());
                        prev.mod->shutdown();
                    } catch (const std::exception& se) {
                        std::fprintf(stderr, "[ModuleManager] ERROR during rollback of '%s': %s\n",
                                     prev.mod->name(), se.what());
                    } catch (...) {
                        std::fprintf(stderr, "[ModuleManager] ERROR during rollback of '%s': unknown exception\n",
                                     prev.mod->name());
                    }
                    prev.state = ModuleState::Stopped;
                }
            }

            started_ = false;
            throw; // Re-throw the original exception
        } catch (...) {
            std::fprintf(stderr, "[ModuleManager] ERROR: module '%s' startup failed with unknown exception\n",
                         entry.mod->name());
            entry.state = ModuleState::Stopped;

            // Same reverse-shutdown as above.
            for (size_t j = i; j > 0; --j) {
                Entry& prev = entries_[j - 1];
                if (prev.state == ModuleState::Running) {
                    prev.state = ModuleState::ShuttingDown;
                    try {
                        std::fprintf(stderr, "[ModuleManager] Rolling back module '%s'\n",
                                     prev.mod->name());
                        prev.mod->shutdown();
                    } catch (...) {
                        std::fprintf(stderr, "[ModuleManager] ERROR during rollback of '%s'\n",
                                     prev.mod->name());
                    }
                    prev.state = ModuleState::Stopped;
                }
            }

            started_ = false;
            throw;
        }
    }

    return true;
}

// ============================================================================
// shutdown_all
// ============================================================================

void ModuleManager::shutdown_all() {
    if (!started_) return;

    // Shutdown in REVERSE startup order (LIFO).
    for (auto it = entries_.rbegin(); it != entries_.rend(); ++it) {
        Entry& entry = *it;
        if (entry.state != ModuleState::Running) continue;

        entry.state = ModuleState::ShuttingDown;
        try {
            std::fprintf(stderr, "[ModuleManager] Shutting down module '%s'\n",
                         entry.mod->name());
            entry.mod->shutdown();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "[ModuleManager] ERROR: module '%s' shutdown failed: %s\n",
                         entry.mod->name(), e.what());
        } catch (...) {
            std::fprintf(stderr, "[ModuleManager] ERROR: module '%s' shutdown failed with unknown exception\n",
                         entry.mod->name());
        }
        entry.state = ModuleState::Stopped;
    }

    started_ = false;
}

// ============================================================================
// module_state
// ============================================================================

ModuleState ModuleManager::module_state(std::string_view name) const {
    for (const auto& entry : entries_) {
        if (std::string_view(entry.mod->name()) == name) {
            return entry.state;
        }
    }
    return ModuleState::Stopped;
}

// ============================================================================
// running_count
// ============================================================================

size_t ModuleManager::running_count() const {
    size_t count = 0;
    for (const auto& entry : entries_) {
        if (entry.state == ModuleState::Running) ++count;
    }
    return count;
}

// ============================================================================
// startup_order
// ============================================================================

std::vector<const char*> ModuleManager::startup_order() const {
    std::vector<const char*> order;
    order.reserve(entries_.size());
    for (const auto& entry : entries_) {
        order.push_back(entry.mod->name());
    }
    return order;
}

} // namespace uz
