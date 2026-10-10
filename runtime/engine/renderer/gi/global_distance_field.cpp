/// UzEngine Global Distance Field — Implementation.
///
/// Creates and manages 4 cascaded clipmap RGBA16F 3D textures for world-space
/// signed distance field tracing. Each frame, dirty clipmaps are recomposited
/// from all mesh SDFs via a compute shader dispatch.
///
/// @ref Wright 2015 "Dynamic Occlusion with Signed Distance Fields" — cascaded GDF layout.

#include "global_distance_field.h"

#include "mesh_sdf_baker.h"
#include "cb_ring_allocator.h"
#include "log.h"
#include "rhi.h"
#include "cvar.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// Diagnostic (followup-13): compare clipmap 0's raw distance field before and after each recentre
// at the same world positions, and log the difference. Reads the volume back twice per event.
static auto* cv_GdfFieldCompare = UZ_CVAR_BOOL("r.GI.GDF.Diagnostic.FieldCompare", false,
    "Diagnostic: on a recentre of the selected clipmap, read the raw field back before and after the "
    "recomposite and log how much it changed at overlapping world positions");
static auto* cv_GdfFieldCompareCascade = UZ_CVAR_INT("r.GI.GDF.Diagnostic.FieldCompareCascade", 0,
    "Diagnostic: which clipmap the field comparison reads back (0-3). Cascade 1 is four times coarser "
    "than 0 and its recentres are the candidate H-50 left open", 0, 3);

/// IEEE half to float, for the R16F readback. Local so this file needs no debug-pass include.
static float GdfHalfToFloat(uint16_t h) {
    const uint32_t sign = (h & 0x8000u) << 16;
    uint32_t exp = (h >> 10) & 0x1Fu;
    uint32_t man = h & 0x3FFu;
    uint32_t bits;
    if (exp == 0) {
        if (man == 0) {
            bits = sign;
        } else {
            exp = 127 - 15 + 1;
            while ((man & 0x400u) == 0) { man <<= 1; --exp; }
            man &= 0x3FFu;
            bits = sign | (exp << 23) | (man << 13);
        }
    } else if (exp == 31) {
        bits = sign | 0x7F800000u | (man << 13);
    } else {
        bits = sign | ((exp + 127 - 15) << 23) | (man << 13);
    }
    float f;
    std::memcpy(&f, &bits, sizeof(f));
    return f;
}

// Block B.2: clipmaps are stored toroidally (world voxel k in texel k mod resolution), so a
// recentre only recomposes the slabs that entered the window. 0 = recompose the whole window
// on every recentre (the pre-2026-10-05 cost; the storage layout is the same either way).
static auto* cv_GdfIncremental = UZ_CVAR_BOOL("r.GI.GDF.Incremental", true, "Global distance field: recompose only the entering slabs on a recentre (0 = whole window)");
// Block B.2 phase 3: stored distances are clamped to this many voxels of each clipmap, which
// bounds how far an instance change can reach, so a moved / added / removed instance (or a
// streamed texture) recomposes only its neighbourhood. 0 = untruncated field and whole-window
// recomposes on every scene change (the pre-2026-10-05 behaviour).
static auto* cv_GdfBandVoxels = UZ_CVAR_FLOAT("r.GI.GDF.BandVoxels", 32.0f, "Global distance field: clamp stored distances to this many voxels of the clipmap (0 = untruncated, every scene change rewrites whole windows)", 0.f, 256.f);
static auto* cv_GdfMaxRegions = UZ_CVAR_INT("r.GI.GDF.MaxRegions", 8, "Global distance field: more instance changes than this in one frame rewrite the whole windows instead of regions", 1, 64);
static auto* cv_GdfResolution = UZ_CVAR_INT("r.GI.GDF.Resolution", 64,
    "Voxels per clipmap axis (requires scene reload)", 32, 256);

// ─────────────────────────────────────────────────────────────────────────────
// Destruction
// ─────────────────────────────────────────────────────────────────────────────

GlobalDistanceField::~GlobalDistanceField() {
    DestroyResources();
}

// ─────────────────────────────────────────────────────────────────────────────
// CreateResources
// ─────────────────────────────────────────────────────────────────────────────

