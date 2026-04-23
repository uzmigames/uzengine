/// UzEngine Scene Renderer — VfxEmitterComponent to ParticleEmitter sync.
///
/// Creates/updates/destroys ParticleEmitter instances based on
/// VfxEmitterComponent ECS entities. Called once per frame from
/// RenderFrame() before VFXSystem::Tick().

#include "scene_renderer.h"

#include "components.h"

#include "engine/animation/skeletal_mesh_instance.h"
#include "engine/animation/skeletal_mesh_subsystem.h"
#include "engine/animation/skeleton.h"
#include "engine/asset/formats/uzfx_loader.h"
#include "engine/ecs/mesh/skeletal_mesh_component.h"
#include "engine/renderer/upload/texture_loader.h"
#include "engine/vfx/vfx_system.h"
#include "engine/vfx/components/particle_emitter.h"
#include "engine/vfx/lod/vfx_frustum_cull.h"
#include "engine/vfx/lod/vfx_hzb_occlusion.h"
#include "engine/vfx/material/vfx_material.h"
#include "engine/vfx/system/vfx_system.h"
#include "engine/vfx/system/vfx_system_instance.h"
#include "camera.h"
#include "camera_manager.h"
#include "log.h"
#include "profiler.h"
#include "rhi.h"
#include "vfs.h"
#include "world.h"

#include <string>
#include <unordered_set>

namespace uz {

namespace {

/// Result of resolving the attach target for a VFX emitter.
///
/// `resolved` is true when the attach entity was found and produced a usable
/// transform (either from a skeletal bone or from the entity's SceneComponent).
/// `position` is the resolved world-space position. `boneMatched` is true only
/// when `attachBoneName` was non-empty AND resolved to a real skeleton bone —
/// the UI uses this to distinguish "parent attach" from "bone socket attach".
struct AttachResolveResult {
    bool resolved    = false;
    bool boneMatched = false;
    Vec3 position    = {0.0f, 0.0f, 0.0f};
};

/// One-shot warning dedup keyed on `(entityId, entityGeneration, bone hash)`.
/// Prevents log spam when the attach target is misconfigured — we print the
/// warning exactly once per (target, bone) combo per process lifetime.
std::unordered_set<uint64_t>& AttachWarnedKeys() {
    static std::unordered_set<uint64_t> set;
    return set;
}

/// Composes a 64-bit dedup key from the attach entity + bone name.
uint64_t MakeAttachWarnKey(Entity e, const std::string& bone) {
    uint64_t key = (static_cast<uint64_t>(e.id) << 32) |
                   static_cast<uint64_t>(e.generation);
    key ^= std::hash<std::string>{}(bone) * 0x9E3779B97F4A7C15ull;
    return key;
}

/// Resolves the VFX emitter's attach target to a world-space position.
///
/// Order of preference:
/// 1. `attachEntity` + non-empty `attachBoneName` + entity owns a
///    `SkeletalMeshComponent` whose skeleton has that bone → bone world matrix
///    from `SkeletalMeshInstance`, composed with the entity's SceneComponent
///    world matrix. Marks `boneMatched = true`.
/// 2. `attachEntity` alone → entity's SceneComponent world position.
/// 3. Invalid / missing → `resolved = false`.
///
/// Missing-target warnings are emitted once per (entity, bone) key.
AttachResolveResult ResolveAttachTransform(World* world,
                                           const VfxEmitterComponent& comp,
                                           const std::string& boneNameFallback) {
    AttachResolveResult out;
    if (!world) return out;
    if (!comp.attachEntity.IsValid()) return out;
    if (!world->IsValid(comp.attachEntity)) {
        auto& warned = AttachWarnedKeys();
        const uint64_t key = MakeAttachWarnKey(comp.attachEntity, comp.attachBoneName);
        if (warned.insert(key).second) {
            UZ_LOG(VFX, Warning,
                   "VFX attach target entity id={} gen={} is invalid — falling back to emitter SceneComponent",
                   comp.attachEntity.id, comp.attachEntity.generation);
        }
        return out;
    }

    // Target entity's SceneComponent transform (used as parent transform for
    // bone sampling AND as the plain fallback when no bone name is set).
    Vec3 parentPos = {0.0f, 0.0f, 0.0f};
    Mat4 parentWorld = Mat4::Identity();
    bool haveParent = false;
    if (world->HasComponent<SceneComponent>(comp.attachEntity)) {
        const auto& sc = world->GetComponent<SceneComponent>(comp.attachEntity);
        parentWorld = sc.GetWorldMatrix();
        parentPos   = sc.GetWorldPosition();
        haveParent  = true;
    }

    // Bone-socket path. Prefer the ECS component's bone name; fall back to
    // the asset-default (edited through the Parameters panel).
    const std::string& activeBone =
        !comp.attachBoneName.empty() ? comp.attachBoneName : boneNameFallback;
    if (!activeBone.empty() &&
        world->HasComponent<SkeletalMeshComponent>(comp.attachEntity)) {
        const auto& smc = world->GetComponent<SkeletalMeshComponent>(comp.attachEntity);
        if (smc.instanceId != UINT32_MAX) {
            SkeletalMeshInstance* inst =
                SkeletalMeshSubsystem::Get().GetInstance(smc.instanceId);
            if (inst != nullptr && inst->GetSkeleton() != nullptr) {
                const int32_t boneIdx = inst->GetSkeleton()->FindBoneIndex(activeBone);
                if (boneIdx >= 0) {
                    const Mat4 boneLocal = inst->GetBoneWorldMatrix(boneIdx);
                    const Mat4 boneWorld = parentWorld * boneLocal;
                    out.position = boneWorld.getTranslation();
                    out.resolved    = true;
                    out.boneMatched = true;
                    return out;
                }
                // Bone name did not match any bone on the skeleton.
                auto& warned = AttachWarnedKeys();
                const uint64_t key = MakeAttachWarnKey(comp.attachEntity, activeBone);
                if (warned.insert(key).second) {
                    UZ_LOG(VFX, Warning,
                           "VFX attach bone '{}' not found on skeleton for entity id={} — falling back to entity root",
                           activeBone, comp.attachEntity.id);
                }
            }
        }
    }

    // Parent-attach fallback (entity root transform).
    if (haveParent) {
        out.position = parentPos;
        out.resolved = true;
    }
    return out;
}

} // namespace

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

