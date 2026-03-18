/// UzEngine Shader Parameter Binder — Implementation.
///
/// Automates constant buffer upload and resource binding from metadata-annotated
/// shader parameter structs.

#include "shader_parameter_binder.h"

#include "engine/rhi/rhi.h"
#include "engine_assert.h"
#include "log.h"
#include "profiler.h"

#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// BindCB — allocate + copy scalar data + bind root CBV
// ─────────────────────────────────────────────────────────────────────────────

CBAllocation ShaderParameterBinder::BindCB(
    IRHICommandList* cmd,
    const void* structData,
    const ShaderParameterMetadata& meta,
    CBRingAllocator& cbRing,
    uint32_t cbRootSlot)
{
    UZ_PROFILE_SCOPE("ShaderParameterBinder::BindCB");

    UZ_CHECK(cmd != nullptr);
    UZ_CHECK(structData != nullptr);
    UZ_CHECK(meta.cbSize > 0);
    UZ_CHECK(meta.cbSize % 256 == 0);

    // Suballocate from the ring buffer (256-byte aligned)
    CBAllocation alloc = cbRing.Allocate(meta.cbSize);
    if (alloc.cpuPtr == nullptr) {
        UZ_LOG(Renderer, Error, "ShaderParameterBinder: CB ring allocator full "
               "(requested {} bytes)", meta.cbSize);
        return alloc;
    }

    // Copy only the scalar portion — contiguous from byte 0 to cbSize.
    // Resource handles live after cbSize and are NOT copied to the GPU buffer.
    std::memcpy(alloc.cpuPtr, structData, meta.cbSize);

    // Bind the constant buffer at the designated root parameter slot
    cmd->SetRootCBV(cbRootSlot, alloc.bufferHandle, alloc.offsetBytes,
                    alloc.cpuPtr, alloc.sizeBytes);

    return alloc;
}

// ─────────────────────────────────────────────────────────────────────────────
// BindResources — bind textures, samplers, and UAVs from metadata
// ─────────────────────────────────────────────────────────────────────────────

void ShaderParameterBinder::BindResources(
    IRHICommandList* cmd,
    const void* structData,
    const ShaderParameterMetadata& meta,
    bool bindless)
{
    UZ_PROFILE_SCOPE("ShaderParameterBinder::BindResources");

    UZ_CHECK(cmd != nullptr);
    UZ_CHECK(structData != nullptr);

    const auto* bytes = static_cast<const uint8_t*>(structData);

    for (uint32_t i = 0; i < meta.memberCount; ++i) {
        const ShaderParamMember& m = meta.members[i];

        switch (m.type) {
        case ShaderParamType::Texture2D:
        case ShaderParamType::TextureCube: {
            if (bindless) {
                // SM6.6 bindless: shaders fetch via ResourceDescriptorHeap[index].
                // The SRV index is typically stored as a scalar field in the CB
                // (e.g., irradianceSrvIndex) and read by the shader from the CB.
                // No explicit texture binding needed.
                break;
            }

            // SM5: bind the texture at its designated register slot (tN).
            RHITextureHandle handle;
            std::memcpy(&handle, bytes + m.offset, sizeof(RHITextureHandle));

            if (handle.IsValid()) {
                cmd->SetFragmentTexture(m.resourceSlot, handle);
            }
            break;
        }

        case ShaderParamType::Sampler: {
            // Bind sampler at its designated register slot (sN).
            // Most passes use static samplers in the root signature, but explicit
            // binding serves as a safety net for passes with custom samplers.
            RHISamplerHandle handle;
            std::memcpy(&handle, bytes + m.offset, sizeof(RHISamplerHandle));

            if (handle.IsValid()) {
                cmd->SetFragmentSampler(m.resourceSlot, handle);
            }
            break;
        }

        case ShaderParamType::UAV: {
            // UAV binding is handled separately by compute dispatch setup.
            // The binder records UAVs but does not bind them automatically
            // because UAV binding requires the compute root signature path
            // (descriptor tables or root UAV), which varies per backend.
            break;
        }

        case ShaderParamType::Scalar:
            // Scalars are already copied to CB in BindCB(). Nothing to do.
            break;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Bind — convenience wrapper for BindCB + BindResources
// ─────────────────────────────────────────────────────────────────────────────

CBAllocation ShaderParameterBinder::Bind(
    IRHICommandList* cmd,
    const void* structData,
    const ShaderParameterMetadata& meta,
    CBRingAllocator& cbRing,
    uint32_t cbRootSlot,
    bool bindless)
{
    CBAllocation alloc = BindCB(cmd, structData, meta, cbRing, cbRootSlot);
    if (alloc.cpuPtr == nullptr) {
        return alloc;
    }

    BindResources(cmd, structData, meta, bindless);
    return alloc;
}

} // namespace uz
