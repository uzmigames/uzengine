/// UzEngine Camera Manager — Multiple camera support with push/pop stack.
///
/// @ref Falcor Scene::getCameras() — runtime camera switching pattern
///
/// Owns a vector of named cameras. One camera is active at any time;
/// `PerFrameCB` is always populated from the active camera.
///
/// # Push/Pop Stack
/// Temporary camera overrides (editor free-cam, cinematic cut) use
/// `PushCamera`/`PopCamera`. The stack preserves the previous active
/// camera so it can be restored instantly.
///
/// # Default Behavior
/// Created with one default camera at index 0 ("Main"). Existing
/// single-camera code is unchanged — `GetActiveCamera()` returns camera 0.

#pragma once

#include "camera.h"

#include <cstdint>
#include <string>
#include <vector>

namespace uz {

/// Stable camera identifier. Valid for the lifetime of the CameraManager.
using CameraId = uint32_t;

/// Invalid camera ID sentinel.
static constexpr CameraId kInvalidCameraId = UINT32_MAX;

/// Manages multiple named cameras with an active camera index and push/pop stack.
class CameraManager {
public:
    CameraManager();
    ~CameraManager() = default;

    CameraManager(const CameraManager&) = delete;
    CameraManager& operator=(const CameraManager&) = delete;

    /// Creates a new camera with the given name. Returns its ID.
    [[nodiscard]] CameraId CreateCamera(const std::string& name);

    /// Destroys a camera by ID. Cannot destroy the active camera.
    /// Returns true on success, false if ID is invalid or active.
    bool DestroyCamera(CameraId id);

    /// Sets the active camera by ID. Returns false if ID is invalid.
    bool SetActiveCamera(CameraId id);

    /// Returns the active camera (mutable).
    [[nodiscard]] Camera& GetActiveCamera();

    /// Returns the active camera (const).
    [[nodiscard]] const Camera& GetActiveCamera() const;

    /// Returns the active camera's ID.
    [[nodiscard]] CameraId GetActiveCameraId() const { return m_activeId; }

    /// Returns a camera by ID, or nullptr if not found.
    [[nodiscard]] Camera* GetCamera(CameraId id);
    [[nodiscard]] const Camera* GetCamera(CameraId id) const;

    /// Returns the name of a camera by ID. Empty string if not found.
    [[nodiscard]] const std::string& GetCameraName(CameraId id) const;

    /// Returns the total number of cameras.
    [[nodiscard]] uint32_t GetCameraCount() const { return static_cast<uint32_t>(m_cameras.size()); }

    /// Pushes the current active camera onto the stack and switches to `id`.
    /// Used for temporary overrides (editor free-cam, cinematic).
    bool PushCamera(CameraId id);

    /// Pops the stack and restores the previous active camera.
    /// Returns false if the stack is empty.
    bool PopCamera();

    /// Returns the stack depth (0 = no pushes).
    [[nodiscard]] uint32_t GetStackDepth() const { return static_cast<uint32_t>(m_stack.size()); }

private:
    struct CameraEntry {
        Camera      camera;
        std::string name;
        CameraId    id = kInvalidCameraId;
    };

    std::vector<CameraEntry>  m_cameras;
    std::vector<CameraId>     m_stack;    ///< Push/pop stack of previous active IDs.
    CameraId                  m_activeId = 0;
    CameraId                  m_nextId   = 0;

    /// Finds the index into m_cameras for a given ID. Returns -1 if not found.
    [[nodiscard]] int FindIndex(CameraId id) const;
};

} // namespace uz