bool GlobalDistanceField::CreateResources(IRHIDevice* device) {
    if (!device) {
        UZ_LOG(Renderer, Error, "GlobalDistanceField::CreateResources: null device");
        return false;
    }

    if (m_initialized) {
        DestroyResources();
    }

    m_device = device;
    const uint32_t resolution = static_cast<uint32_t>(cv_GdfResolution->value);

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        clip.extent = kClipmapExtents[i];
        clip.center = Vec3{0.0f, 0.0f, 0.0f};
        clip.dirty  = true;
        clip.hasData = false;

        RHITextureDesc texDesc{};
        texDesc.width            = resolution;
        texDesc.height           = resolution;
        texDesc.depthOrArraySize = static_cast<uint16_t>(resolution);
        texDesc.mipLevels        = 1;
        texDesc.format           = RHIFormat::RGBA16Float;
        texDesc.usage            = RHITextureUsage::ShaderResource
                                 | RHITextureUsage::UnorderedAccess;
        texDesc.is3D             = true;

        // Build a unique debug name per cascade
        char debugName[64];
        std::snprintf(debugName, sizeof(debugName), "GDF_Clipmap%u", i);
        texDesc.debugName = debugName;

        clip.texture = device->CreateTexture(texDesc);
        if (!clip.texture.IsValid()) {
            UZ_LOG(Renderer, Error,
                   "GlobalDistanceField: failed to create clipmap {} texture ({}^3 RGBA16F)",
                   i, resolution);
            DestroyResources();
            return false;
        }

        clip.srvIndex = device->GetTextureSRVIndex(clip.texture);
        clip.uavIndex = device->GetTextureUAVIndex(clip.texture);
        texDesc.format = RHIFormat::R16Float;
        std::snprintf(debugName, sizeof(debugName), "GDF_RawDistance%u", i);
        clip.rawTexture = device->CreateTexture(texDesc);
        if (!clip.rawTexture.IsValid()) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: failed to create raw clipmap {} ({}^3 R16F)", i, resolution);
            DestroyResources();
            return false;
        }
        clip.rawSrvIndex = device->GetTextureSRVIndex(clip.rawTexture);
        clip.rawUavIndex = device->GetTextureUAVIndex(clip.rawTexture);
        if (clip.srvIndex == UINT32_MAX || clip.uavIndex == UINT32_MAX
            || clip.rawSrvIndex == UINT32_MAX || clip.rawUavIndex == UINT32_MAX) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: missing descriptor for clipmap pair {}", i);
            DestroyResources();
            return false;
        }

        UZ_LOG(Renderer, Info,
               "GlobalDistanceField: clipmap {} created ({}^3 RGBA16F+R16F, extent={:.0f}cm, SRV={}, UAV={}, rawSRV={}, rawUAV={})",
               i, resolution, clip.extent, clip.srvIndex, clip.uavIndex, clip.rawSrvIndex, clip.rawUavIndex);
    }

    m_initialized = true;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// DestroyResources
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::DestroyResources() {
    if (!m_device) return;

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];
        if (clip.texture.IsValid()) {
            m_device->DestroyTexture(clip.texture);
            clip.texture  = RHITextureHandle{};
            clip.srvIndex = UINT32_MAX;
            clip.uavIndex = UINT32_MAX;
        }
        if (clip.rawTexture.IsValid()) {
            m_device->DestroyTexture(clip.rawTexture);
            clip.rawTexture = RHITextureHandle{};
        }
        clip.rawSrvIndex = UINT32_MAX;
        clip.rawUavIndex = UINT32_MAX;
        clip.dirty = true;
        clip.hasData = false;
    }

    m_initialized = false;
    m_meshDataHash = 0;
    m_meshDataCount = UINT32_MAX;
}

