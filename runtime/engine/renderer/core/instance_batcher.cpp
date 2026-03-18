#include "instance_batcher.h"

#include "log.h"
#include "profiler.h"

UZ_DECLARE_LOG_CATEGORY(Renderer);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

/// Returns true if two commands share the same mesh and material textures,
/// meaning they can be merged into a single instanced batch.
static bool IsSameBatch(const DrawCommand& a, const DrawCommand& b) noexcept
{
    return a.meshId           == b.meshId
        && a.albedoSrvIndex   == b.albedoSrvIndex
        && a.normalSrvIndex   == b.normalSrvIndex
        && a.roughnessSrvIndex == b.roughnessSrvIndex
        && a.metallicSrvIndex == b.metallicSrvIndex
        && a.aoSrvIndex       == b.aoSrvIndex
        && a.emissiveSrvIndex == b.emissiveSrvIndex;
}

/// Fills an InstanceData struct from a DrawCommand.
static InstanceData MakeInstanceData(const DrawCommand& cmd) noexcept
{
    InstanceData inst;
    inst.world        = cmd.worldMatrix;
    inst.normalMatrix = cmd.normalMatrix;
    return inst;
}

/// Creates an InstanceBatch from the first command at a given offset.
static InstanceBatch MakeBatch(const DrawCommand& cmd, uint32_t firstInstance) noexcept
{
    InstanceBatch batch;
    batch.meshId             = cmd.meshId;
    batch.firstInstance      = firstInstance;
    batch.instanceCount      = 1;
    batch.albedoSrvIndex     = cmd.albedoSrvIndex;
    batch.normalSrvIndex     = cmd.normalSrvIndex;
    batch.roughnessSrvIndex  = cmd.roughnessSrvIndex;
    batch.metallicSrvIndex   = cmd.metallicSrvIndex;
    batch.aoSrvIndex         = cmd.aoSrvIndex;
    batch.emissiveSrvIndex   = cmd.emissiveSrvIndex;
    batch.emissiveColorR     = cmd.emissiveColorR;
    batch.emissiveColorG     = cmd.emissiveColorG;
    batch.emissiveColorB     = cmd.emissiveColorB;
    batch.emissiveIntensity  = cmd.emissiveIntensity;
    batch.albedoTexture      = cmd.albedoTexture;
    batch.normalTexture      = cmd.normalTexture;
    batch.roughnessTexture   = cmd.roughnessTexture;
    batch.metallicTexture    = cmd.metallicTexture;
    batch.aoTexture          = cmd.aoTexture;
    batch.emissiveTexture    = cmd.emissiveTexture;
    return batch;
}

// ─────────────────────────────────────────────────────────────────────────────
// BuildInstanceBatches
// ─────────────────────────────────────────────────────────────────────────────

void BuildInstanceBatches(const DrawList& drawList,
                          std::vector<InstanceBatch>& outBatches,
                          std::vector<InstanceData>& outInstances)
{
    UZ_PROFILE_SCOPE("BuildInstanceBatches");

    outBatches.clear();
    outInstances.clear();

    const auto& commands = drawList.GetCommands();
    if (commands.empty()) {
        return;
    }

    const auto commandCount = static_cast<uint32_t>(commands.size());

    // Clamp to the per-frame instance limit.
    const uint32_t maxCount = (commandCount > kMaxInstances) ? kMaxInstances : commandCount;
    if (commandCount > kMaxInstances) {
        UZ_LOG(Renderer, Warning,
               "BuildInstanceBatches: {} commands exceed kMaxInstances ({}), dropping {}",
               commandCount, kMaxInstances, commandCount - kMaxInstances);
    }

    // Reserve storage to avoid per-element reallocation.
    outInstances.reserve(maxCount);
    outBatches.reserve(maxCount / 4); // Conservative estimate — many commands merge.

    // Seed the first batch from the first command.
    outInstances.push_back(MakeInstanceData(commands[0]));
    outBatches.push_back(MakeBatch(commands[0], 0));

    for (uint32_t i = 1; i < maxCount; ++i) {
        const auto& cmd = commands[i];

        outInstances.push_back(MakeInstanceData(cmd));

        if (IsSameBatch(cmd, commands[i - 1])) {
            // Extend the current batch.
            outBatches.back().instanceCount++;
        } else {
            // Start a new batch at the current instance offset.
            outBatches.push_back(MakeBatch(cmd, i));
        }
    }
}

} // namespace uz
