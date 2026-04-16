/// UzEngine SkeletalMeshSubsystem — Implementation.

#include "skeletal_mesh_subsystem.h"
#include "skeletal_mesh_instance.h"
#include "skeleton.h"
#include "skeleton_io.h"
#include "animation_clip.h"
#include "animation_io.h"
#include "animation_controller.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "vfs.h"
#include "upload_context.h"
#include "scene_renderer.h"
#include "job_system.h"
#include "world.h"
#include "static_mesh_component.h"
#include "skeletal_mesh_component.h"
#include "log.h"

UZ_DECLARE_LOG_CATEGORY(Animation);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Singleton
// ─────────────────────────────────────────────────────────────────────────────

SkeletalMeshSubsystem& SkeletalMeshSubsystem::Get() {
    static SkeletalMeshSubsystem instance;
    return instance;
}

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

uint32_t SkeletalMeshSubsystem::RegisterEntity(const RegisterInfo& info) {
    if (info.sourceMesh == nullptr || info.skeleton == nullptr) {
        UZ_LOG(Animation, Error, "RegisterEntity: null mesh or skeleton");
        return UINT32_MAX;
    }

    std::lock_guard<std::mutex> lock(m_registryMutex);

    InstanceRecord rec;
    rec.instance = std::make_unique<SkeletalMeshInstance>();
    rec.instance->Bind(info.sourceMesh, info.skeleton, info.outputMeshId);
    rec.controller = info.controller;

    const uint32_t id = static_cast<uint32_t>(m_records.size());
    m_records.push_back(std::move(rec));

    UZ_LOG(Animation, Info,
           "SkeletalMeshSubsystem: registered entity {} (bones={}, verts={}, outputMeshId={})",
           id, info.skeleton->GetBoneCount(), info.sourceMesh->GetVertexCount(), info.outputMeshId);
    return id;
}

void SkeletalMeshSubsystem::UnregisterEntity(uint32_t instanceId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    if (instanceId >= m_records.size()) return;
    m_records[instanceId].instance.reset();
    m_records[instanceId].controller = nullptr;
}

void SkeletalMeshSubsystem::Reset() {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    m_records.clear();
    m_controllers.clear();
    m_skeletons.clear();
    m_sharedMeshes.clear();
}

std::size_t SkeletalMeshSubsystem::GetInstanceCount() const {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    return m_records.size();
}

SkeletalMeshInstance* SkeletalMeshSubsystem::GetInstance(uint32_t instanceId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    if (instanceId >= m_records.size()) return nullptr;
    return m_records[instanceId].instance.get();
}

AnimationController* SkeletalMeshSubsystem::GetController(uint32_t controllerId) {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    if (controllerId >= m_controllers.size()) return nullptr;
    return m_controllers[controllerId].get();
}

AnimationController* SkeletalMeshSubsystem::CreateController() {
    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto ctrl = std::make_unique<AnimationController>();
    AnimationController* raw = ctrl.get();
    m_controllers.push_back(std::move(ctrl));
    return raw;
}

// ─────────────────────────────────────────────────────────────────────────────
// Finalize — post-scene-load wiring
// ─────────────────────────────────────────────────────────────────────────────

