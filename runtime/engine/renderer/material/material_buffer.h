/// UzEngine Material Buffer
///
/// Manages a GPU StructuredBuffer of PBRMaterial elements. Materials are
/// referenced by index from `PerObjectCB.materialIndex`. The buffer is
/// uploaded to an upload-heap resource and accessed via bindless SRV index
/// (SM6.6) or descriptor table (SM5).
///
/// # GPU Access
/// ```hlsl
/// // SM6.6 bindless:
/// StructuredBuffer<PBRMaterial> materials = ResourceDescriptorHeap[materialBufferSRV];
/// PBRMaterial mat = materials[perObject.materialIndex];
///
/// // SM5 descriptor table:
/// StructuredBuffer<PBRMaterial> materials : register(t0);
/// ```
///
/// # Lifetime
/// Call `Init()` after device creation, `Shutdown()` before device destruction.
/// Call `Upload()` once per frame before issuing draw calls that read materials.
///
/// # Thread Safety
/// NOT thread-safe. Use from the render thread only.

#pragma once

#include "material.h"
#include "rhi_types.h"

#include <cstdint>
#include <vector>

namespace uz {

class IRHIDevice;

/// GPU-backed array of PBR materials with CPU-side dirty tracking.
///
/// Maintains a CPU mirror of the material array and uploads to an upload-heap
/// StructuredBuffer only when materials have been added or modified. Index 0
/// is reserved as the default material (white, non-metallic, roughness 0.5).
///
/// # Capacity
/// Fixed at `Init()` time. Cannot grow. `AddMaterial()` returns `UINT32_MAX`
/// when the buffer is full.
class MaterialBuffer {
public:
    MaterialBuffer() = default;
    ~MaterialBuffer();

    // Non-copyable — GPU resource ownership.
    MaterialBuffer(const MaterialBuffer&) = delete;
    MaterialBuffer& operator=(const MaterialBuffer&) = delete;

    /// Move constructor. Transfers GPU resource ownership from `other`.
    /// After the move, `other` is in a default-constructed (empty) state.
    MaterialBuffer(MaterialBuffer&& other) noexcept;

    /// Move assignment. Releases current resources, then transfers from `other`.
    MaterialBuffer& operator=(MaterialBuffer&& other) noexcept;

    /// Initializes the material buffer with a fixed capacity.
    ///
    /// Creates an upload-heap StructuredBuffer with `stride = sizeof(PBRMaterial)`.
    /// Reserves index 0 as the default material (PBRMaterial default constructor
    /// values: white baseColor, metallic 0, roughness 0.5, ao 1, no emissive).
    ///
    /// # Arguments
    /// * `device` — RHI device for buffer creation. Must outlive this buffer.
    /// * `maxMaterials` — Maximum number of materials (including the default at index 0).
    ///
    /// # Returns
    /// `true` if the GPU buffer was created and mapped successfully.
    /// `false` on invalid arguments or GPU allocation failure.
    bool Init(IRHIDevice* device, uint32_t maxMaterials = 256);

    /// Releases the GPU buffer and resets all state.
    ///
    /// Safe to call multiple times or on an uninitialized buffer.
    void Shutdown();

    /// Adds a material and returns its index in the StructuredBuffer.
    ///
    /// The material is stored in the CPU mirror and marked dirty for upload.
    /// The returned index can be stored in `PerObjectCB.materialIndex`.
    ///
    /// # Returns
    /// The material index on success, or `UINT32_MAX` if the buffer is at capacity.
    [[nodiscard]] uint32_t AddMaterial(const PBRMaterial& material);

    /// Updates an existing material at the given index.
    ///
    /// # Arguments
    /// * `index` — Must be in range `[0, Count())`. Out-of-range indices are logged and ignored.
    /// * `material` — New material data to write.
    void UpdateMaterial(uint32_t index, const PBRMaterial& material);

    /// Returns the CPU-side copy of the material at the given index.
    ///
    /// # Arguments
    /// * `index` — Must be in range `[0, Count())`.
    ///
    /// # Returns
    /// Reference to the material. Undefined behavior if `index >= Count()`.
    [[nodiscard]] const PBRMaterial& GetMaterial(uint32_t index) const;

    /// Uploads all dirty materials to the GPU buffer.
    ///
    /// Maps the upload-heap buffer, copies the entire CPU mirror, and unmaps.
    /// No-op if no materials have been added or modified since the last upload.
    /// Call once per frame before rendering.
    void Upload();

    /// Returns the SRV descriptor index for SM6.6 bindless access.
    ///
    /// In shaders:
    /// `StructuredBuffer<PBRMaterial> mats = ResourceDescriptorHeap[GetSRVIndex()];`
    ///
    /// # Returns
    /// The SRV index in the global CBV_SRV_UAV heap, or `UINT32_MAX` if the
    /// buffer was not created with StructuredBuffer usage.
    [[nodiscard]] uint32_t GetSRVIndex() const { return m_srvIndex; }

    /// Returns the GPU buffer handle for SM5 descriptor table binding.
    [[nodiscard]] RHIBufferHandle GetBufferHandle() const { return m_buffer; }

    /// Returns the number of materials currently stored (including default at index 0).
    [[nodiscard]] uint32_t Count() const { return m_count; }

    /// Returns the maximum capacity set at `Init()` time.
    [[nodiscard]] uint32_t Capacity() const { return m_capacity; }

private:
    IRHIDevice*              m_device   = nullptr;
    RHIBufferHandle          m_buffer;
    uint8_t*                 m_mappedPtr = nullptr; ///< Persistently mapped upload-heap pointer.
    uint32_t                 m_srvIndex = UINT32_MAX;
    uint32_t                 m_capacity = 0;
    uint32_t                 m_count    = 0;
    std::vector<PBRMaterial> m_materials;            ///< CPU-side mirror of the GPU buffer.
    bool                     m_dirty    = false;
};

} // namespace uz
