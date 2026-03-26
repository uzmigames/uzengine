#include "gbuffer.h"

/// Deferred geometry buffer (GBuffer) resource lifecycle and move semantics.
///
/// GBuffer holds 6 render textures (albedo, normal, depth, velocity, position, emissive)
/// plus their SRV/UAV descriptor indices for deferred lighting pipeline access.

#include "rhi.h"
#include "log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Move semantics
// ─────────────────────────────────────────────────────────────────────────────

GBuffer::GBuffer(GBuffer&& other) noexcept
    : albedo(other.albedo)
    , normal(other.normal)
    , depth(other.depth)
    , velocity(other.velocity)
    , positionWS(other.positionWS)
    , emissive(other.emissive)
    , albedoSRV(other.albedoSRV)
    , normalSRV(other.normalSRV)
    , albedoUAV(other.albedoUAV)
    , normalUAV(other.normalUAV)
    , depthSRV(other.depthSRV)
    , velocitySRV(other.velocitySRV)
    , positionWSSRV(other.positionWSSRV)
    , emissiveSRV(other.emissiveSRV)
    , width(other.width)
    , height(other.height)
{
    other.albedo = RHITextureHandle::Invalid();
    other.normal = RHITextureHandle::Invalid();
    other.depth = RHITextureHandle::Invalid();
    other.velocity = RHITextureHandle::Invalid();
    other.positionWS = RHITextureHandle::Invalid();
    other.emissive = RHITextureHandle::Invalid();
    other.albedoSRV = UINT32_MAX;
    other.normalSRV = UINT32_MAX;
    other.albedoUAV = UINT32_MAX;
    other.normalUAV = UINT32_MAX;
    other.depthSRV = UINT32_MAX;
    other.velocitySRV = UINT32_MAX;
    other.positionWSSRV = UINT32_MAX;
    other.emissiveSRV = UINT32_MAX;
    other.width = 0;
    other.height = 0;
}

GBuffer& GBuffer::operator=(GBuffer&& other) noexcept {
    if (this != &other) {
        albedo = other.albedo;
        normal = other.normal;
        depth = other.depth;
        velocity = other.velocity;
        positionWS = other.positionWS;
        emissive = other.emissive;
        albedoSRV = other.albedoSRV;
        normalSRV = other.normalSRV;
        albedoUAV = other.albedoUAV;
        normalUAV = other.normalUAV;
        depthSRV = other.depthSRV;
        velocitySRV = other.velocitySRV;
        positionWSSRV = other.positionWSSRV;
        emissiveSRV = other.emissiveSRV;
        width = other.width;
        height = other.height;

        other.albedo = RHITextureHandle::Invalid();
        other.normal = RHITextureHandle::Invalid();
        other.depth = RHITextureHandle::Invalid();
        other.velocity = RHITextureHandle::Invalid();
        other.positionWS = RHITextureHandle::Invalid();
        other.emissive = RHITextureHandle::Invalid();
        other.albedoSRV = UINT32_MAX;
        other.normalSRV = UINT32_MAX;
        other.albedoUAV = UINT32_MAX;
        other.normalUAV = UINT32_MAX;
        other.depthSRV = UINT32_MAX;
        other.velocitySRV = UINT32_MAX;
        other.positionWSSRV = UINT32_MAX;
        other.emissiveSRV = UINT32_MAX;
        other.width = 0;
        other.height = 0;
    }
    return *this;
}

// ─────────────────────────────────────────────────────────────────────────────
// Lifecycle
// ─────────────────────────────────────────────────────────────────────────────

