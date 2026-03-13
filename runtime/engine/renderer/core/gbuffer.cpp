#include "gbuffer.h"

#include "rhi.h"
#include "log.h"

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Move semantics
// ─────────────────────────────────────────────────────────────────────────────

GBuffer::GBuffer(GBuffer&& other) noexcept
    : albedo(other.albedo)
    , normal(other.normal)
    , metalRough(other.metalRough)
    , depth(other.depth)
    , albedoSRV(other.albedoSRV)
    , normalSRV(other.normalSRV)
    , metalRoughSRV(other.metalRoughSRV)
    , depthSRV(other.depthSRV)
    , width(other.width)
    , height(other.height)
{
    other.albedo = RHITextureHandle::Invalid();
    other.normal = RHITextureHandle::Invalid();
    other.metalRough = RHITextureHandle::Invalid();
    other.depth = RHITextureHandle::Invalid();
    other.albedoSRV = UINT32_MAX;
    other.normalSRV = UINT32_MAX;
    other.metalRoughSRV = UINT32_MAX;
    other.depthSRV = UINT32_MAX;
    other.width = 0;
    other.height = 0;
}

GBuffer& GBuffer::operator=(GBuffer&& other) noexcept {
    if (this != &other) {
        albedo = other.albedo;
        normal = other.normal;
        metalRough = other.metalRough;
        depth = other.depth;
        albedoSRV = other.albedoSRV;
        normalSRV = other.normalSRV;
        metalRoughSRV = other.metalRoughSRV;
        depthSRV = other.depthSRV;
        width = other.width;
        height = other.height;

        other.albedo = RHITextureHandle::Invalid();
        other.normal = RHITextureHandle::Invalid();
        other.metalRough = RHITextureHandle::Invalid();
        other.depth = RHITextureHandle::Invalid();
        other.albedoSRV = UINT32_MAX;
        other.normalSRV = UINT32_MAX;
        other.metalRoughSRV = UINT32_MAX;
        other.depthSRV = UINT32_MAX;
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

    const RHITextureUsage depthUsage =
        RHITextureUsage::DepthStencil | RHITextureUsage::ShaderResource;

    // Albedo — RGBA8Unorm base color + opacity
    albedo = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA8Unorm,
        .usage = colorUsage,
        .debugName = "GBuffer_Albedo",
    });

    // Normal — RGBA16Float world-space normals
    normal = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA16Float,
        .usage = colorUsage,
        .debugName = "GBuffer_Normal",
    });

    // Metallic-Roughness — RGBA8Unorm (R=metallic, G=roughness, B=AO)
    metalRough = device->CreateTexture(RHITextureDesc{
        .width = w,
        .height = h,
        .depthOrArraySize = 1,
        .mipLevels = 1,
        .format = RHIFormat::RGBA8Unorm,
        .usage = colorUsage,
        .debugName = "GBuffer_MetalRough",
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

    if (!albedo.IsValid() || !normal.IsValid() || !metalRough.IsValid() || !depth.IsValid()) {
        UZ_LOG(Renderer, Error, "GBuffer creation failed: one or more textures could not be allocated ({}x{})", w, h);
        Destroy(device);
        return false;
    }

    // Retrieve bindless SRV indices for the lighting pass
    albedoSRV = device->GetTextureSRVIndex(albedo);
    normalSRV = device->GetTextureSRVIndex(normal);
    metalRoughSRV = device->GetTextureSRVIndex(metalRough);
    depthSRV = device->GetTextureSRVIndex(depth);

    UZ_LOG(Renderer, Info, "GBuffer created: {}x{}", w, h);
    return true;
}

bool GBuffer::Resize(IRHIDevice* device, uint32_t newWidth, uint32_t newHeight) {
    Destroy(device);
    return Create(device, newWidth, newHeight);
}

void GBuffer::Destroy(IRHIDevice* device) {
    if (albedo.IsValid())     { device->DestroyTexture(albedo); }
    if (normal.IsValid())     { device->DestroyTexture(normal); }
    if (metalRough.IsValid()) { device->DestroyTexture(metalRough); }
    if (depth.IsValid())      { device->DestroyTexture(depth); }

    albedo = RHITextureHandle::Invalid();
    normal = RHITextureHandle::Invalid();
    metalRough = RHITextureHandle::Invalid();
    depth = RHITextureHandle::Invalid();

    albedoSRV = UINT32_MAX;
    normalSRV = UINT32_MAX;
    metalRoughSRV = UINT32_MAX;
    depthSRV = UINT32_MAX;

    width = 0;
    height = 0;
}

bool GBuffer::IsValid() const {
    return albedo.IsValid() && normal.IsValid() && metalRough.IsValid() && depth.IsValid();
}

} // namespace uz
