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

// ─────────────────────────────────────────────────────────────────────────────
// Orthographic Preset Views
// ─────────────────────────────────────────────────────────────────────────────

void CameraManager::SetOrthographicView(OrthoView view, float orthoSize, float aspectRatio) {
    if (view == OrthoView::None) {
        RestorePerspectiveView();
        return;
    }

    Camera& cam = GetActiveCamera();

    // Save perspective state once — do NOT overwrite if already in ortho mode.
    if (m_activeOrthoView == OrthoView::None) {
        m_savedPerspective.position    = cam.GetPosition();
        m_savedPerspective.orientation = cam.GetOrientation();
        m_savedPerspective.fovY        = cam.GetFovY();
        m_savedPerspective.aspectRatio = cam.GetAspectRatio();
        m_savedPerspective.nearPlane   = cam.GetNearPlane();
        m_savedPerspective.farPlane    = cam.GetFarPlane();
        m_savedPerspective.valid       = true;
    }
    m_activeOrthoView = view;

    // ── Camera distance and frustum dimensions ──────────────────────────────
    // Place the camera far enough that the near/far planes bracket the scene.
    // 100000 cm = 1 km: appropriate for typical UzEngine scenes (unit = 1 cm).
    constexpr float kViewDist  = 100000.f; // cm
    constexpr float kOrthoNear = 1.f;
    constexpr float kOrthoFar  = kViewDist * 2.f;

    const float orthoW = orthoSize * aspectRatio;
    const float orthoH = orthoSize;

    // ── Position and up vector per view ────────────────────────────────────
    // Coordinate system: X=forward, Y=right, Z=up (left-handed).
    //
    // Top/Bottom place the camera along ±Z so the up hint must be along X
    // (perpendicular to the forward direction; using Vec3::Up()=Z would be
    // collinear with the forward vector and degenerate the LookAt matrix).
    //
    // All other views keep the camera in the XY plane so Vec3::Up()=Z is safe.
    Vec3 pos;
    Vec3 up;
    switch (view) {
        case OrthoView::Top:    pos = {0.f, 0.f,  kViewDist}; up = Vec3::Forward(); break;
        case OrthoView::Bottom: pos = {0.f, 0.f, -kViewDist}; up = Vec3::Forward(); break;
        case OrthoView::Front:  pos = {-kViewDist, 0.f, 0.f}; up = Vec3::Up();      break;
        case OrthoView::Back:   pos = { kViewDist, 0.f, 0.f}; up = Vec3::Up();      break;
        case OrthoView::Left:   pos = {0.f,  kViewDist, 0.f}; up = Vec3::Up();      break;
        case OrthoView::Right:  pos = {0.f, -kViewDist, 0.f}; up = Vec3::Up();      break;
        default:
            UZ_LOG(Camera, Warning, "CameraManager::SetOrthographicView: unknown view {}", static_cast<int>(view));
            return;
    }

    cam.SetPosition(pos);
    cam.LookAt(Vec3{0.f, 0.f, 0.f}, up);
    cam.SetOrthographic(orthoW, orthoH, kOrthoNear, kOrthoFar);

    UZ_LOG(Camera, Info, "CameraManager: ortho view '{}' (frustum={}x{}, dist={})",
           OrthoViewName(view), orthoW, orthoH, kViewDist);
}

void CameraManager::RestorePerspectiveView() {
    if (m_activeOrthoView == OrthoView::None) {
        return; // Not in ortho mode — nothing to restore.
    }

    if (!m_savedPerspective.valid) {
        UZ_LOG(Camera, Warning, "CameraManager::RestorePerspectiveView: no saved state");
        m_activeOrthoView = OrthoView::None;
        return;
    }

    Camera& cam = GetActiveCamera();
    cam.SetPosition(m_savedPerspective.position);
    cam.SetOrientation(m_savedPerspective.orientation);
    cam.SetPerspective(m_savedPerspective.fovY,
                       m_savedPerspective.aspectRatio,
                       m_savedPerspective.nearPlane,
                       m_savedPerspective.farPlane);

    UZ_LOG(Camera, Info, "CameraManager: restored perspective view (pos={},{},{} fovY={:.3f})",
           m_savedPerspective.position.x, m_savedPerspective.position.y,
           m_savedPerspective.position.z, m_savedPerspective.fovY);

    m_activeOrthoView = OrthoView::None;
    m_savedPerspective.valid = false;
}

} // namespace uz