bool GBuffer::Create(IRHIDevice* device, uint32_t w, uint32_t h) {
    width = w;
    height = h;

    const RHITextureUsage colorUsage =
        RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource;

    // Albedo and Normal also need UnorderedAccess for the DBuffer apply compute pass.
    // @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall" — DBuffer decals
    const RHITextureUsage colorUsageUAV =
        RHITextureUsage::RenderTarget | RHITextureUsage::ShaderResource | RHITextureUsage::UnorderedAccess;

    const RHITextureUsage depthUsage =
        RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;

    // Albedo — RGBA8Unorm base color + opacity
    // Uses UAV for DBuffer decal apply compute pass (in-place modification).
    albedo = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA8Unorm,
        .usage = colorUsageUAV,
        .debugName = "GBuffer_Albedo",
    });

    // Normal — RGBA8Unorm octahedral normal (RG) + metallic (B) + roughness (A)
    // Uses UAV for DBuffer decal apply compute pass (in-place modification).
    normal = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA8Unorm,
        .usage = colorUsageUAV,
        .debugName = "GBuffer_Normal",
    });

    // Depth — D32Float hardware depth
    depth = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::D32Float,
        .usage = depthUsage,
        .debugName = "GBuffer_Depth",
    });

    // Velocity — RG16Float screen-space motion vectors (written by velocity pass)
    velocity = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RG16Float,
        .usage = colorUsage,
        .debugName = "GBuffer_Velocity",
    });

    // PositionWS — RGBA32Float world-space position (debug / reconstruction verification)
    positionWS = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA32Float,
        .usage = colorUsage,
        .debugName = "GBuffer_PositionWS",
    });

    // Emissive — RGBA16Float HDR emissive color (RGB, A unused)
    // In a forward renderer, emissive is added directly to scene color.
    // writes emissive during the base pass.
    // In our deferred pipeline, we store it in a separate GBuffer RT
    // and add it during the deferred lighting pass.
    emissive = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA16Float,
        .usage = colorUsage,
        .debugName = "GBuffer_Emissive",
    });

    if (!albedo.IsValid() || !normal.IsValid() || !depth.IsValid() || !velocity.IsValid() || !positionWS.IsValid() || !emissive.IsValid()) {
        UZ_LOG(Renderer, Error, "GBuffer creation failed: one or more textures could not be allocated ({}x{})", w, h);
        Destroy(device);
        return false;
    }

    // Retrieve bindless SRV indices for downstream passes
    albedoSRV     = device->GetTextureSRVIndex(albedo);
    normalSRV     = device->GetTextureSRVIndex(normal);
    depthSRV      = device->GetTextureSRVIndex(depth);
    velocitySRV   = device->GetTextureSRVIndex(velocity);
    positionWSSRV = device->GetTextureSRVIndex(positionWS);
    emissiveSRV   = device->GetTextureSRVIndex(emissive);

    // Retrieve bindless UAV indices for DBuffer apply compute pass.
    // @ref Valient 2014 "Deferred Rendering in Killzone: Shadow Fall"
    albedoUAV     = device->GetTextureUAVIndex(albedo);
    normalUAV     = device->GetTextureUAVIndex(normal);

    UZ_LOG(Renderer, Info, "GBuffer created: {}x{}", w, h);
    return true;
}

bool GBuffer::Resize(IRHIDevice* device, uint32_t newWidth, uint32_t newHeight) {
    Destroy(device);
    return Create(device, newWidth, newHeight);
}

void GBuffer::Destroy(IRHIDevice* device) {
    if (albedo.IsValid())      { device->DestroyTexture(albedo); }
    if (normal.IsValid())      { device->DestroyTexture(normal); }
    if (depth.IsValid())       { device->DestroyTexture(depth); }
    if (velocity.IsValid())    { device->DestroyTexture(velocity); }
    if (positionWS.IsValid())  { device->DestroyTexture(positionWS); }
    if (emissive.IsValid())    { device->DestroyTexture(emissive); }

    albedo     = RHITextureHandle::Invalid();
    normal     = RHITextureHandle::Invalid();
    depth      = RHITextureHandle::Invalid();
    velocity   = RHITextureHandle::Invalid();
    positionWS = RHITextureHandle::Invalid();
    emissive   = RHITextureHandle::Invalid();

    albedoSRV     = UINT32_MAX;
    normalSRV     = UINT32_MAX;
    albedoUAV     = UINT32_MAX;
    normalUAV     = UINT32_MAX;
    depthSRV      = UINT32_MAX;
    velocitySRV   = UINT32_MAX;
    positionWSSRV = UINT32_MAX;
    emissiveSRV   = UINT32_MAX;

    width = 0;
    height = 0;
}

bool GBuffer::IsValid() const {
    return albedo.IsValid() && normal.IsValid() && depth.IsValid() && velocity.IsValid() && positionWS.IsValid() && emissive.IsValid();
}

} // namespace uz
