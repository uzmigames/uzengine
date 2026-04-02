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
        // ── Already created — sync emitting state + world position ──────
        if (emitterComp.runtimeEmitterIdx >= 0) {
            const auto idx = static_cast<size_t>(emitterComp.runtimeEmitterIdx);
            if (idx < m_runtimeEmitters.size() && m_runtimeEmitters[idx]) {
                m_runtimeEmitters[idx]->isEmitting = emitterComp.emitting;
                // Sync world position from entity transform.
                if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
                    m_runtimeEmitters[idx]->worldPosition = sc->GetPosition();
                }
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

            // Instantiate first emitter into the already-created slot.
            if (!UzfxLoader::InstantiateEmitter(desc.emitters[0], *emitter, m_device,
                                                 activeVFX->GetBudget())) {
                UZ_LOG(VFX, Error,
                       "SyncVfxEmitters: failed to instantiate emitter from '{}' — skipping",
                       emitterComp.assetPath);
                continue;
            }

            emitter->graphPath = emitterComp.assetPath;
            Vec3 worldPos{};
            if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
                worldPos = sc->GetPosition();
            }
            emitter->worldPosition = worldPos;

            // GPU init: upload free list + transition buffers to UAV.
            // Done here (before render graph) to avoid WaitIdle deadlock.
            {
                auto* initCmd = m_device->CreateCommandList(RHIQueueType::Graphics);
                if (initCmd) {
                    initCmd->Begin();
                    emitter->buffer.UploadInitialState(m_device, initCmd);
                    emitter->buffer.TransitionAllToUAV(initCmd);
                    initCmd->End();
                    initCmd->Execute();
                    m_device->WaitIdle();
                    m_device->DestroyCommandList(initCmd);
                    for (auto h : emitter->buffer.pendingStagingBuffers) {
                        m_device->DestroyBuffer(h);
                    }
                    emitter->buffer.pendingStagingBuffers.clear();
                }
            }
            emitter->needsInitialUpload = false;

            // Multi-emitter: instantiate additional emitters from the same system.
            for (size_t ei = 1; ei < desc.emitters.size(); ++ei) {
                auto extra = std::make_unique<ParticleEmitter>();
                if (!UzfxLoader::InstantiateEmitter(desc.emitters[ei], *extra, m_device,
                                                     activeVFX->GetBudget())) {
                    UZ_LOG(VFX, Warning,
                           "SyncVfxEmitters: failed to instantiate sub-emitter [{}] '{}' — skipping",
                           ei, desc.emitters[ei].name);
                    continue;
                }
                extra->graphPath = emitterComp.assetPath;
                extra->worldPosition = worldPos;
                extra->isEmitting = emitterComp.emitting;
                // GPU init for sub-emitter
                {
                    auto* initCmd = m_device->CreateCommandList(RHIQueueType::Graphics);
                    if (initCmd) {
                        initCmd->Begin();
                        extra->buffer.UploadInitialState(m_device, initCmd);
                        extra->buffer.TransitionAllToUAV(initCmd);
                        initCmd->End();
                        initCmd->Execute();
                        m_device->WaitIdle();
                        m_device->DestroyCommandList(initCmd);
                        for (auto h : extra->buffer.pendingStagingBuffers) {
                            m_device->DestroyBuffer(h);
                        }
                        extra->buffer.pendingStagingBuffers.clear();
                    }
                }
                extra->needsInitialUpload = false;
                activeVFX->RegisterEmitter(extra.get());
                m_runtimeEmitters.push_back(std::move(extra));
            }
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

            // Mark emitter as needing initial GPU state upload.
            // The upload happens in RegisterParticlePasses when the render
            // command list is available (avoids creating a temp cmd list here
            // which can deadlock with the fence on the direct queue).
            emitter->needsInitialUpload = true;
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
