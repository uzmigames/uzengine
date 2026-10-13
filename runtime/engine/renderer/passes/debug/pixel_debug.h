/// UzEngine PixelDebug — GPU printf system for shader diagnostics.
///
/// @ref NVIDIA Falcor PixelDebug — InterlockedAdd + structured buffer readback.
///
/// Manages GPU buffers for shader debug output. Shaders write debug records
/// via pixel_debug.hlsli macros. This class creates the buffers, binds them,
/// reads back results, and formats them as JSON.
///
/// # Lifecycle
/// 1. Init() — create GPU buffers (counter + records + readback staging)
/// 2. BeginFrame() — clear counter buffer to 0
/// 3. Bind() — set UAV descriptors before the draw call
/// 4. EndFrame() — copy records to readback buffer, signal fence
/// 5. ReadResults() — map readback (after fence wait), decode records
/// 6. ToJSON() — format results as JSON string
///
/// # Thread Safety
/// NOT thread-safe. All calls must happen on the render thread.

#pragma once

#include "rhi_types.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

class IRHIDevice;
class IRHICommandList;
namespace rg { class RgStateRegistry; }

/// A single debug record read back from the GPU.
struct PixelDebugRecord {
    uint32_t messageId;  ///< User-defined message ID (MSB=1 means uint value).
    float    floatValue; ///< Float interpretation of the value.
    uint32_t uintValue;  ///< Uint interpretation of the value.
    bool     isUint;     ///< True if the value was logged as uint.
    uint32_t pixelX;     ///< Pixel X coordinate.
    uint32_t pixelY;     ///< Pixel Y coordinate.
};

/// Configuration for the PixelDebug system.
struct PixelDebugConfig {
    uint32_t pixelX = 0;          ///< Debug pixel X coordinate.
    uint32_t pixelY = 0;          ///< Debug pixel Y coordinate.
    uint32_t maxRecords = 256;    ///< Max records per frame.
    bool     enabled = false;     ///< True if pixel debug is active.
};

/// GPU printf system — manages debug buffers and readback.
class PixelDebugSystem {
public:
    PixelDebugSystem() = default;
    ~PixelDebugSystem() = default;

    PixelDebugSystem(const PixelDebugSystem&) = delete;
    PixelDebugSystem& operator=(const PixelDebugSystem&) = delete;

    void SetStateRegistry(rg::RgStateRegistry* states) { m_states = states; }

    /// Creates GPU buffers (counter + records + readback + upload).
    /// Call once after device creation.
    bool Init(IRHIDevice* device, const PixelDebugConfig& config);

    /// Clears the counter buffer to 0 at the start of each frame.
    void BeginFrame(IRHICommandList* cmdList);

    /// Returns true if the system is initialized and enabled.
    [[nodiscard]] bool IsEnabled() const noexcept { return m_config.enabled && m_initialized; }

    /// Returns the counter buffer handle (for UAV binding).
    [[nodiscard]] RHIBufferHandle GetCounterBuffer() const noexcept { return m_counterBuffer; }

    /// Returns the records buffer handle (for UAV binding).
    [[nodiscard]] RHIBufferHandle GetRecordsBuffer() const noexcept { return m_recordsBuffer; }

    /// Returns the descriptor heap index for the counter buffer UAV.
    /// Used by the lighting pass to push bindless UAV indices as root constants.
    [[nodiscard]] uint32_t GetCounterUavIndex() const noexcept { return m_counterUavIndex; }

    /// Returns the descriptor heap index for the records buffer UAV.
    /// Used by the lighting pass to push bindless UAV indices as root constants.
    [[nodiscard]] uint32_t GetRecordsUavIndex() const noexcept { return m_recordsUavIndex; }

    /// Returns the config for uploading to PixelDebugCB.
    [[nodiscard]] const PixelDebugConfig& GetConfig() const noexcept { return m_config; }

    /// Copies records to readback buffer after rendering.
    void EndFrame(IRHICommandList* cmdList);

    /// Reads back results from the staging buffer.
    /// Must be called after GPU idle (fence wait).
    [[nodiscard]] std::vector<PixelDebugRecord> ReadResults(IRHIDevice* device);

    /// Formats results as JSON string.
    [[nodiscard]] static std::string ToJSON(const std::vector<PixelDebugRecord>& records,
                                             uint32_t pixelX, uint32_t pixelY,
                                             uint32_t frameNumber);

    /// Releases all GPU resources.
    void Shutdown(IRHIDevice* device);

private:
    rg::RgStateRegistry* m_states = nullptr;
    PixelDebugConfig m_config;
    bool m_initialized = false;

    RHIBufferHandle m_counterBuffer;    ///< 4 bytes, UAV + upload-clearable.
    RHIBufferHandle m_recordsBuffer;    ///< maxRecords * 16 bytes, UAV.
    RHIBufferHandle m_readbackBuffer;   ///< Same size as records, readback heap.
    RHIBufferHandle m_uploadBuffer;     ///< 4 bytes, upload heap (for clearing counter).
    uint32_t m_counterUavIndex = 0;     ///< Descriptor heap index for counter UAV (cached from Init).
    uint32_t m_recordsUavIndex = 0;     ///< Descriptor heap index for records UAV (cached from Init).
};

} // namespace uz
