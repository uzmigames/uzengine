/// UzEngine Camera Manager — Implementation.

#include "camera_manager.h"
#include "log.h"
#include "engine_assert.h"

UZ_DEFINE_LOG_CATEGORY(Camera, Info);

namespace uz {

CameraManager::CameraManager() {
    (void)CreateCamera("Main");
    m_activeId = 0;
}

CameraId CameraManager::CreateCamera(const std::string& name) {
    CameraEntry entry;
    entry.id   = m_nextId++;
    entry.name = name;
    m_cameras.push_back(std::move(entry));

    UZ_LOG(Camera, Info, "CameraManager: created camera '{}' (id={})", name, entry.id);
    return m_cameras.back().id;
}

bool CameraManager::DestroyCamera(CameraId id) {
    if (id == m_activeId) {
        UZ_LOG(Camera, Warning, "CameraManager: cannot destroy active camera (id={})", id);
        return false;
    }

    int idx = FindIndex(id);
    if (idx < 0) {
        UZ_LOG(Camera, Warning, "CameraManager: camera id={} not found", id);
        return false;
    }

    UZ_LOG(Camera, Info, "CameraManager: destroyed camera '{}' (id={})",
           m_cameras[idx].name, id);
    m_cameras.erase(m_cameras.begin() + idx);
    return true;
}

bool CameraManager::SetActiveCamera(CameraId id) {
    int idx = FindIndex(id);
    if (idx < 0) {
        UZ_LOG(Camera, Warning, "CameraManager: cannot set active — id={} not found", id);
        return false;
    }

    CameraId prev = m_activeId;
    m_activeId = id;

    if (prev != id) {
        UZ_LOG(Camera, Info, "CameraManager: switched active camera '{}' → '{}' (id={} → {})",
               GetCameraName(prev), m_cameras[idx].name, prev, id);
    }
    return true;
}

Camera& CameraManager::GetActiveCamera() {
    int idx = FindIndex(m_activeId);
    UZ_CHECK(idx >= 0);
    return m_cameras[idx].camera;
}

const Camera& CameraManager::GetActiveCamera() const {
    int idx = FindIndex(m_activeId);
    UZ_CHECK(idx >= 0);
    return m_cameras[idx].camera;
}

Camera* CameraManager::GetCamera(CameraId id) {
    int idx = FindIndex(id);
    return (idx >= 0) ? &m_cameras[idx].camera : nullptr;
}

const Camera* CameraManager::GetCamera(CameraId id) const {
    int idx = FindIndex(id);
    return (idx >= 0) ? &m_cameras[idx].camera : nullptr;
}

const std::string& CameraManager::GetCameraName(CameraId id) const {
    int idx = FindIndex(id);
    if (idx >= 0) return m_cameras[idx].name;
    static const std::string kEmpty;
    return kEmpty;
}

bool CameraManager::PushCamera(CameraId id) {
    int idx = FindIndex(id);
    if (idx < 0) {
        UZ_LOG(Camera, Warning, "CameraManager: PushCamera — id={} not found", id);
        return false;
    }

    m_stack.push_back(m_activeId);
    m_activeId = id;

    UZ_LOG(Camera, Info, "CameraManager: pushed camera '{}' (stack depth={})",
           m_cameras[idx].name, m_stack.size());
    return true;
}

bool CameraManager::PopCamera() {
    if (m_stack.empty()) {
        UZ_LOG(Camera, Warning, "CameraManager: PopCamera — stack is empty");
        return false;
    }

    CameraId prev = m_activeId;
    m_activeId = m_stack.back();
    m_stack.pop_back();

    UZ_LOG(Camera, Info, "CameraManager: popped camera '{}' → '{}' (stack depth={})",
           GetCameraName(prev), GetCameraName(m_activeId), m_stack.size());
    return true;
}

int CameraManager::FindIndex(CameraId id) const {
    for (int i = 0; i < static_cast<int>(m_cameras.size()); ++i) {
        if (m_cameras[i].id == id) return i;
    }
    return -1;
}

} // namespace uz
