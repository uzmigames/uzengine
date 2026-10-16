/// UzEngine SkeletalMeshSubsystem — Implementation.

#include "skeletal_mesh_subsystem.h"
#include "skeletal_mesh_instance.h"
#include "skeleton.h"
#include "skeleton_io.h"
#include "animation_clip.h"
#include "animation_io.h"
#include "animation_controller.h"
#include "mat4.h"
#include "mesh.h"
#include "gpu_mesh.h"
#include "vertex.h"
#include "vfs.h"
#include "upload_context.h"
#include "scene_renderer.h"
#include "pipeline_library.h"
#include "rhi.h"
#include "job_system.h"
#include "world.h"
#include "static_mesh_component.h"
#include "skeletal_mesh_component.h"
#include "log.h"
#include "render_graph_states.h"

#include <cstring>

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
        // the VB with skinned vertices each frame. Flagging `gpuSkinned`
        // upgrades the VB usage with UAV + StructuredBuffer so
        // `gpu_skinning_cs.hlsl` can write animated vertices into it
        // directly (see MakeVertexBufferDesc in gpu_mesh.cpp).
        Mesh procCopy;
        procCopy.header            = sourceMeshPtr->header;
        procCopy.vertexData        = sourceMeshPtr->vertexData; // 48 B bind pose (seed, overwritten by CS each frame)
        procCopy.indexData         = sourceMeshPtr->indexData;
        procCopy.submeshes         = sourceMeshPtr->submeshes;
        procCopy.skinnedVertexData.clear(); // proc mesh itself is not skinned
        procCopy.skeletonPath.clear();
        procCopy.gpuSkinned        = true;

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
            // Strip common DCC-tool retargeting prefixes (e.g. UE exports
            // retargeted animations as "AS_Mushroom_Idle"; gameplay scripts
            // refer to the logical name "Mushroom_Idle"). Strip the prefix
            // and ALSO register the unstripped name as an alias so existing
            // content still resolves.
            const std::string originalName = name;
            constexpr const char* kRetargetPrefixes[] = {"AS_", "Anim_"};
            for (const char* prefix : kRetargetPrefixes) {
                const size_t plen = std::strlen(prefix);
                if (name.size() > plen && name.compare(0, plen, prefix) == 0) {
                    name.erase(0, plen);
                    break;
                }
            }
            AnimationClip clipCopy = parsed.value();
            controller->RegisterClip(name, std::move(parsed.value()));
            if (originalName != name) {
                controller->RegisterClip(originalName, std::move(clipCopy));
            }
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

        // Game-thread per-instance work: advance the controller, sample the
        // pose for this frame, and stash it on the instance. The palette and
        // the skinned vertex stream are produced LATER on the render thread
        // — by `DispatchSkinningGpu` for the GPU compute path, which calls
        // `UpdatePaletteFromPose` itself just before recording the dispatch.
        //
        // We intentionally DO NOT call `UpdatePaletteFromPose` or `SkinCpu`
        // here: the GPU path renders the SkinnedVertex[] stream on the GPU
        // and ignores `m_outputVertexData`, so the per-frame 120 k-vertex
        // CPU skinning pass would be pure waste. (`UploadAll` is now a
        // bind-pose-only fallback for backends without compute support; it
        // uploads `m_outputVertexData` seeded once by `Bind()`, which is
        // sufficient for the bind-pose render and never benefits from a
        // per-frame `SkinCpu`.)
        const Skeleton* skeleton = w.instance->GetSkeleton();
        if (skeleton != nullptr && w.controller != nullptr) {
            w.controller->Update(dt);
            Pose pose;
            w.controller->SamplePose(*skeleton, pose);
            w.instance->SetPose(pose);
        }
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

    // Snapshot only the instances that actually produced new skinned bytes
    // this frame. `UploadContext::UploadBuffer` allocates a fresh DX12 staging
    // committed resource per call, which is expensive (~0.1-1 ms), so skipping
    // unchanged instances is the single largest win for static / idle meshes.
    struct DirtySnapshot {
        uint32_t                 meshId;
        std::vector<uint8_t>     bytes;
        SkeletalMeshInstance*    instance;
    };
    std::vector<DirtySnapshot> snapshots;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        snapshots.reserve(m_records.size());
        for (auto& rec : m_records) {
            if (!rec.instance) continue;
            if (!rec.instance->IsOutputDirty()) continue;
            const auto& bytes = rec.instance->GetOutputVertexData();
            if (bytes.empty()) continue;
            snapshots.push_back({rec.instance->GetOutputMeshId(), bytes, rec.instance.get()});
        }
    }
    if (snapshots.empty()) return;

    UploadContext upload(device);
    for (auto& snap : snapshots) {
        const GPUMesh* gmesh = renderer.GetGPUMesh(snap.meshId);
        if (gmesh == nullptr || !gmesh->IsValid()) continue;
        if (!upload.UploadBuffer(cmdList, gmesh->vertexBuffer, snap.bytes.data(), snap.bytes.size())) {
            UZ_LOG(Animation, Warning,
                   "SkeletalMeshSubsystem::UploadAll: UploadBuffer failed for meshId {}", snap.meshId);
            continue;
        }
        // Consume the dirty flag now that the copy is recorded; if a new
        // SkinCpu pass sets the flag again later this frame, we'll pick it
        // up on the next UploadAll call (not expected in normal tick order).
        snap.instance->ClearOutputDirty();
    }
    // The command list still holds unsubmitted CopyBuffer commands that
    // reference these staging buffers. Destroying them now (as
    // ReleaseStaging() would) leaves the GPU reading freed memory and
    // produces corrupted vertex data (visible as stray geometry across the
    // screen). Hand ownership to the renderer's deferred-destroy queue,
    // which fires on the next frame AFTER the GPU fence has completed.
    for (RHIBufferHandle staging : upload.TakeStagingBuffers()) {
        renderer.QueueBufferDestroy(staging);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU skinning (gpu_skinning_cs.hlsl)
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/// Lazily creates the per-instance GPU resources backing the skinning pass:
/// - input SRV: `sourceMesh->skinnedVertexData` uploaded once (Default heap);
/// - palette SRV: Default-heap buffer of `kMaxBones × 64 B`, identity-seeded
///   via a single staging copy and then refreshed each frame from the per-
///   instance staging path in `DispatchSkinningGpu`;
/// - output UAV: pulled from the GPUMesh's vertex buffer.
///
/// Returns true once all three handles are valid on the instance. Returns
/// false (silently) while the GPUMesh is still pending upload or the RHI
/// doesn't expose bindless descriptors.
bool EnsureGpuSkinningResources(SkeletalMeshInstance& inst,
                                 IRHIDevice* device,
                                 IRHICommandList* cmdList,
                                 UploadContext& upload,
                                 SceneRenderer& renderer) {
    if (inst.GetGpuInputBuffer().IsValid() &&
        inst.GetGpuPaletteBuffer().IsValid() &&
        inst.GetGpuOutputUav() != UINT32_MAX) {
        return true;
    }

    const Mesh* src = inst.GetSourceMesh();
    if (!src || src->skinnedVertexData.empty()) return false;

    const GPUMesh* gmesh = renderer.GetGPUMesh(inst.GetOutputMeshId());
    if (gmesh == nullptr || !gmesh->IsValid()) return false;

    // ── Output UAV index ─────────────────────────────────────────────────
    const uint32_t outputUav = device->GetBufferUAVIndex(gmesh->vertexBuffer);
    if (outputUav == UINT32_MAX) {
        UZ_LOG(Animation, Warning,
               "GPU skinning: output VB has no UAV (gpuSkinned flag missing?) for instance");
        return false;
    }
    inst.SetGpuOutputUav(outputUav);

    // ── Skinned input buffer (one-time upload) ──────────────────────────
    if (!inst.GetGpuInputBuffer().IsValid()) {
        // stride=0 → raw ByteAddressBuffer SRV so the CPU-packed 56 B
        // SkinnedVertex layout is read at exact offsets in HLSL without
        // the D3D12 vector-packing padding that a StructuredBuffer<T>
        // would inject.
        RHIBufferDesc inDesc{};
        inDesc.sizeBytes = static_cast<uint64_t>(src->skinnedVertexData.size());
        inDesc.stride    = 0;
        inDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        inDesc.memory    = RHIMemoryType::Default;
        inDesc.debugName = "SkeletalMesh_SkinnedInput";
        RHIBufferHandle inBuf = device->CreateBuffer(inDesc);
        if (!inBuf.IsValid()) {
            UZ_LOG(Animation, Error, "GPU skinning: CreateBuffer(input) failed");
            return false;
        }
        if (!upload.UploadBuffer(cmdList, inBuf,
                                 src->skinnedVertexData.data(),
                                 src->skinnedVertexData.size())) {
            UZ_LOG(Animation, Error, "GPU skinning: UploadBuffer(input) failed");
            device->DestroyBuffer(inBuf);
            return false;
        }
        // UploadBuffer leaves the buffer in CopyDst state. The compute
        // shader reads it as a StructuredBuffer SRV, so we must transition
        // to ShaderResource before the first dispatch can read fresh data.
        RHIBarrierDesc inToSrv{};
        inToSrv.buffer      = inBuf;
        inToSrv.stateBefore = RHIResourceState::CopyDst;
        inToSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&inToSrv, 1);

        const uint32_t inSrv = device->GetBufferSRVIndex(inBuf);
        if (inSrv == UINT32_MAX) {
            UZ_LOG(Animation, Error, "GPU skinning: input buffer has no SRV index");
            device->DestroyBuffer(inBuf);
            return false;
        }
        inst.SetGpuInput(inBuf, inSrv);
    }

    // ── Palette buffer (DEFAULT heap, populated via per-frame staging) ──
    // We deliberately use a Default-heap destination plus a per-instance,
    // PERSISTENTLY-MAPPED Upload-heap staging buffer that we reuse every
    // frame.
    //
    // History: a previous version called `UploadContext::UploadBuffer` once
    // per frame, which allocated a fresh staging through D3D12MA every
    // frame. After a few hundred frames the upload pool fragmented enough
    // to start failing every staging allocation with `DXGI_ERROR_DEVICE_
    // REMOVED` (0x887A0005), spamming the log without breaking rendering
    // because the dispatch then ran with the previous frame's stale
    // palette. A single permanent staging eliminates the churn.
    if (!inst.GetGpuPaletteBuffer().IsValid()) {
        static constexpr uint64_t kMaxBones = 256;
        // Destination — Default heap, raw byte-address SRV.
        RHIBufferDesc pDesc{};
        pDesc.sizeBytes = kMaxBones * sizeof(Mat4);
        pDesc.stride    = 0;
        pDesc.usage     = RHIBufferUsage::StructuredBuffer | RHIBufferUsage::CopyDst;
        pDesc.memory    = RHIMemoryType::Default;
        pDesc.debugName = "SkeletalMesh_Palette";
        RHIBufferHandle pBuf = device->CreateBuffer(pDesc);
        if (!pBuf.IsValid()) {
            UZ_LOG(Animation, Error, "GPU skinning: CreateBuffer(palette) failed");
            return false;
        }
        const uint32_t pSrv = device->GetBufferSRVIndex(pBuf);
        if (pSrv == UINT32_MAX) {
            UZ_LOG(Animation, Error, "GPU skinning: palette SRV index missing");
            device->DestroyBuffer(pBuf);
            return false;
        }

        // Persistent staging — Upload heap, mapped once and reused.
        RHIBufferDesc sDesc{};
        sDesc.sizeBytes = kMaxBones * sizeof(Mat4);
        sDesc.stride    = 0;
        sDesc.usage     = RHIBufferUsage::CopySrc;
        sDesc.memory    = RHIMemoryType::Upload;
        sDesc.debugName = "SkeletalMesh_PaletteStaging";
        RHIBufferHandle sBuf = device->CreateBuffer(sDesc);
        if (!sBuf.IsValid()) {
            UZ_LOG(Animation, Error, "GPU skinning: CreateBuffer(palette staging) failed");
            device->DestroyBuffer(pBuf);
            return false;
        }
        void* sMapped = device->MapBuffer(sBuf);
        if (!sMapped) {
            UZ_LOG(Animation, Error, "GPU skinning: MapBuffer(palette staging) failed");
            device->DestroyBuffer(sBuf);
            device->DestroyBuffer(pBuf);
            return false;
        }

        // Seed identity into the staging, copy into the destination, transition
        // destination to ShaderResource. After this point, every per-frame
        // upload is just memcpy + CopyBufferRegion + 2 barriers — no D3D12MA
        // allocations.
        Mat4* slots = static_cast<Mat4*>(sMapped);
        const Mat4 identity = Mat4::Identity();
        for (uint64_t i = 0; i < kMaxBones; ++i) slots[i] = identity;

        cmdList->CopyBuffer(sBuf, 0, pBuf, 0, kMaxBones * sizeof(Mat4));

        RHIBarrierDesc palToSrv{};
        palToSrv.buffer      = pBuf;
        palToSrv.stateBefore = RHIResourceState::CopyDst;
        palToSrv.stateAfter  = RHIResourceState::ShaderResource;
        cmdList->ResourceBarrier(&palToSrv, 1);

        inst.SetGpuPalette(pBuf, pSrv, nullptr);
        inst.SetGpuPaletteStaging(sBuf, sMapped);
    }

    return true;
}

} // anonymous namespace