// ─────────────────────────────────────────────────────────────────────────────
// Update — recenter clipmaps on camera, mark dirty as needed
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::Update(const Vec3& cameraPos) {
    if (!m_initialized) return;
    ServiceFieldCompare();

    for (uint32_t i = 0; i < kNumClipmaps; ++i) {
        auto& clip = m_clipmaps[i];

        // Voxel size for this cascade: full extent / resolution
        /// Snap clipmap center to voxel grid to prevent swimming artifacts.
        const float voxelSize = GetVoxelSize(i);

        // Snap the clipmap center to voxel grid to avoid swimming artifacts.
        // Re-center when camera moves more than half a voxel from the current center.
        // The diagnostic phase offset shifts the snapping grid, so the same camera path crosses a
        // boundary at different frames (followup-13 item 1.5).
        // The window centre is always a whole number of voxels: toroidal storage (block B.2)
        // addresses texels by the world voxel index, so the voxel grid itself never moves. The
        // diagnostic phase offset therefore shifts WHEN a boundary is crossed (the event frames
        // still move with it, which is what followup-13 used it for), no longer the grid.
        const float phase = m_centrePhaseOffsetCm;
        Vec3 snappedCenter;
        snappedCenter.x = std::floor((cameraPos.x + phase) / voxelSize) * voxelSize;
        snappedCenter.y = std::floor((cameraPos.y + phase) / voxelSize) * voxelSize;
        snappedCenter.z = std::floor((cameraPos.z + phase) / voxelSize) * voxelSize;

        // Check if the center changed (moved by at least one voxel)
        const float dx = std::abs(snappedCenter.x - clip.center.x);
        const float dy = std::abs(snappedCenter.y - clip.center.y);
        const float dz = std::abs(snappedCenter.z - clip.center.z);

        if (!m_centreFrozen && (dx >= voxelSize || dy >= voxelSize || dz >= voxelSize)) {
            clip.prevCenter = clip.center;
            clip.recentred  = true;
            // Whole voxels the window moved, accumulated until the composite consumes them
            // (Update may run twice per frame). Both centres are voxel multiples, so lround
            // absorbs the float error.
            clip.shift[0] += static_cast<int32_t>(std::lround((snappedCenter.x - clip.center.x) / voxelSize));
            clip.shift[1] += static_cast<int32_t>(std::lround((snappedCenter.y - clip.center.y) / voxelSize));
            clip.shift[2] += static_cast<int32_t>(std::lround((snappedCenter.z - clip.center.z) / voxelSize));
            clip.center = snappedCenter;
            clip.dirty  = true;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CompositeFromMeshSDFs — dispatch compute shader for each dirty clipmap
// ─────────────────────────────────────────────────────────────────────────────

void GlobalDistanceField::CompositeFromMeshSDFs(
    IRHICommandList* cmdList,
    const MeshSDF* meshSDFs,
    const Mat4* meshTransforms,
    uint32_t meshSdfCount,
    RHIPipelineHandle compositePipeline,
    CBRingAllocator* cbRing,
    RHIBufferHandle,
    uint32_t,
    const void*,
    uint32_t)
{
    if (!m_initialized || !cmdList || !compositePipeline.IsValid() || !cbRing) {
        return;
    }

    if (meshSdfCount > 0 && !meshSDFs) {
        return;
    }

    std::vector<GdfMeshSdfGPU> gpuMeshes;
    gpuMeshes.reserve(meshSdfCount);
    std::vector<InstanceRecord> records;
    records.reserve(meshSdfCount);
    for (uint32_t m = 0; m < meshSdfCount; ++m) {
        const auto& sdf = meshSDFs[m];
        if (!sdf.valid || sdf.srvIndex == UINT32_MAX || sdf.resolution == 0
            || sdf.volumeExtent[0] <= 0 || sdf.volumeExtent[1] <= 0 || sdf.volumeExtent[2] <= 0) continue;

        GdfMeshSdfGPU entry{};
        entry.originX = sdf.volumeOrigin[0];
        entry.originY = sdf.volumeOrigin[1];
        entry.originZ = sdf.volumeOrigin[2];
        entry.extentX = sdf.volumeExtent[0];
        entry.extentY = sdf.volumeExtent[1];
        entry.extentZ = sdf.volumeExtent[2];
        entry.surfaceBias = sdf.twoSided
            ? std::max({entry.extentX, entry.extentY, entry.extentZ}) / sdf.resolution : 0.0f;
        std::memcpy(entry.invTransform, sdf.invTransform, sizeof(entry.invTransform));
        entry.srvIndex   = sdf.srvIndex;
        entry.resolution = sdf.resolution;
        entry.albedoSrv = sdf.albedoSrv;
        entry.metallicSrv = sdf.metallicSrv;
        std::memcpy(entry.baseColorTint, sdf.baseColorTint, sizeof(entry.baseColorTint));
        entry.metallicMultiplier = sdf.metallicMultiplier;
        entry.materialFlags = sdf.materialFlags;
        entry.trianglesSrv = sdf.triangleSrv;
        entry.triangleCountAndFlags = sdf.triangleBuffer.IsValid() ? sdf.triangleCountAndFlags : 0u;

        // Gershgorin bounds the largest eigenvalue of A^T A, where A is the
        // world-to-local linear transform. Its inverse square root bounds
        // distances conservatively even for nonuniform scale and shear.
        double maxRowSum = 0.0;
        for (uint32_t row = 0; row < 3; ++row) {
            double rowSum = 0.0;
            for (uint32_t col = 0; col < 3; ++col) {
                double gram = 0.0;
                for (uint32_t k = 0; k < 3; ++k) {
                    gram += static_cast<double>(sdf.invTransform[k * 4 + row])
                          * sdf.invTransform[k * 4 + col];
                }
                rowSum += std::abs(gram);
            }
            maxRowSum = std::max(maxRowSum, rowSum);
        }
        if (!std::isfinite(maxRowSum) || maxRowSum <= 0.0) continue;
        entry.distanceScale = static_cast<float>(1.0 / std::sqrt(maxRowSum));
        gpuMeshes.push_back(entry);
        // World bounds of the SDF volume (the eight corners through the world matrix), expanded by
        // the two-sided surface bias scaled by the largest axis stretch: the per-entity diff
        // (block B.2 phase 3) derives the recompose regions from them.
        InstanceRecord record{};
        record.key   = sdf.instanceKey;
        record.entry = entry;
        if (meshTransforms) {
            const Mat4& world = meshTransforms[m];
            Vec3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
            for (uint32_t corner = 0; corner < 8; ++corner) {
                const Vec3 local{sdf.volumeOrigin[0] + ((corner & 1) ? sdf.volumeExtent[0] : -sdf.volumeExtent[0]),
                                 sdf.volumeOrigin[1] + ((corner & 2) ? sdf.volumeExtent[1] : -sdf.volumeExtent[1]),
                                 sdf.volumeOrigin[2] + ((corner & 4) ? sdf.volumeExtent[2] : -sdf.volumeExtent[2])};
                const Vec3 p = world * local;
                lo = lo.min(p);
                hi = hi.max(p);
            }
            const float stretch = std::max({std::sqrt(world.m._11 * world.m._11 + world.m._12 * world.m._12 + world.m._13 * world.m._13),
                                            std::sqrt(world.m._21 * world.m._21 + world.m._22 * world.m._22 + world.m._23 * world.m._23),
                                            std::sqrt(world.m._31 * world.m._31 + world.m._32 * world.m._32 + world.m._33 * world.m._33)});
            const float margin = entry.surfaceBias * std::max(stretch, 1.0f);
            record.lo = Vec3{lo.x - margin, lo.y - margin, lo.z - margin};
            record.hi = Vec3{hi.x + margin, hi.y + margin, hi.z + margin};
        } else {
            record.lo = Vec3{-1e30f, -1e30f, -1e30f};
            record.hi = Vec3{1e30f, 1e30f, 1e30f};
        }
        records.push_back(record);
    }

    const auto meshCount = static_cast<uint32_t>(gpuMeshes.size());
    uint64_t meshHash = 14695981039346656037ull;
    for (const auto& entry : gpuMeshes) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(&entry);
        for (size_t b = 0; b < sizeof(entry); ++b) {
            meshHash = (meshHash ^ bytes[b]) * 1099511628211ull;
        }
    }
    m_meshEntries = gpuMeshes;
    m_meshEntriesHash = meshHash ^ (static_cast<uint64_t>(meshCount) * 0x9E3779B97F4A7C15ull);
    bool needsComposite = false;
    const bool sceneChanged = meshHash != m_meshDataHash || meshCount != m_meshDataCount;
    // Per-entity diff (block B.2 phase 3): which instances changed since last frame, and the world
    // box each change can reach (old ∪ new bounds; the band bounds the reach, added per clipmap).
    std::stable_sort(records.begin(), records.end(), [](const InstanceRecord& a, const InstanceRecord& b) { return a.key < b.key; });
    std::vector<ChangedRegion> changes;
    {
        size_t i = 0, j = 0;
        while (i < m_prevInstances.size() || j < records.size()) {
            if (j >= records.size() || (i < m_prevInstances.size() && m_prevInstances[i].key < records[j].key)) {
                changes.push_back({m_prevInstances[i].lo, m_prevInstances[i].hi});   // removed
                ++i;
            } else if (i >= m_prevInstances.size() || records[j].key < m_prevInstances[i].key) {
                changes.push_back({records[j].lo, records[j].hi});                    // added
                ++j;
            } else {
                if (std::memcmp(&m_prevInstances[i].entry, &records[j].entry, sizeof(GdfMeshSdfGPU)) != 0) {
                    changes.push_back({m_prevInstances[i].lo.min(records[j].lo), m_prevInstances[i].hi.max(records[j].hi)});
                }
                ++i; ++j;
            }
        }
    }
    m_prevInstances = std::move(records);
    m_changedRegions.clear();
    const float bandVoxels = cv_GdfBandVoxels->value;
    bool everyClipHasData = true;
    for (const auto& clip : m_clipmaps) everyClipHasData &= clip.hasData;
    const bool regional = cv_GdfIncremental->value != 0.0f && bandVoxels > 0.0f && sceneChanged
                       && !changes.empty() && changes.size() <= static_cast<size_t>(cv_GdfMaxRegions->value) && everyClipHasData;
    if (regional) {
        // Consumers see the change boxes expanded by the finest band (plus a voxel of margin).
        const float reach = bandVoxels * GetVoxelSize(0) + GetVoxelSize(0);
        for (const auto& c : changes) {
            m_changedRegions.push_back({Vec3{c.lo.x - reach, c.lo.y - reach, c.lo.z - reach},
                                        Vec3{c.hi.x + reach, c.hi.y + reach, c.hi.z + reach}});
        }
    }
    m_incremental = cv_GdfIncremental->value != 0.0f;
    for (uint32_t clipIndex = 0; clipIndex < kNumClipmaps; ++clipIndex) {
        auto& clip = m_clipmaps[clipIndex];
        clip.band = bandVoxels > 0.0f ? bandVoxels * GetVoxelSize(clipIndex) : 0.0f;
        clip.changeRegions.clear();
        if (sceneChanged && !regional) clip.full = true;   // every voxel may change: whole window
        clip.dirty |= sceneChanged;
        needsComposite |= clip.dirty;
    }
    if (!needsComposite) return;

    const uint32_t batchCount = std::max(1u, (meshCount + kGdfMeshesPerBatch - 1) / kGdfMeshesPerBatch);
    std::vector<CBAllocation> batches;
    batches.reserve(batchCount);
    for (uint32_t batch = 0; batch < batchCount; ++batch) {
        const uint32_t offset = batch * kGdfMeshesPerBatch;
        const uint32_t count = std::min(kGdfMeshesPerBatch, meshCount - offset);
        // Only the entries the shader reads (meshCount): a full 64 KiB batch per region and clipmap
        // exhausted the 2 MiB frame ring when several instances changed in one frame (block B.3).
        const size_t batchBytes = static_cast<size_t>(std::max(1u, count)) * sizeof(GdfMeshSdfGPU);
        auto allocation = cbRing->Allocate(batchBytes);
        if (!allocation.cpuPtr) {
            UZ_LOG(Renderer, Error, "GlobalDistanceField: failed to allocate mesh SDF batch {}", batch);
            return;
        }
        std::memset(allocation.cpuPtr, 0, batchBytes);
        if (count > 0) {
            std::memcpy(allocation.cpuPtr, gpuMeshes.data() + offset, count * sizeof(GdfMeshSdfGPU));
        }
        batches.push_back(allocation);
    }

    if (regional) {
        for (uint32_t clipIndex = 0; clipIndex < kNumClipmaps; ++clipIndex) {
            auto& clip = m_clipmaps[clipIndex];
            const uint32_t res = m_device->GetTextureDesc(clip.texture).width;
            const float voxelSize = GetVoxelSize(clipIndex);
            // An instance change reaches `band` beyond its bounds (plus the two-sided bias the
            // composite adds, one voxel of the clipmap); the region is that box in window voxels.
            const float reach = clip.band + 2.0f * voxelSize;
            const float origin[3] = {clip.center.x - clip.extent, clip.center.y - clip.extent, clip.center.z - clip.extent};
            for (const auto& c : changes) {
                const float lo[3] = {c.lo.x - reach, c.lo.y - reach, c.lo.z - reach};
                const float hi[3] = {c.hi.x + reach, c.hi.y + reach, c.hi.z + reach};
                ClipmapData::RecomposeRegion region{};
                bool empty = false;
                for (uint32_t a = 0; a < 3; ++a) {
                    const int64_t v0 = std::max<int64_t>(0, static_cast<int64_t>(std::floor((lo[a] - origin[a]) / voxelSize)));
                    const int64_t v1 = std::min<int64_t>(res, static_cast<int64_t>(std::ceil((hi[a] - origin[a]) / voxelSize)));
                    if (v1 <= v0) { empty = true; break; }
                    region.origin[a] = static_cast<uint32_t>(v0);
                    region.size[a]   = static_cast<uint32_t>(v1 - v0);
                }
                if (empty) continue;
                // Instances whose influence (bounds + band) reaches the region.
                std::vector<GdfMeshSdfGPU> overlapping;
                for (const auto& record : m_prevInstances) {
                    if (record.lo.x - clip.band > hi[0] || record.hi.x + clip.band < lo[0]
                     || record.lo.y - clip.band > hi[1] || record.hi.y + clip.band < lo[1]
                     || record.lo.z - clip.band > hi[2] || record.hi.z + clip.band < lo[2]) continue;
                    overlapping.push_back(record.entry);
                }
                region.meshCount = static_cast<uint32_t>(overlapping.size());
                const uint32_t regionBatches = std::max(1u, (region.meshCount + kGdfMeshesPerBatch - 1) / kGdfMeshesPerBatch);
                for (uint32_t batch = 0; batch < regionBatches; ++batch) {
                    const uint32_t offset = batch * kGdfMeshesPerBatch;
                    const uint32_t count = std::min(kGdfMeshesPerBatch, region.meshCount - offset);
                    const size_t batchBytes = static_cast<size_t>(std::max(1u, count)) * sizeof(GdfMeshSdfGPU);
                    auto allocation = cbRing->Allocate(batchBytes);
                    if (!allocation.cpuPtr) { region.batches.clear(); break; }
                    std::memset(allocation.cpuPtr, 0, batchBytes);
                    if (count > 0) std::memcpy(allocation.cpuPtr, overlapping.data() + offset, count * sizeof(GdfMeshSdfGPU));
                    region.batches.push_back(allocation);
                }
                if (region.batches.empty()) { clip.full = true; clip.changeRegions.clear(); break; }
                clip.changeRegions.push_back(std::move(region));
            }
        }
    }
    cmdList->SetPipeline(compositePipeline);
    for (auto& clip : m_clipmaps) {
        if (!clip.dirty) continue;
        RHIBarrierDesc toUav{};
        toUav.texture     = clip.texture;
        toUav.stateBefore = clip.hasData ? RHIResourceState::ShaderResource : RHIResourceState::Common;
        toUav.stateAfter  = RHIResourceState::UnorderedAccess;
        RHIBarrierDesc initialBarriers[2] = {toUav, toUav};
        initialBarriers[1].texture = clip.rawTexture;
        // Diagnostic (followup-13, block B.2 gate G1): on a recentre, the slab recompose of this
        // frame is copied out and a full recompose of the same window is scheduled for the next
        // frame; the two must be identical. Only the selected cascade, only once at a time.
        const uint32_t compareCascade = static_cast<uint32_t>(
            std::clamp(static_cast<int>(cv_GdfFieldCompareCascade->value), 0, static_cast<int>(kNumClipmaps) - 1));
        const bool fieldCompareHere = cv_GdfFieldCompare->value != 0.0f && &clip == &m_clipmaps[compareCascade]
                                   && (clip.recentred || !clip.changeRegions.empty()) && clip.hasData && !m_fieldComparePending;
        if (!(cv_GdfFieldCompare->value != 0.0f)) clip.recentred = false;
        if (fieldCompareHere) {
            const uint32_t res = m_device->GetTextureDesc(clip.rawTexture).width;
            const uint32_t rowPitch = ((res * 2u + 255u) & ~255u);
            const uint64_t bytes = static_cast<uint64_t>(rowPitch) * res * res;
            RHIBufferDesc rb{};
            rb.sizeBytes = bytes;
            rb.stride    = 2;
            rb.usage     = RHIBufferUsage::CopyDst;
            rb.memory    = RHIMemoryType::Readback;
            rb.debugName = "GDF_FieldCompare_Before";
            m_fieldBefore = m_device->CreateBuffer(rb);
            rb.debugName = "GDF_FieldCompare_After";
            m_fieldAfter = m_device->CreateBuffer(rb);
            if (m_fieldBefore.IsValid() && m_fieldAfter.IsValid()) {
                m_fieldResolution = res;
                m_fieldRowPitch   = rowPitch;
                m_fieldExtent     = clip.extent;
                m_fieldOldCenter  = clip.prevCenter;
                m_fieldCascade    = compareCascade;
                m_fieldNewCenter  = clip.center;
                m_fieldAwaitSlab      = true;
                m_fieldBeforeCopied   = false;
                m_fieldComparePending = true;
            } else {
                UZ_LOG(Renderer, Error, "GDF field compare: readback allocation failed");
                if (m_fieldBefore.IsValid()) m_device->DestroyBuffer(m_fieldBefore);
                if (m_fieldAfter.IsValid())  m_device->DestroyBuffer(m_fieldAfter);
                m_fieldBefore = {}; m_fieldAfter = {};
            }
            clip.recentred = false;
        }
        cmdList->ResourceBarrier(initialBarriers, 2);
        GdfCompositeConstants constants{};
        constants.outputUav      = clip.uavIndex;
        constants.rawOutputUav   = clip.rawUavIndex;
        constants.resolution     = m_device->GetTextureDesc(clip.texture).width;
        constants.clipmapCenterX = clip.center.x;
        constants.clipmapCenterY = clip.center.y;
        constants.clipmapCenterZ = clip.center.z;
        constants.clipmapExtent  = clip.extent;
        // Toroidal storage (block B.2): world voxel k lives in texel k mod resolution, so a voxel
        // keeps its texel across recentres and a recentre only writes the voxels that entered the
        // window. windowOrigin = the window's first world voxel index modulo the resolution; the
        // shader adds it to the window-relative voxel before wrapping.
        const uint32_t res = constants.resolution;
        const float voxelSize = 2.0f * clip.extent / static_cast<float>(res);
        uint32_t windowOrigin[3] = {};
        {
            const float origin[3] = {clip.center.x - clip.extent, clip.center.y - clip.extent, clip.center.z - clip.extent};
            for (uint32_t a = 0; a < 3; ++a) {
                const int64_t k0 = std::lround(origin[a] / voxelSize);
                windowOrigin[a] = static_cast<uint32_t>(((k0 % static_cast<int64_t>(res)) + res) % res);
            }
        }
        constants.windowOrigin = windowOrigin[0] | (windowOrigin[1] << 9) | (windowOrigin[2] << 18);
        constants.band         = clip.band;
        // Regions to recompose, window-relative: the whole window; or one slab per axis the
        // window moved along (the entering side, |shift| planes; corners are written twice) plus
        // the neighbourhood of every changed instance, each with its own instance upload.
        std::vector<ClipmapData::RecomposeRegion> regions;
        const bool slabs = m_incremental && !clip.full && clip.hasData
            && (clip.shift[0] != 0 || clip.shift[1] != 0 || clip.shift[2] != 0)
            && std::abs(clip.shift[0]) < static_cast<int32_t>(res)
            && std::abs(clip.shift[1]) < static_cast<int32_t>(res)
            && std::abs(clip.shift[2]) < static_cast<int32_t>(res);
        const bool wholeWindow = clip.full || !clip.hasData || !m_incremental
            || (!slabs && clip.changeRegions.empty())
            || (!slabs && (clip.shift[0] != 0 || clip.shift[1] != 0 || clip.shift[2] != 0));
        if (wholeWindow) {
            ClipmapData::RecomposeRegion r{};
            for (uint32_t b = 0; b < 3; ++b) { r.origin[b] = 0; r.size[b] = res; }
            regions.push_back(std::move(r));
        } else {
            if (slabs) {
                for (uint32_t a = 0; a < 3; ++a) {
                    const int32_t shift = clip.shift[a];
                    if (shift == 0) continue;
                    ClipmapData::RecomposeRegion r{};
                    for (uint32_t b = 0; b < 3; ++b) { r.origin[b] = 0; r.size[b] = res; }
                    r.size[a]   = static_cast<uint32_t>(std::abs(shift));
                    r.origin[a] = shift > 0 ? res - static_cast<uint32_t>(shift) : 0u;
                    regions.push_back(std::move(r));
                }
            }
            for (auto& change : clip.changeRegions) regions.push_back(std::move(change));
        }
        clip.changeRegions.clear();
        clip.shift[0] = clip.shift[1] = clip.shift[2] = 0;
        clip.full = false;
        const uint32_t regionCount = static_cast<uint32_t>(regions.size());
        for (uint32_t region = 0; region < regionCount; ++region) {
            const auto& r = regions[region];
            constants.regionOrigin = r.origin[0] | (r.origin[1] << 9) | (r.origin[2] << 18);
            constants.regionSize   = r.size[0] | (r.size[1] << 9) | (r.size[2] << 18);
            const bool own = !r.batches.empty();           // a change region carries its own instances
            const uint32_t regionBatchCount = own ? static_cast<uint32_t>(r.batches.size()) : batchCount;
            const uint32_t regionMeshCount  = own ? r.meshCount : meshCount;
            for (uint32_t batch = 0; batch < regionBatchCount; ++batch) {
                const auto& allocation = own ? r.batches[batch] : batches[batch];
                constants.meshCount = std::min(kGdfMeshesPerBatch, regionMeshCount - batch * kGdfMeshesPerBatch);
                constants.accumulate = batch > 0;
                cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 13);
                cmdList->SetRootCBV(2, allocation.bufferHandle, allocation.offsetBytes,
                                   allocation.cpuPtr, std::max(1u, constants.meshCount) * sizeof(GdfMeshSdfGPU));
                cmdList->Dispatch((r.size[0] + 3) / 4, (r.size[1] + 3) / 4, (r.size[2] + 3) / 4);
                if (batch + 1 < regionBatchCount || region + 1 < regionCount) {
                    RHIBarrierDesc uavBarrier{};
                    uavBarrier.texture = clip.texture;
                    uavBarrier.stateBefore = RHIResourceState::UnorderedAccess;
                    uavBarrier.stateAfter = RHIResourceState::UnorderedAccess;
                    RHIBarrierDesc batchBarriers[2] = {uavBarrier, uavBarrier};
                    batchBarriers[1].texture = clip.rawTexture;
                    cmdList->ResourceBarrier(batchBarriers, 2);
                }
            }
        }

        RHIBarrierDesc toSrv{};
        toSrv.texture     = clip.texture;
        toSrv.stateBefore = RHIResourceState::UnorderedAccess;
        toSrv.stateAfter  = RHIResourceState::ShaderResource;
        RHIBarrierDesc finalBarriers[2] = {toSrv, toSrv};
        finalBarriers[1].texture = clip.rawTexture;
        cmdList->ResourceBarrier(finalBarriers, 2);

        // Diagnostic (followup-13): the field AFTER the recomposite, same command list, after the
        // barrier that makes the new values visible.
        // Must be the SAME clipmap the "before" snapshot came from. It read m_clipmaps[0]
        // unconditionally while the "before" side followed the selected cascade, so a comparison on
        // cascade 1 measured cascade 1 against cascade 0 — two volumes with different extents, which
        // is what produced H-53's "the field is not preserved" (withdrawn 2026-09-17).
        if (m_fieldComparePending && m_fieldAwaitSlab && &clip == &m_clipmaps[m_fieldCascade]) {
            // Diagnostic (block B.2, gates G1 and regional): the incremental result of this frame
            // (slabs after a recentre, regions after an instance change), then a whole-window
            // recompose of the SAME instances in the SAME command list; ServiceFieldCompare compares
            // the two texel by texel. Same frame, so no window or instance change can slip in.
            CopyRawFieldTo(cmdList, clip, m_fieldBefore);
            RHIBarrierDesc back[2] = {toSrv, toSrv};
            back[0].stateBefore = RHIResourceState::ShaderResource;
            back[0].stateAfter  = RHIResourceState::UnorderedAccess;
            back[1] = back[0];
            back[1].texture = clip.rawTexture;
            cmdList->ResourceBarrier(back, 2);
            constants.regionOrigin = 0;
            constants.regionSize   = res | (res << 9) | (res << 18);
            for (uint32_t batch = 0; batch < batchCount; ++batch) {
                const auto& allocation = batches[batch];
                constants.meshCount = std::min(kGdfMeshesPerBatch, meshCount - batch * kGdfMeshesPerBatch);
                constants.accumulate = batch > 0;
                cmdList->SetRootConstants(0, reinterpret_cast<const uint32_t*>(&constants), 13);
                cmdList->SetRootCBV(2, allocation.bufferHandle, allocation.offsetBytes,
                                   allocation.cpuPtr, std::max(1u, constants.meshCount) * sizeof(GdfMeshSdfGPU));
                cmdList->Dispatch((res + 3) / 4, (res + 3) / 4, (res + 3) / 4);
                if (batch + 1 < batchCount) {
                    RHIBarrierDesc uavBarrier{};
                    uavBarrier.texture = clip.texture;
                    uavBarrier.stateBefore = RHIResourceState::UnorderedAccess;
                    uavBarrier.stateAfter = RHIResourceState::UnorderedAccess;
                    RHIBarrierDesc batchBarriers[2] = {uavBarrier, uavBarrier};
                    batchBarriers[1].texture = clip.rawTexture;
                    cmdList->ResourceBarrier(batchBarriers, 2);
                }
            }
            cmdList->ResourceBarrier(finalBarriers, 2);
            CopyRawFieldTo(cmdList, clip, m_fieldAfter);
            m_fieldAwaitSlab    = false;
            m_fieldBeforeCopied = false;
            m_fieldCompareAge   = 0;
        }

        clip.dirty = false;
        clip.hasData = true;
    }
    if (m_meshDataHash != meshHash || m_meshDataCount != meshCount) { if (!regional) ++m_sceneRevision; }   // whole-window scene recomposes only: the consumers reset everything on a revision change
    m_meshDataHash = meshHash;
    m_meshDataCount = meshCount;
}

// ─────────────────────────────────────────────────────────────────────────────
// Diagnostic: the field before and after a recentre (followup-13)
// ─────────────────────────────────────────────────────────────────────────────

/// Copies every Z slice of the raw R16F volume into `dst` (slice z at z * rowPitch * res). The texture
/// is in ShaderResource state on entry and is returned to it.
void GlobalDistanceField::CopyRawFieldTo(IRHICommandList* cmdList, ClipmapData& clip, RHIBufferHandle dst) {
    const uint32_t res = m_fieldResolution;
    const uint64_t slicePitch = static_cast<uint64_t>(m_fieldRowPitch) * res;
    RHIBarrierDesc toCopy{};
    toCopy.texture     = clip.rawTexture;
    toCopy.stateBefore = RHIResourceState::ShaderResource;
    toCopy.stateAfter  = RHIResourceState::CopySrc;
    cmdList->ResourceBarrier(&toCopy, 1);
    for (uint32_t z = 0; z < res; ++z) {
        cmdList->CopyTextureToBuffer(clip.rawTexture, 0, dst, z * slicePitch, m_fieldRowPitch, z);
    }
    toCopy.stateBefore = RHIResourceState::CopySrc;
    toCopy.stateAfter  = RHIResourceState::ShaderResource;
    cmdList->ResourceBarrier(&toCopy, 1);
}

/// A few frames after the copies were recorded, maps both volumes and compares them at the same world
/// positions. The centre moved by a whole number of voxels, so old voxel (x, y, z) and new voxel
/// (x + sx, y + sy, z + sz) are the same point: world(i, c) = c + (2 (i + 0.5) / res - 1) extent, hence
/// i_new = i_old + (c_old - c_new) / voxel. Logs one line and releases the buffers.
void GlobalDistanceField::ServiceFieldCompare() {
    if (!m_fieldComparePending) return;
    if (++m_fieldCompareAge < 4) return;
    m_fieldComparePending = false;
    m_device->WaitIdle();
    const auto* before = static_cast<const uint8_t*>(m_device->MapBuffer(m_fieldBefore));
    const auto* after  = static_cast<const uint8_t*>(m_device->MapBuffer(m_fieldAfter));
    const uint32_t res = m_fieldResolution;
    const float voxel = 2.0f * m_fieldExtent / static_cast<float>(res);
    Vec3 delta;
    delta.x = m_fieldOldCenter.x - m_fieldNewCenter.x;
    delta.y = m_fieldOldCenter.y - m_fieldNewCenter.y;
    delta.z = m_fieldOldCenter.z - m_fieldNewCenter.z;
    const int32_t sx = static_cast<int32_t>(std::lround(delta.x / voxel));
    const int32_t sy = static_cast<int32_t>(std::lround(delta.y / voxel));
    const int32_t sz = static_cast<int32_t>(std::lround(delta.z / voxel));
    const float residual = std::max({std::abs(delta.x / voxel - sx), std::abs(delta.y / voxel - sy),
                                     std::abs(delta.z / voxel - sz)});
    if (!before || !after) {
        UZ_LOG(Renderer, Error, "GDF field compare: readback map failed");
    } else {
        const uint64_t slicePitch = static_cast<uint64_t>(m_fieldRowPitch) * res;
        uint64_t overlap = 0, identical = 0, overOnePercent = 0, nonFinite = 0;
        double sumAbs = 0.0, maxAbs = 0.0;
        // Same texel, no shift: "did the content stay where it was written?" Counted over the
        // whole volume, so it is not the same population as the overlap above and the two
        // fractions are not complementary.
        uint64_t texelTotal = 0, texelIdentical = 0;
        double texelSumAbs = 0.0, texelMaxAbs = 0.0;
        // A histogram of |delta| in units of 1e-3 voxel, capped, for a p99 without storing values.
        constexpr uint32_t kBins = 4096;
        std::vector<uint64_t> hist(kBins, 0);
        for (uint32_t z = 0; z < res; ++z) {
            for (uint32_t y = 0; y < res; ++y) {
                const uint8_t* rowB = before + z * slicePitch + static_cast<uint64_t>(y) * m_fieldRowPitch;
                const uint8_t* rowA = after  + z * slicePitch + static_cast<uint64_t>(y) * m_fieldRowPitch;
                for (uint32_t x = 0; x < res; ++x) {
                    uint16_t hb, ha;
                    std::memcpy(&hb, rowB + x * 2u, 2);
                    std::memcpy(&ha, rowA + x * 2u, 2);
                    const float vb = GdfHalfToFloat(hb), va = GdfHalfToFloat(ha);
                    if (!std::isfinite(vb) || !std::isfinite(va)) continue;
                    ++texelTotal;
                    if (hb == ha) ++texelIdentical;
                    const double td = std::abs(static_cast<double>(va) - vb);
                    texelSumAbs += td;
                    if (td > texelMaxAbs) texelMaxAbs = td;
                }
            }
        }
        // Toroidal storage (block B.2): both snapshots hold the NEW window (the slab recompose and
        // the full recompose of the same window), so every texel is compared with itself; the
        // "overlap" is the whole volume. The window test below is kept in the general form.
        const int64_t resI = res;
        int64_t k0Old[3], k0New[3];
        {
            const float newOrigin[3] = {m_fieldNewCenter.x - m_fieldExtent, m_fieldNewCenter.y - m_fieldExtent, m_fieldNewCenter.z - m_fieldExtent};
            for (uint32_t a = 0; a < 3; ++a) {
                k0New[a] = std::lround(newOrigin[a] / voxel);
                k0Old[a] = k0New[a];
            }
        }
        const auto inNewWindow = [&](uint32_t t, uint32_t axis) {
            const int64_t k = k0Old[axis] + ((((static_cast<int64_t>(t) - k0Old[axis]) % resI) + resI) % resI);
            return k >= k0New[axis] && k < k0New[axis] + resI;
        };
        for (uint32_t z = 0; z < res; ++z) {
            if (!inNewWindow(z, 2)) continue;
            for (uint32_t y = 0; y < res; ++y) {
                if (!inNewWindow(y, 1)) continue;
                const uint8_t* rowOld = before + z * slicePitch + static_cast<uint64_t>(y) * m_fieldRowPitch;
                const uint8_t* rowNew = after  + z * slicePitch + static_cast<uint64_t>(y) * m_fieldRowPitch;
                for (uint32_t x = 0; x < res; ++x) {
                    if (!inNewWindow(x, 0)) continue;
                    uint16_t ho, hn;
                    std::memcpy(&ho, rowOld + x * 2u, 2);
                    std::memcpy(&hn, rowNew + x * 2u, 2);
                    const float vo = GdfHalfToFloat(ho), vn = GdfHalfToFloat(hn);
                    if (!std::isfinite(vo) || !std::isfinite(vn)) { ++nonFinite; continue; }
                    ++overlap;
                    if (ho == hn) ++identical;
                    const double d = std::abs(static_cast<double>(vn) - vo);
                    sumAbs += d;
                    if (d > maxAbs) maxAbs = d;
                    if (d > 0.01 * voxel) ++overOnePercent;
                    const uint32_t bin = static_cast<uint32_t>(std::min<double>(kBins - 1, d / voxel * 1000.0));
                    ++hist[bin];
                }
            }
        }
        double p99 = 0.0;
        if (overlap > 0) {
            uint64_t acc = 0;
            const uint64_t target = static_cast<uint64_t>(0.99 * static_cast<double>(overlap));
            for (uint32_t b = 0; b < kBins; ++b) {
                acc += hist[b];
                if (acc >= target) { p99 = (b + 0.5) / 1000.0 * voxel; break; }
            }
        }
        UZ_LOG(Renderer, Info,
               "GDF field compare: clipmap={} oldCenter=({},{},{}) newCenter=({},{},{}) shiftVoxels=({},{},{}) "
               "shiftResidual={} voxelCm={} overlap={} identical={} nonFinite={} meanAbsCm={} p99AbsCm={} "
               "maxAbsCm={} overOnePercentVoxel={} sameTexelTotal={} sameTexelIdentical={} "
               "sameTexelMeanAbsCm={} sameTexelMaxAbsCm={}",
               m_fieldCascade, m_fieldOldCenter.x, m_fieldOldCenter.y, m_fieldOldCenter.z,
               m_fieldNewCenter.x, m_fieldNewCenter.y, m_fieldNewCenter.z, sx, sy, sz, residual, voxel,
               overlap, identical, nonFinite, overlap ? sumAbs / static_cast<double>(overlap) : 0.0, p99,
               maxAbs, overOnePercent, texelTotal, texelIdentical,
               texelTotal ? texelSumAbs / static_cast<double>(texelTotal) : 0.0, texelMaxAbs);
    }
    if (before) m_device->UnmapBuffer(m_fieldBefore);
    if (after)  m_device->UnmapBuffer(m_fieldAfter);
    m_device->DestroyBuffer(m_fieldBefore);
    m_device->DestroyBuffer(m_fieldAfter);
    m_fieldBefore = {};
    m_fieldAfter = {};
}

// ─────────────────────────────────────────────────────────────────────────────
// Accessors
// ─────────────────────────────────────────────────────────────────────────────

const GlobalDistanceField::ClipmapData& GlobalDistanceField::GetClipmap(uint32_t index) const {
    static const ClipmapData kInvalid{};
    if (index >= kNumClipmaps) return kInvalid;
    return m_clipmaps[index];
}

float GlobalDistanceField::GetVoxelSize(uint32_t cascadeIndex) const {
    if (!m_initialized || cascadeIndex >= kNumClipmaps) return 0.0f;
    const uint32_t resolution = m_device->GetTextureDesc(m_clipmaps[cascadeIndex].texture).width;
    return (m_clipmaps[cascadeIndex].extent * 2.0f) / static_cast<float>(resolution);
}

} // namespace uz
