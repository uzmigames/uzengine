/// VFX Module Stack Materialization — turns serialized stack data into runtime modules.
///
/// `DeserializeEmitter` populates `ParticleEmitter::stack` with
/// `VfxModuleInstance` records (type names + raw JSON config) but does not
/// instantiate the C++ module objects — that requires a `ModuleRegistry`,
/// which the serializer keeps no dependency on. This header provides the
/// missing step: walks `e.stack`, creates each module via the registry,
/// applies its JSON config through the virtual `IVFXModule::ApplyJsonConfig`,
/// and routes the result to the correct slot of `e.moduleStack`.
///
/// # Usage
/// ```cpp
/// ParticleEmitter e;
/// DeserializeEmitter(jsonStr, e);
/// ApplyEmitterStackToModuleStack(e, registry);
/// // e.moduleStack now has runtime IVFXModule instances ready to dispatch.
/// ```

#pragma once

#include "particle_emitter.h"
#include "engine/vfx/modules/module_registry.h"
#include "engine/vfx/stack/vfx_stage.h"

#include <json.hpp>
#include <utility>

namespace uz {

namespace detail {

/// Parses `mod.configJson` and forwards it to `IVFXModule::ApplyJsonConfig`.
///
/// Empty / malformed config is tolerated — the module receives an empty
/// object and falls back to its constructor defaults. Any exception thrown
/// by `nlohmann::json::parse` is caught locally; modules with bad config
/// load with defaults rather than aborting the whole emitter.
inline void ApplyConfigJson(const VfxModuleInstance& mod, IVFXModule& module) {
    if (mod.configJson.empty()) {
        module.ApplyJsonConfig(nlohmann::json::object());
        return;
    }
    try {
        const auto parsed = nlohmann::json::parse(mod.configJson);
        module.ApplyJsonConfig(parsed);
    } catch (...) {
        module.ApplyJsonConfig(nlohmann::json::object());
    }
}

} // namespace detail

/// Materializes runtime `IVFXModule` instances from `e.stack` into
/// `e.moduleStack`, using `registry` to map `moduleTypeName` strings to
/// concrete classes. Existing modules in `e.moduleStack` are cleared first
/// so the function is idempotent — calling it twice does not duplicate
/// modules.
///
/// Stage routing:
/// - `VfxStage::ParticleSpawn`  → `EmitterModuleStack::AddSpawnModule`
/// - `VfxStage::ParticleUpdate` → `EmitterModuleStack::AddUpdateModule`
/// - `VfxStage::Render`         → `EmitterModuleStack::SetRenderModule`
///   (only the LAST render module wins; multiple render modules per stage
///   are an authoring error but tolerated to avoid silent drops)
/// - All other stages are skipped silently — they describe higher-level
///   system / emitter behaviour that doesn't map to per-particle modules.
///
/// Modules whose `enabled` flag is `false` are skipped. Modules with a type
/// name not registered in `registry` log a warning (via the registry) and
/// the slot stays empty. Render stage with no module results in
/// `renderModule == nullptr`, which downstream code treats as "render
/// disabled" — matching pre-stack legacy behaviour.
inline void ApplyEmitterStackToModuleStack(ParticleEmitter& e,
                                            const ModuleRegistry& registry) {
    e.moduleStack.Clear();

    for (const auto& entry : e.stack.Entries()) {
        for (const auto& mod : entry.modules) {
            if (!mod.enabled) continue;
            if (mod.moduleTypeName.empty()) continue;

            auto instance = registry.Create(mod.moduleTypeName.c_str());
            if (!instance) {
                // Registry already logged the failure.
                continue;
            }

            detail::ApplyConfigJson(mod, *instance);

            switch (entry.stage) {
            case VfxStage::ParticleSpawn:
                e.moduleStack.AddSpawnModule(std::move(instance));
                break;
            case VfxStage::ParticleUpdate:
                e.moduleStack.AddUpdateModule(std::move(instance));
                break;
            case VfxStage::Render:
                e.moduleStack.SetRenderModule(std::move(instance));
                break;
            default:
                // Other stages (System*, Emitter*) are reserved for higher-
                // level orchestration that does not flow through the per-
                // particle module slots.
                break;
            }
        }
    }
}

} // namespace uz