    // ── Frustum-cull plane extraction (phase 10.5.0) ────────────────────
    // Extracted once per frame from the active camera's viewProj; every
    // emitter tests its bounding sphere against these planes and feeds a
    // 2-frame hysteresis state machine. Culled emitters skip sim + render.
    // @ref engine/vfx/lod/vfx_frustum_cull.h
    const Mat4 viewProj = m_cameraManager->GetActiveCamera().GetProjection() *
                          m_cameraManager->GetActiveCamera().GetView();
    const FrustumPlanes frustum = ExtractFrustumPlanes(viewProj);

    for (auto [enttEntity, emitterComp] : view.each()) {
        // ── Already created — sync emitting state + world position ──────
        if (emitterComp.runtimeEmitterIdx >= 0) {
            const auto idx = static_cast<size_t>(emitterComp.runtimeEmitterIdx);
            if (idx < m_runtimeEmitters.size() && m_runtimeEmitters[idx]) {
                auto* rt = m_runtimeEmitters[idx].get();
                rt->isEmitting = emitterComp.emitting;
                // Sync world position: attach target (bone/entity) overrides
                // the emitter's own SceneComponent when configured.
                const AttachResolveResult attach = ResolveAttachTransform(
                    world, emitterComp, rt->attachBoneNameDefault);
                rt->attachedThisFrame = attach.resolved;
                if (attach.resolved) {
                    rt->worldPosition        = attach.position;
                    rt->attachedWorldPosition = attach.position;
                } else if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
                    rt->worldPosition = sc->GetPosition();
                }
                // Handle restart request: reset GPU buffer only.
                // ParticleEmitter fields are edited directly by the overlay
                // sliders, so we do NOT re-copy from the ECS component here.
                // For preset switches (Tab/Left/Right), the app code already
                // wrote the new preset into VfxEmitterComponent AND the runtime
                // ParticleEmitter gets the preset via ApplyPresetToComponent +
                // the initial creation path.
                if (emitterComp.restartRequested) {
                    // Respect the caller's `emitting` flag so a restart used
                    // to CLEAR particles (preset switch with emitting=false)
                    // doesn't inadvertently re-enable emission.
                    const bool keepEmitting = emitterComp.emitting;
                    rt->buffer.needsInitialization = true;
                    rt->spawnAccumulator = 0.0f;
                    rt->cpuAliveCount = 0;
                    rt->isEmitting = keepEmitting;
                    emitterComp.restartRequested = false;
                    // Reset cull hysteresis so the post-teleport frame is
                    // always "visible" regardless of the pre-teleport state.
                    emitterComp.cullCulledThisFrame   = false;
                    emitterComp.cullCulledPrevFrame   = false;
                    emitterComp.cullFramesSinceCulled = 0;

                    // Phase 15.14 — re-copy all config fields from the
                    // ECS component to the runtime emitter. Needed so
                    // preset switches / host-driven param edits take
                    // effect on emitters that the demo can't address
                    // via `GetRuntimeEmitter` (e.g. just-added slots
                    // whose `runtimeEmitterIdx` was still -1 at the
                    // time of the switch).
                    rt->maxParticles        = emitterComp.maxParticles;
                    rt->emissionRate        = emitterComp.emissionRate;
                    rt->lifetime            = emitterComp.lifetime;
                    rt->lifetimeVariation   = emitterComp.lifetimeVariation;
                    rt->spawnVelocity       = emitterComp.spawnVelocity;
                    rt->velocityVariation   = emitterComp.velocityVariation;
                    rt->spreadAngle         = emitterComp.spreadAngle;
                    rt->spawnShape          = emitterComp.spawnShape;
                    rt->spawnRadius         = emitterComp.spawnRadius;
                    rt->initialSize         = emitterComp.initialSize;
                    rt->initialColor        = emitterComp.initialColor;
                    rt->emissiveIntensity   = emitterComp.emissiveIntensity;
                    rt->litMode             = emitterComp.litMode;
                    rt->softParticleFadeRange = emitterComp.softParticleFadeRange;
                    rt->facingMode          = emitterComp.facingMode;
                    rt->stretchFactor       = emitterComp.stretchFactor;
                    rt->blendMode           = emitterComp.blendMode;
                    rt->cameraOffset        = emitterComp.cameraOffset;
                    rt->pivotOffsetX        = emitterComp.pivotOffsetX;
                    rt->pivotOffsetY        = emitterComp.pivotOffsetY;
                    rt->sizeOverLifeStart   = emitterComp.sizeOverLifeStart;
                    rt->sizeOverLifePeak    = emitterComp.sizeOverLifePeak;
                    rt->sizeOverLifeEnd     = emitterComp.sizeOverLifeEnd;
                    rt->sizeOverLifePeakTime = emitterComp.sizeOverLifePeakTime;
                    rt->colorLifeStartR     = emitterComp.colorLifeStartR;
                    rt->colorLifeStartG     = emitterComp.colorLifeStartG;
                    rt->colorLifeStartB     = emitterComp.colorLifeStartB;
                    rt->colorLifeStartA     = emitterComp.colorLifeStartA;
                    rt->colorLifeEndR       = emitterComp.colorLifeEndR;
                    rt->colorLifeEndG       = emitterComp.colorLifeEndG;
                    rt->colorLifeEndB       = emitterComp.colorLifeEndB;
                    rt->colorLifeEndA       = emitterComp.colorLifeEndA;
                    rt->gravity             = emitterComp.gravity;
                    rt->damping             = emitterComp.damping;
                    rt->curlStrength        = emitterComp.curlStrength;
                    rt->curlFrequency       = emitterComp.curlFrequency;
                    rt->curlSpeed           = emitterComp.curlSpeed;
                    rt->dragCoefficient     = emitterComp.dragCoefficient;
                    rt->windDirX            = emitterComp.windDirX;
                    rt->windDirY            = emitterComp.windDirY;
                    rt->windDirZ            = emitterComp.windDirZ;
                    rt->windStrength        = emitterComp.windStrength;
                    rt->orbitEnabled        = emitterComp.orbitEnabled;
                    rt->orbitRadius         = emitterComp.orbitRadius;
                    rt->orbitSpeed          = emitterComp.orbitSpeed;
                    rt->accelX              = emitterComp.accelX;
                    rt->accelY              = emitterComp.accelY;
                    rt->accelZ              = emitterComp.accelZ;
                    rt->vortexTangentialStrength = emitterComp.vortexTangentialStrength;
                    rt->vortexRadialStrength     = emitterComp.vortexRadialStrength;
                    rt->vortexMaxRadius          = emitterComp.vortexMaxRadius;
                    rt->groundCollision     = emitterComp.groundCollision;
                    rt->restitution         = emitterComp.restitution;
                    rt->collisionResponse   = emitterComp.collisionResponse;
                    rt->collisionFriction   = emitterComp.collisionFriction;
                    rt->rotationRate        = emitterComp.rotationRate;
                    rt->rotationOverLifeStart = emitterComp.rotationOverLifeStart;
                    rt->rotationOverLifeEnd   = emitterComp.rotationOverLifeEnd;
                    rt->sizeBySpeedEnabled  = emitterComp.sizeBySpeedEnabled;
                    rt->sizeBySpeedMin      = emitterComp.sizeBySpeedMin;
                    rt->sizeBySpeedMax      = emitterComp.sizeBySpeedMax;
                    rt->speedRangeMax       = emitterComp.speedRangeMax;
                    rt->colorBySpeedEnabled = emitterComp.colorBySpeedEnabled;
                    rt->screenSpaceSizeEnabled = emitterComp.screenSpaceSizeEnabled;
                    rt->screenSpacePixelSize   = emitterComp.screenSpacePixelSize;
                    rt->emitterDuration     = emitterComp.emitterDuration;
                    rt->loopCount           = emitterComp.loopCount;
                    rt->startDelay          = emitterComp.startDelay;
                    rt->particleLightEnabled     = emitterComp.particleLightEnabled;
                    rt->particleLightRadius      = emitterComp.particleLightRadius;
                    rt->particleLightIntensity   = emitterComp.particleLightIntensity;
                    rt->particleLightFlicker     = emitterComp.particleLightFlicker;
                    rt->particleLightFlickerSpeed = emitterComp.particleLightFlickerSpeed;
                    rt->renderPriority      = emitterComp.renderPriority;
                }

                // ── Frustum cull test (phase 10.5.0) ────────────────────
                // Conservative bounding sphere = spawnRadius + maxSpeed *
                // maxLifetime. Sphere test → hysteresis state machine on
                // the ECS component. The runtime emitter's `cullSuppress`
                // flag (below) gates the sim + render pass.
                const float radius = ComputeEmitterBoundingSphereRadius(
                    rt->spawnRadius,
                    std::max(0.0f, rt->velocityVariation) +
                        std::max(0.0f, std::max(
                            std::max(std::fabs(rt->spawnVelocity.x),
                                     std::fabs(rt->spawnVelocity.y)),
                            std::fabs(rt->spawnVelocity.z))),
                    std::max(0.0f, rt->lifetime));
                const bool inside = IsSphereInsideFrustum(
                    frustum, rt->worldPosition, radius);

                EmitterCullState cs;
                cs.culledThisFrame   = emitterComp.cullCulledThisFrame;
                cs.culledPrevFrame   = emitterComp.cullCulledPrevFrame;
                cs.framesSinceCulled = emitterComp.cullFramesSinceCulled;
                bool shouldDispatch = cs.Update(inside);
                emitterComp.cullCulledThisFrame   = cs.culledThisFrame;
                emitterComp.cullCulledPrevFrame   = cs.culledPrevFrame;
                emitterComp.cullFramesSinceCulled = cs.framesSinceCulled;

                // ── HZB occlusion gate (phase 9.14.0) ──────────────────
                // If still visible after frustum test, run the HZB
                // occlusion check. The HZB texture + mip chain are
                // queried by the scene renderer if available; when no
                // HZB is present the vote defaults to "visible" and
                // the hysteresis stays in the "not occluded" state.
                if (shouldDispatch) {
                    // Construct transient hysteresis from ECS state.
                    VfxOcclusionHysteresis hzb(3u);
                    // Restore committed state from component.
                    if (emitterComp.hzbOccluded) {
                        // Force committed-occluded by feeding enough
                        // occluded votes through the state machine.
                        for (uint32_t k = 0; k < 4; ++k) hzb.Update(true);
                    }

                    // TODO(9.14): actual HZB pyramid sample requires
                    // the scene's HZB SRV + viewProj. When the HZB
                    // pass is integrated, replace this with:
                    //   auto rect = VfxProjectAABBToScreen(aabb, viewProj);
                    //   auto mip = VfxSelectHZBMip(rect, hzbW, hzbH, mipCount);
                    //   float hzbDepth = SampleHZB(mip, rect);
                    //   bool rawOccluded = VfxIsOccludedByHZB(rect.nearestDepth, hzbDepth);
                    // For now, vote "not occluded" so the hysteresis
                    // is wired but no emitters are incorrectly culled.
                    const bool rawOccluded = false;

                    const bool stillVisible = !hzb.Update(rawOccluded);
                    emitterComp.hzbOccluded     = hzb.CommittedOccluded();
                    emitterComp.hzbDissentCount = hzb.DissentCount();

                    if (!stillVisible) {
                        shouldDispatch = false;
                    }
                }

                rt->cullSuppress = !shouldDispatch;
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
            const AttachResolveResult attach = ResolveAttachTransform(
                world, emitterComp, emitter->attachBoneNameDefault);
            if (attach.resolved) {
                worldPos = attach.position;
                emitter->attachedThisFrame = true;
                emitter->attachedWorldPosition = attach.position;
            } else if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
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
            // Inline parameters: copy ALL config fields from ECS component
            emitter->maxParticles       = emitterComp.maxParticles;
            emitter->emissionRate       = emitterComp.emissionRate;
            emitter->lifetime           = emitterComp.lifetime;
            emitter->lifetimeVariation  = emitterComp.lifetimeVariation;
            emitter->spawnVelocity      = emitterComp.spawnVelocity;
            emitter->velocityVariation  = emitterComp.velocityVariation;
            emitter->spreadAngle        = emitterComp.spreadAngle;
            emitter->spawnShape         = emitterComp.spawnShape;
            emitter->spawnRadius        = emitterComp.spawnRadius;
            emitter->initialSize        = emitterComp.initialSize;
            emitter->initialColor       = emitterComp.initialColor;
            emitter->emissiveIntensity  = emitterComp.emissiveIntensity;
            emitter->litMode            = emitterComp.litMode;
            emitter->softParticleFadeRange = emitterComp.softParticleFadeRange;
            emitter->facingMode         = emitterComp.facingMode;
            emitter->stretchFactor      = emitterComp.stretchFactor;
            emitter->blendMode          = emitterComp.blendMode;
            emitter->cameraOffset       = emitterComp.cameraOffset;
            emitter->pivotOffsetX       = emitterComp.pivotOffsetX;
            emitter->pivotOffsetY       = emitterComp.pivotOffsetY;
            emitter->sizeOverLifeStart  = emitterComp.sizeOverLifeStart;
            emitter->sizeOverLifePeak   = emitterComp.sizeOverLifePeak;
            emitter->sizeOverLifeEnd    = emitterComp.sizeOverLifeEnd;
            emitter->sizeOverLifePeakTime = emitterComp.sizeOverLifePeakTime;
            emitter->colorLifeStartR    = emitterComp.colorLifeStartR;
            emitter->colorLifeStartG    = emitterComp.colorLifeStartG;
            emitter->colorLifeStartB    = emitterComp.colorLifeStartB;
            emitter->colorLifeStartA    = emitterComp.colorLifeStartA;
            emitter->colorLifeEndR      = emitterComp.colorLifeEndR;
            emitter->colorLifeEndG      = emitterComp.colorLifeEndG;
            emitter->colorLifeEndB      = emitterComp.colorLifeEndB;
            emitter->colorLifeEndA      = emitterComp.colorLifeEndA;
            emitter->sizeBySpeedEnabled = emitterComp.sizeBySpeedEnabled;
            emitter->sizeBySpeedMin     = emitterComp.sizeBySpeedMin;
            emitter->sizeBySpeedMax     = emitterComp.sizeBySpeedMax;
            emitter->speedRangeMax      = emitterComp.speedRangeMax;
            emitter->colorBySpeedEnabled = emitterComp.colorBySpeedEnabled;
            emitter->screenSpaceSizeEnabled = emitterComp.screenSpaceSizeEnabled;
            emitter->screenSpacePixelSize = emitterComp.screenSpacePixelSize;
            emitter->gravity            = emitterComp.gravity;
            emitter->damping            = emitterComp.damping;
            emitter->curlStrength       = emitterComp.curlStrength;
            emitter->curlFrequency      = emitterComp.curlFrequency;
            emitter->curlSpeed          = emitterComp.curlSpeed;
            emitter->dragCoefficient    = emitterComp.dragCoefficient;
            emitter->windDirX           = emitterComp.windDirX;
            emitter->windDirY           = emitterComp.windDirY;
            emitter->windDirZ           = emitterComp.windDirZ;
            emitter->windStrength       = emitterComp.windStrength;
            emitter->accelX             = emitterComp.accelX;
            emitter->accelY             = emitterComp.accelY;
            emitter->accelZ             = emitterComp.accelZ;
            emitter->vortexTangentialStrength = emitterComp.vortexTangentialStrength;
            emitter->vortexRadialStrength     = emitterComp.vortexRadialStrength;
            emitter->vortexMaxRadius          = emitterComp.vortexMaxRadius;
            emitter->orbitEnabled       = emitterComp.orbitEnabled;
            emitter->orbitRadius        = emitterComp.orbitRadius;
            emitter->orbitSpeed         = emitterComp.orbitSpeed;
            emitter->groundCollision    = emitterComp.groundCollision;
            emitter->restitution        = emitterComp.restitution;
            emitter->collisionResponse  = emitterComp.collisionResponse;
            emitter->collisionFriction  = emitterComp.collisionFriction;
            emitter->rotationRate       = emitterComp.rotationRate;
            emitter->rotationOverLifeStart = emitterComp.rotationOverLifeStart;
            emitter->rotationOverLifeEnd   = emitterComp.rotationOverLifeEnd;
            emitter->particleLightEnabled     = emitterComp.particleLightEnabled;
            emitter->particleLightRadius      = emitterComp.particleLightRadius;
            emitter->particleLightIntensity   = emitterComp.particleLightIntensity;
            emitter->particleLightFlicker     = emitterComp.particleLightFlicker;
            emitter->particleLightFlickerSpeed = emitterComp.particleLightFlickerSpeed;
            emitter->emitterDuration    = emitterComp.emitterDuration;
            emitter->loopCount          = emitterComp.loopCount;
            emitter->startDelay         = emitterComp.startDelay;
            emitter->isEmitting         = emitterComp.emitting;
            emitter->renderPriority     = emitterComp.renderPriority;

            // ── Sprite texture ─────────────────────────────────────────
            emitter->spriteTexturePath   = emitterComp.spriteTexturePath;
            emitter->flipbookRows        = emitterComp.flipbookRows;
            emitter->flipbookCols        = emitterComp.flipbookCols;
            emitter->flipbookTotalFrames = emitterComp.flipbookTotalFrames;
            emitter->flipbookFPS         = emitterComp.flipbookFPS;
            emitter->spriteChannelMask   = emitterComp.spriteChannelMask;

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

        // ── Load sprite texture if path is set ─────────────────────────
        // Creates a VFXMaterial, loads the texture via TextureLoader, and
        // assigns the bindless SRV index to the emitter for rendering.
        if (!emitter->spriteTexturePath.empty() && emitter->spriteTextureSrvIndex == UINT32_MAX) {
            auto mat = std::make_unique<VFXMaterial>();
            mat->spriteTexturePath  = emitter->spriteTexturePath;
            mat->flipbookRows       = emitter->flipbookRows;
            mat->flipbookCols       = emitter->flipbookCols;
            mat->flipbookTotalFrames = emitter->flipbookTotalFrames;

            auto* texCmd = m_device->CreateCommandList(RHIQueueType::Graphics);
            if (texCmd) {
                texCmd->Begin();
                if (mat->LoadTexture(m_device, texCmd, /*isSRGB=*/true)) {
                    emitter->spriteTextureSrvIndex = mat->spriteTextureSrvIndex;
                    // VFXMaterial auto-detects flipbook from filename; propagate back
                    emitter->flipbookRows        = mat->flipbookRows;
                    emitter->flipbookCols        = mat->flipbookCols;
                    emitter->flipbookTotalFrames = mat->flipbookTotalFrames;
                    emitter->material = mat.release(); // Transfer ownership to emitter
                    UZ_LOG(VFX, Info,
                           "SyncVfxEmitters: loaded sprite '{}' (SRV={}, {}x{} flipbook)",
                           emitter->spriteTexturePath, emitter->spriteTextureSrvIndex,
                           emitter->flipbookRows, emitter->flipbookCols);
                } else {
                    UZ_LOG(VFX, Warning,
                           "SyncVfxEmitters: failed to load sprite '{}' — using procedural circle",
                           emitter->spriteTexturePath);
                }
                texCmd->End();
                texCmd->Execute();
                m_device->WaitIdle();
                m_device->DestroyCommandList(texCmd);
            }
        }

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

    // ── VfxSystemComponent (compound, multi-emitter) ─────────────────────
    // Each VfxSystemComponent instantiates a VfxSystemInstance which owns N
    // child ParticleEmitter*. Child emitters are registered with the global
    // VFXSystem so they take part in per-frame compute dispatch, but the
    // unique_ptr ownership lives inside the VfxSystemInstance.
    auto sysView = registry.view<VfxSystemComponent>();
    for (auto [enttEntity, sysComp] : sysView.each()) {
        // Existing instance — sync transform and emission state.
        if (sysComp.runtimeInstanceIdx >= 0) {
            const auto idx = static_cast<size_t>(sysComp.runtimeInstanceIdx);
            if (idx < m_runtimeSystems.size() && m_runtimeSystems[idx]) {
                auto* inst = m_runtimeSystems[idx].get();
                inst->SetEmitting(sysComp.emitting);
                // Propagate to children every frame. `VfxSystemInstance::Tick`
                // would do this, but it is not called from the render loop,
                // so `SetEmitting` alone does not silence active children.
                // Without this, toggling `sysComp.emitting = false` stops
                // new spawns at the instance level but the children keep
                // spawning until a restart explicitly overrides them.
                for (auto* child : inst->GetEmitters()) {
                    if (!child) continue;
                    child->isEmitting = sysComp.emitting;
                }
                if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
                    inst->SetWorldTransform(sc->GetPosition());
                }
                if (sysComp.restartRequested) {
                    // Restart every child emitter: reset GPU buffer + spawn state.
                    // Respect the caller's `sysComp.emitting` flag so a restart
                    // used to CLEAR particles (preset switch: emitting=false)
                    // doesn't inadvertently re-enable emission on the next tick.
                    const bool keepEmitting = sysComp.emitting;
                    for (auto* child : inst->GetEmitters()) {
                        if (!child) continue;
                        child->buffer.needsInitialization = true;
                        child->spawnAccumulator = 0.0f;
                        child->cpuAliveCount    = 0;
                        child->isEmitting       = keepEmitting;
                    }
                    inst->SetEmitting(keepEmitting);
                    sysComp.restartRequested = false;
                }
            }
            continue;
        }

        // First encounter — load asset and create instance.
        if (sysComp.systemAssetPath.empty()) {
            UZ_LOG(VFX, Warning,
                   "SyncVfxEmitters: VfxSystemComponent has empty systemAssetPath — skipping");
            continue;
        }

        auto asset = VfxSystem::Load(sysComp.systemAssetPath, vfs);
        if (!asset) {
            UZ_LOG(VFX, Error,
                   "SyncVfxEmitters: failed to load VfxSystem asset '{}' — skipping",
                   sysComp.systemAssetPath);
            continue;
        }

        auto instance = std::make_unique<VfxSystemInstance>();
        instance->SetEmitting(sysComp.emitting);
        if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
            instance->SetWorldTransform(sc->GetPosition());
        }
        if (!instance->Init(*asset, m_device, activeVFX->GetBudget())) {
            UZ_LOG(VFX, Error,
                   "SyncVfxEmitters: VfxSystemInstance::Init failed for '{}' — skipping",
                   sysComp.systemAssetPath);
            continue;
        }
        // Re-apply transform AFTER Init so children created with default
        // worldPosition=(0,0,0) get the correct position immediately.
        if (auto* sc = registry.try_get<SceneComponent>(enttEntity)) {
            instance->SetWorldTransform(sc->GetPosition());
        }

        // Register each child with the global VFXSystem AND perform the
        // same initial GPU upload / transition done in the single-emitter
        // path. This keeps the render-graph happy on frame 1.
        for (auto* child : instance->GetEmitters()) {
            if (!child) continue;

            auto* initCmd = m_device->CreateCommandList(RHIQueueType::Graphics);
            if (initCmd) {
                initCmd->Begin();
                child->buffer.UploadInitialState(m_device, initCmd);
                child->buffer.TransitionAllToUAV(initCmd);
                initCmd->End();
                initCmd->Execute();
                m_device->WaitIdle();
                m_device->DestroyCommandList(initCmd);
                for (auto h : child->buffer.pendingStagingBuffers) {
                    m_device->DestroyBuffer(h);
                }
                child->buffer.pendingStagingBuffers.clear();
            }
            child->needsInitialUpload = false;

            // ── Load sprite texture if preset specified one ─────────────
            // Mirrors the single-emitter sprite-load path so compound
            // children (e.g. FireballHead flipbook) can display sprites too.
            if (!child->spriteTexturePath.empty()
                && child->spriteTextureSrvIndex == UINT32_MAX) {
                auto mat = std::make_unique<VFXMaterial>();
                mat->spriteTexturePath   = child->spriteTexturePath;
                mat->flipbookRows        = child->flipbookRows;
                mat->flipbookCols        = child->flipbookCols;
                mat->flipbookTotalFrames = child->flipbookTotalFrames;

                auto* texCmd = m_device->CreateCommandList(RHIQueueType::Graphics);
                if (texCmd) {
                    texCmd->Begin();
                    if (mat->LoadTexture(m_device, texCmd, /*isSRGB=*/true)) {
                        child->spriteTextureSrvIndex = mat->spriteTextureSrvIndex;
                        child->flipbookRows        = mat->flipbookRows;
                        child->flipbookCols        = mat->flipbookCols;
                        child->flipbookTotalFrames = mat->flipbookTotalFrames;
                        child->material = mat.release();
                        UZ_LOG(VFX, Info,
                               "SyncVfxEmitters: compound child loaded sprite '{}' "
                               "(SRV={}, {}x{} flipbook, {} frames)",
                               child->spriteTexturePath,
                               child->spriteTextureSrvIndex,
                               child->flipbookRows, child->flipbookCols,
                               child->flipbookTotalFrames);
                    } else {
                        UZ_LOG(VFX, Warning,
                               "SyncVfxEmitters: compound child failed to load "
                               "sprite '{}' — using procedural circle",
                               child->spriteTexturePath);
                    }
                    texCmd->End();
                    texCmd->Execute();
                    m_device->WaitIdle();
                    m_device->DestroyCommandList(texCmd);
                }
            }

            activeVFX->RegisterEmitter(child);
        }

        const auto newIdx = static_cast<int32_t>(m_runtimeSystems.size());
        m_runtimeSystems.push_back(std::move(instance));
        sysComp.runtimeInstanceIdx = newIdx;

        UZ_LOG(VFX, Info,
               "SyncVfxEmitters: created VfxSystemInstance idx={} from '{}' "
               "({} child emitters)",
               newIdx, sysComp.systemAssetPath,
               m_runtimeSystems[newIdx]->GetEmitterCount());
    }
}

} // namespace uz