void SkeletalMeshSubsystem::Finalize(World& world, SceneRenderer& renderer, IVFS& vfs) {
    auto& registry = world.GetRegistry();
    auto view = registry.view<StaticMeshComponent, SkeletalMeshComponent>();

    uint32_t perEntityIndex = 0;
    for (auto enttEnt : view) {
        auto& staticMesh = view.get<StaticMeshComponent>(enttEnt);
        auto& skelMesh   = view.get<SkeletalMeshComponent>(enttEnt);

        if (skelMesh.instanceId != UINT32_MAX) continue; // already registered

        // ── Load the source mesh via VFS ────────────────────────────────
        const Mesh* sourceMeshPtr = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            auto it = m_sharedMeshes.find(skelMesh.meshPath);
            if (it != m_sharedMeshes.end()) {
                sourceMeshPtr = it->second.get();
            }
        }
        if (sourceMeshPtr == nullptr) {
            auto bytes = vfs.Read(skelMesh.meshPath);
            if (bytes.empty()) {
                UZ_LOG(Animation, Warning, "Finalize: VFS read failed for '{}'", skelMesh.meshPath);
                continue;
            }
            Mesh loaded = LoadMeshFromMemory(bytes, skelMesh.meshPath);
            if (!loaded.IsValid()) {
                UZ_LOG(Animation, Warning, "Finalize: '{}' not a valid mesh", skelMesh.meshPath);
                continue;
            }
            if (loaded.skinnedVertexData.empty()) {
                UZ_LOG(Animation, Warning, "Finalize: '{}' is not skinned (no skinnedVertexData)", skelMesh.meshPath);
                continue;
            }
            std::lock_guard<std::mutex> lock(m_registryMutex);
            auto owned = std::make_unique<Mesh>(std::move(loaded));
            sourceMeshPtr = owned.get();
            m_sharedMeshes.emplace(skelMesh.meshPath, std::move(owned));
        }

        // ── Resolve skeleton path ──────────────────────────────────────
        std::string skelPath = skelMesh.skeletonPath;
        if (skelPath.empty()) skelPath = sourceMeshPtr->skeletonPath;
        if (skelPath.empty()) {
            UZ_LOG(Animation, Warning,
                   "Finalize: no skeleton path on component or mesh '{}'", skelMesh.meshPath);
            continue;
        }
        const Skeleton* skeleton = LoadSkeletonFromVfs(vfs, skelPath);
        if (skeleton == nullptr) continue;

        // ── Register a per-entity procedural output mesh ───────────────
        // We deep-copy the bind-pose mesh so the subsystem later overwrites
        // the VB with skinned vertices each frame.
        Mesh procCopy;
        procCopy.header            = sourceMeshPtr->header;
        procCopy.vertexData        = sourceMeshPtr->vertexData; // 48 B bind pose
        procCopy.indexData         = sourceMeshPtr->indexData;
        procCopy.submeshes         = sourceMeshPtr->submeshes;
        procCopy.skinnedVertexData.clear(); // proc mesh itself is not skinned
        procCopy.skeletonPath.clear();

        const std::string key = "skeletal://" + skelMesh.meshPath + "#" + std::to_string(perEntityIndex++);
        const uint32_t procMeshId = renderer.RegisterProceduralMesh(std::move(procCopy), key);
        if (procMeshId == UINT32_MAX) {
            UZ_LOG(Animation, Warning, "Finalize: RegisterProceduralMesh failed for '{}'", key);
            continue;
        }

        // Wire the StaticMeshComponent to the per-entity mesh, and mark it
        // loaded so scene_sync does not try to lazy-load from meshPath.
        staticMesh.meshId = procMeshId;
        staticMesh.meshLoaded = true;
        staticMesh.lodGroup.numLevels = 1;
        staticMesh.lodGroup.levels[0].meshIndex = procMeshId;

        // ── Create a controller and load the clip library ─────────────
        AnimationController* controller = CreateController();
        uint32_t controllerIdx = UINT32_MAX;
        {
            std::lock_guard<std::mutex> lock(m_registryMutex);
            for (size_t i = 0; i < m_controllers.size(); ++i) {
                if (m_controllers[i].get() == controller) {
                    controllerIdx = static_cast<uint32_t>(i);
                    break;
                }
            }
        }

        uint32_t loadedClips = 0;
        for (const auto& clipPath : skelMesh.animationClipPaths) {
            auto clipBytes = vfs.Read(clipPath);
            if (clipBytes.empty()) {
                UZ_LOG(Animation, Warning, "Finalize: VFS read failed for clip '{}'", clipPath);
                continue;
            }
            auto parsed = ReadAnimation(clipBytes);
            if (!parsed.has_value()) continue;

            std::string name = parsed->name;
            if (name.empty()) {
                // Derive from filename if the clip didn't carry a name.
                auto slash = clipPath.find_last_of("/\\");
                auto dot   = clipPath.find_last_of('.');
                name = clipPath.substr(
                    (slash == std::string::npos) ? 0 : slash + 1,
                    (dot == std::string::npos) ? std::string::npos : dot - ((slash == std::string::npos) ? 0 : slash + 1));
            }
            controller->RegisterClip(name, std::move(parsed.value()));
            ++loadedClips;
        }

        if (!skelMesh.initialClipName.empty() && controller->HasClip(skelMesh.initialClipName)) {
            controller->Play(skelMesh.initialClipName, true, 1.0f);
        }

        // ── Register the instance ─────────────────────────────────────
        RegisterInfo regInfo;
        regInfo.sourceMesh   = sourceMeshPtr;
        regInfo.skeleton     = skeleton;
        regInfo.outputMeshId = procMeshId;
        regInfo.controller   = controller;
        skelMesh.instanceId  = RegisterEntity(regInfo);
        skelMesh.controllerId = controllerIdx;

        UZ_LOG(Animation, Info,
               "Finalize: entity with mesh '{}' — skeleton '{}' ({} bones), {} clips, outputMeshId={}",
               skelMesh.meshPath, skelPath, skeleton->GetBoneCount(), loadedClips, procMeshId);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Resource loaders
// ─────────────────────────────────────────────────────────────────────────────

const Skeleton* SkeletalMeshSubsystem::LoadSkeletonFromVfs(IVFS& vfs, const std::string& path) {
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        auto it = m_skeletons.find(path);
        if (it != m_skeletons.end()) return it->second.get();
    }

    auto bytes = vfs.Read(path);
    if (bytes.empty()) {
        UZ_LOG(Animation, Error, "LoadSkeletonFromVfs: empty bytes for '{}'", path);
        return nullptr;
    }

    auto result = ReadSkeleton(bytes);
    if (!result.has_value()) {
        UZ_LOG(Animation, Error, "LoadSkeletonFromVfs: ReadSkeleton failed for '{}'", path);
        return nullptr;
    }

    std::lock_guard<std::mutex> lock(m_registryMutex);
    auto owned = std::make_unique<Skeleton>(std::move(result.value()));
    Skeleton* raw = owned.get();
    m_skeletons.emplace(path, std::move(owned));
    UZ_LOG(Animation, Info, "LoadSkeletonFromVfs: loaded '{}' ({} bones)", path, raw->GetBoneCount());
    return raw;
}