void SkeletalMeshSubsystem::DispatchSkinningGpu(IRHIDevice* device,
                                                 IRHICommandList* cmdList,
                                                 SceneRenderer& renderer,
                                                 PipelineLibrary& pipelines,
                                                 rg::RgStateRegistry& states) {
    if (device == nullptr || cmdList == nullptr) return;

    RHIPipelineHandle skinningPso = pipelines.GetGpuSkinning();
    if (!skinningPso.IsValid()) return;

    // Snapshot instance pointers under the lock; run the compute work outside
    // the lock (the instance pointers are stable for the frame lifetime).
    std::vector<SkeletalMeshInstance*> instances;
    {
        std::lock_guard<std::mutex> lock(m_registryMutex);
        instances.reserve(m_records.size());
        for (auto& rec : m_records) {
            if (!rec.instance) continue;
            if (!rec.instance->IsValid()) continue;
            instances.push_back(rec.instance.get());
        }
    }
    if (instances.empty()) return;

    UploadContext upload(device);

    // Bind the compute pipeline once; per-instance state is pushed via root
    // constants and bindless descriptor indices.
    cmdList->SetPipeline(skinningPso);

    for (SkeletalMeshInstance* inst : instances) {
        if (!EnsureGpuSkinningResources(*inst, device, cmdList, upload, renderer)) {
            continue;
        }

        inst->UpdatePaletteFromPose();

        // ── Diagnostic: dump m_palette[5] every 30 frames to confirm the
        // pose is being animated (rotations should appear in row 0/1/2).
        {
            static uint32_t s_frame = 0;
            ++s_frame;
            if ((s_frame % 30u) == 0u && !inst->GetPalette().empty() &&
                inst->GetPalette().size() > 5) {
                const float* m = reinterpret_cast<const float*>(&inst->GetPalette()[5]);
                UZ_LOG(Animation, Warning,
                       "  pal[5] @ frame {}: r0=({:.4f},{:.4f},{:.4f},{:.4f}) "
                       "r1=({:.4f},{:.4f},{:.4f},{:.4f}) "
                       "r2=({:.4f},{:.4f},{:.4f},{:.4f}) "
                       "r3=({:.4f},{:.4f},{:.4f},{:.4f})",
                       s_frame,
                       m[0], m[1], m[2], m[3],
                       m[4], m[5], m[6], m[7],
                       m[8], m[9], m[10], m[11],
                       m[12], m[13], m[14], m[15]);
            }
        }

        // ── Per-frame palette upload via the persistent staging buffer.
        // memcpy m_palette into the upload-heap staging, then issue a
        // CopyBufferRegion into the Default-heap palette destination.
        // No D3D12MA allocation per frame — the staging buffer was created
        // ONCE in EnsureGpuSkinningResources.
        const auto& palette = inst->GetPalette();
        void* stagingMapped = inst->GetGpuPaletteStagingMappedPtr();
        if (!palette.empty() && stagingMapped) {
            const size_t bytes = palette.size() * sizeof(Mat4);
            std::memcpy(stagingMapped, palette.data(), bytes);

            RHIBarrierDesc palToCopy{};
            palToCopy.buffer      = inst->GetGpuPaletteBuffer();
            palToCopy.stateBefore = RHIResourceState::ShaderResource;
            palToCopy.stateAfter  = RHIResourceState::CopyDst;
            cmdList->ResourceBarrier(&palToCopy, 1);

            cmdList->CopyBuffer(inst->GetGpuPaletteStagingBuffer(), 0,
                                inst->GetGpuPaletteBuffer(), 0, bytes);

            RHIBarrierDesc palToSrv{};
            palToSrv.buffer      = inst->GetGpuPaletteBuffer();
            palToSrv.stateBefore = RHIResourceState::CopyDst;
            palToSrv.stateAfter  = RHIResourceState::ShaderResource;
            cmdList->ResourceBarrier(&palToSrv, 1);
        }

        GPUMesh* gmesh = renderer.GetGPUMesh(inst->GetOutputMeshId());
        if (gmesh == nullptr || !gmesh->IsValid()) continue;

        const uint32_t vertexCount = inst->GetSourceMesh()->GetVertexCount();
        if (vertexCount == 0) continue;

        const uint64_t vertexBytes = static_cast<uint64_t>(vertexCount) * gmesh->vertexStride;
        gmesh->prevPoseValid = gmesh->prevVertexBuffer.IsValid();
        if (!gmesh->prevVertexBuffer.IsValid()) {
            RHIBufferDesc previous{};
            previous.sizeBytes = vertexBytes;
            previous.stride = gmesh->vertexStride;
            previous.usage = RHIBufferUsage::UnorderedAccess | RHIBufferUsage::StructuredBuffer
                | RHIBufferUsage::CopyDst | RHIBufferUsage::CopySrc;
            previous.memory = RHIMemoryType::Default;
            previous.debugName = "SkeletalMesh_PreviousPose";
            gmesh->prevVertexBuffer = device->CreateBuffer(previous);
            if (!gmesh->prevVertexBuffer.IsValid()) continue;
            gmesh->prevVbSrvIndex = device->GetBufferSRVIndex(gmesh->prevVertexBuffer);
        }
        // Preserve the last dispatch before overwriting the output. On first
        // dispatch this copies Bind()'s seeded pose; scene sync uses current VB
        // until prevPoseValid, so first sight has zero deformation velocity.
        states.Transition(cmdList, gmesh->vertexBuffer, RHIResourceState::CopySrc);
        states.Transition(cmdList, gmesh->prevVertexBuffer, RHIResourceState::CopyDst);
        cmdList->CopyBuffer(gmesh->vertexBuffer, 0, gmesh->prevVertexBuffer, 0, vertexBytes);
        states.Transition(cmdList, gmesh->prevVertexBuffer, RHIResourceState::ShaderResource);

        // The GBuffer vertex shader reads this buffer as a bindless
        // StructuredBuffer SRV (see gbuffer_pass.cpp: bindless path uses
        // `mesh.vbSrvIndex`, not `SetVertexBuffer`). So the correct target
        // state around the compute dispatch is ShaderResource, not
        // VertexBuffer — the latter would leave the VS reading the buffer
        // from the wrong D3D12 resource state and blocking the SRV.
        states.Transition(cmdList, gmesh->vertexBuffer, RHIResourceState::UnorderedAccess);

        // Push constants (4 uints: inputSrv, paletteSrv, outputUav, vertexCount).
        const uint32_t push[4] = {
            inst->GetGpuInputSrv(),
            inst->GetGpuPaletteSrv(),
            inst->GetGpuOutputUav(),
            vertexCount
        };
        cmdList->SetRootConstants(0, push, 4);

        const uint32_t groupsX = (vertexCount + 63u) / 64u;
        cmdList->Dispatch(groupsX, 1, 1);

        // UAV → ShaderResource so the GBuffer VS can read the freshly
        // written vertices via the bindless SRV path.
        states.Transition(cmdList, gmesh->vertexBuffer, RHIResourceState::ShaderResource);
    }

    // Defer staging destruction until the GPU fence of this frame elapses.
    for (RHIBufferHandle staging : upload.TakeStagingBuffers()) {
        renderer.QueueBufferDestroy(staging);
    }
}

} // namespace uz
