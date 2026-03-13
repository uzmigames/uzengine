/// UzEngine Module Registry — Implementation
///
/// Stores module registrar callbacks in a function-local static vector to avoid
/// static initialization order fiasco. Callbacks are invoked once during engine
/// loop initialization.

#include "module_registry.h"

#include <vector>

namespace uz::detail {

/// Returns the singleton list of registrar callbacks.
///
/// Uses a function-local static to guarantee construction before first use,
/// avoiding the static initialization order fiasco.
static std::vector<ModuleRegistrarFn>& GetRegistrars() {
    static std::vector<ModuleRegistrarFn> registrars;
    return registrars;
}

void AddModuleRegistrar(ModuleRegistrarFn fn) {
    GetRegistrars().push_back(fn);
}

void RunModuleRegistrars(EngineLoop& loop) {
    for (auto& fn : GetRegistrars()) {
        fn(loop);
    }
}

} // namespace uz::detail