// ─────────────────────────────────────────────────────────────────────────────
// Per-tick orchestration
// ─────────────────────────────────────────────────────────────────────────────

void SkeletalMeshSubsystem::TickAll(float dt) {
    // Build a flat list of (instance, controller) pairs under the lock, then
    // release so parallel workers don't contend on the registry mutex.
    struct WorkItem {
        SkeletalMeshInstance* instance;
        AnimationController*  controller;
    };
    std::vector<WorkItem> work;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        work.reserve(m_records.size());
        for (auto& rec : m_records) {
            if (!rec.instance) continue;
            work.push_back({rec.instance.get(), rec.controller});
        }
    }
    if (work.empty()) return;

    auto runOne = [dt](WorkItem& w) {
        if (!w.instance || !w.instance->IsValid()) return;

        const Skeleton* skeleton = w.instance->GetSkeleton();
        if (skeleton != nullptr && w.controller != nullptr) {
            w.controller->Update(dt);
            Pose pose;
            w.controller->SamplePose(*skeleton, pose);
            w.instance->SetPose(pose);
        }

        w.instance->UpdatePaletteFromPose();
        w.instance->SkinCpu();
    };

    auto* js = JobSystem::Get();
    if (js != nullptr && work.size() >= 2) {
        js->ParallelFor(work.data(), work.size(), runOne);
    } else {
        for (auto& w : work) runOne(w);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render-thread upload
// ─────────────────────────────────────────────────────────────────────────────

void SkeletalMeshSubsystem::UploadAll(IRHIDevice* device, IRHICommandList* cmdList, SceneRenderer& renderer) {
    if (device == nullptr || cmdList == nullptr) return;

    std::vector<std::pair<uint32_t, std::vector<uint8_t>>> snapshots;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        snapshots.reserve(m_records.size());
        for (auto& rec : m_records) {
            if (!rec.instance) continue;
            const auto& bytes = rec.instance->GetOutputVertexData();
            if (bytes.empty()) continue;
            snapshots.emplace_back(rec.instance->GetOutputMeshId(), bytes);
        }
    }
    if (snapshots.empty()) return;

    UploadContext upload(device);
    for (const auto& [meshId, bytes] : snapshots) {
        const GPUMesh* gmesh = renderer.GetGPUMesh(meshId);
        if (gmesh == nullptr || !gmesh->IsValid()) continue;
        if (!upload.UploadBuffer(cmdList, gmesh->vertexBuffer, bytes.data(), bytes.size())) {
            UZ_LOG(Animation, Warning,
                   "SkeletalMeshSubsystem::UploadAll: UploadBuffer failed for meshId {}", meshId);
        }
    }
    upload.ReleaseStaging();
}

} // namespace uz
