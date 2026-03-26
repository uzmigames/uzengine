/// UzEngine Scene Renderer — VfxEmitterComponent to ParticleEmitter sync.
///
/// Creates/updates/destroys ParticleEmitter instances based on
/// VfxEmitterComponent ECS entities. Called once per frame from
/// RenderFrame() before VFXSystem::Tick().

#include "scene_renderer.h"

#include "components.h"

#include "engine/asset/formats/uzfx_loader.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "log.h"
#include "profiler.h"
#include "rhi.h"
#include "vfs.h"
#include "world.h"

#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// SyncVfxEmitters
// ─────────────────────────────────────────────────────────────────────────────

void SceneRenderer::SyncVfxEmitters(World* world, IVFS* vfs) {
    UZ_PROFILE_FUNCTION();

    if (!world) return;

    VFXSystem* activeVFX = m_vfxSystemOverride ? m_vfxSystemOverride : m_vfxSystem.get();
    if (!activeVFX || !activeVFX->IsInitialized()) return;

    auto& registry = world->GetRegistry();
    auto view = registry.view<VfxEmitterComponent>();

    for (auto [enttEntity, emitterComp] : view.each()) {
        // ── Already created — just sync emitting state ──────────────────
        if (emitterComp.runtimeEmitterIdx >= 0) {
            const auto idx = static_cast<size_t>(emitterComp.runtimeEmitterIdx);
            if (idx < m_runtimeEmitters.size() && m_runtimeEmitters[idx]) {
                m_runtimeEmitters[idx]->isEmitting = emitterComp.emitting;
            }
            continue;
        }

        // ── New component — create a ParticleEmitter ────────────────────
        auto emitter = std::make_unique<ParticleEmitter>();

        if (!emitterComp.assetPath.empty() && vfs) {
            // Asset-driven: load .uzfx definition from VFS
            auto fileData = vfs->Read(emitterComp.assetPath);
            if (fileData.empty()) {
                UZ_LOG(VFX, Warning,
                       "SyncVfxEmitters: failed to read asset '{}' — skipping",
                       emitterComp.assetPath);
                continue;
            }

            std::string jsonStr(fileData.begin(), fileData.end());
            UzfxDesc desc = UzfxLoader::Load(jsonStr);
            if (desc.name.empty()) {
                UZ_LOG(VFX, Warning,
                       "SyncVfxEmitters: failed to parse asset '{}' — skipping",
                       emitterComp.assetPath);
                continue;
            }

            if (!UzfxLoader::Instantiate(desc, *emitter, m_device,
                                          activeVFX->GetBudget())) {
                UZ_LOG(VFX, Error,
                       "SyncVfxEmitters: failed to instantiate emitter from '{}' — skipping",
                       emitterComp.assetPath);
                continue;
            }

            emitter->graphPath = emitterComp.assetPath;
        } else {
            // Inline parameters: configure from component fields
            emitter->maxParticles    = emitterComp.maxParticles;
            emitter->emissionRate    = emitterComp.emissionRate;
            emitter->lifetime        = emitterComp.lifetime;
            emitter->gravity         = emitterComp.gravity;
            emitter->damping         = emitterComp.damping;
            emitter->spawnVelocity   = emitterComp.spawnVelocity;
            emitter->isEmitting      = emitterComp.emitting;

            if (!emitter->Initialize(m_device, activeVFX->GetBudget())) {
                UZ_LOG(VFX, Error,
                       "SyncVfxEmitters: failed to initialize inline emitter — skipping");
                continue;
            }
        }

        emitter->isEmitting = emitterComp.emitting;

        // Register with VFXSystem for per-frame simulation
        activeVFX->RegisterEmitter(emitter.get());

        // Store in our owned vector and record the index in the component
        const auto newIdx = static_cast<int32_t>(m_runtimeEmitters.size());
        m_runtimeEmitters.push_back(std::move(emitter));
        emitterComp.runtimeEmitterIdx = newIdx;

        UZ_LOG(VFX, Info,
               "SyncVfxEmitters: created emitter idx={} (asset='{}')",
               newIdx,
               emitterComp.assetPath.empty() ? "<inline>" : emitterComp.assetPath);
    }
}

} // namespace uz
