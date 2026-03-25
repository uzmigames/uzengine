/// EmitterModuleStack implementation -- per-emitter module management.

#include "emitter_module_stack.h"

#include "engine/logging/log.h"

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

// ---------------------------------------------------------------------------
// Mutators
// ---------------------------------------------------------------------------

void EmitterModuleStack::AddSpawnModule(std::unique_ptr<IVFXModule> mod)
{
    if (!mod) {
        UZ_LOG(VFX, Warning, "EmitterModuleStack::AddSpawnModule: null module ignored");
        return;
    }
    UZ_LOG(VFX, Info, "EmitterModuleStack: added spawn module '{}'", mod->GetName());
    spawnModules.push_back(std::move(mod));
    dirty = true;
}

void EmitterModuleStack::AddUpdateModule(std::unique_ptr<IVFXModule> mod)
{
    if (!mod) {
        UZ_LOG(VFX, Warning, "EmitterModuleStack::AddUpdateModule: null module ignored");
        return;
    }
    UZ_LOG(VFX, Info, "EmitterModuleStack: added update module '{}'", mod->GetName());
    updateModules.push_back(std::move(mod));
    dirty = true;
}

void EmitterModuleStack::SetRenderModule(std::unique_ptr<IVFXModule> mod)
{
    if (mod) {
        UZ_LOG(VFX, Info, "EmitterModuleStack: set render module '{}'", mod->GetName());
    } else {
        UZ_LOG(VFX, Info, "EmitterModuleStack: cleared render module");
    }
    renderModule = std::move(mod);
    dirty = true;
}

void EmitterModuleStack::Clear()
{
    spawnModules.clear();
    updateModules.clear();
    renderModule.reset();
    composedUpdateHLSL.clear();
    composedSpawnHLSL.clear();
    composedCacheKey = 0;
    dirty = true;
}

// ---------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------

std::vector<IVFXModule*> EmitterModuleStack::GetUpdateModulePtrs() const
{
    std::vector<IVFXModule*> ptrs;
    ptrs.reserve(updateModules.size());
    for (const auto& mod : updateModules) {
        ptrs.push_back(mod.get());
    }
    return ptrs;
}

std::vector<IVFXModule*> EmitterModuleStack::GetSpawnModulePtrs() const
{
    std::vector<IVFXModule*> ptrs;
    ptrs.reserve(spawnModules.size());
    for (const auto& mod : spawnModules) {
        ptrs.push_back(mod.get());
    }
    return ptrs;
}

bool EmitterModuleStack::HasModules() const noexcept
{
    return !updateModules.empty() || !spawnModules.empty();
}

} // namespace uz
