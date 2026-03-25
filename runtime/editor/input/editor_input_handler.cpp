/// EditorInputHandler — Implementation of editor input routing with gizmo interaction.

#include "editor_input_handler.h"

#include "camera.h"
#include "input.h"
#include "log.h"
#include "scene_component.h"
#include "scene_renderer.h"
#include "selection_manager.h"
#include "world.h"

#include <cmath>

UZ_DEFINE_LOG_CATEGORY(Editor, Trace);

namespace uz {

void EditorInputHandler::Init(InputSystem* input, SelectionManager* selection,
                               Camera* camera, World* world) {
    m_input = input;
    m_selection = selection;
    m_camera = camera;
    m_world = world;
}

int EditorInputHandler::HitResultToAxisIndex(GizmoHitResult hit) {
    switch (hit) {
    case GizmoHitResult::None:    return -1;
    case GizmoHitResult::AxisX:   return 0;
    case GizmoHitResult::AxisY:   return 1;
    case GizmoHitResult::AxisZ:   return 2;
    case GizmoHitResult::PlaneXY: return 3;
    case GizmoHitResult::PlaneXZ: return 4;
    case GizmoHitResult::PlaneYZ: return 5;
    case GizmoHitResult::Center:  return 6;
    }
    return -1;
}

int EditorInputHandler::GetGizmoHoveredAxis() const {
    return HitResultToAxisIndex(m_gizmoInteraction.GetHoveredAxis());
}

int EditorInputHandler::GetGizmoActiveAxis() const {
    return HitResultToAxisIndex(m_gizmoInteraction.GetActiveAxis());
}

void EditorInputHandler::Update(float /*dt*/) {
    if (!m_input) return;

    // ── Gizmo mode hotkeys (only when not dragging) ─────────────────────
    if (!m_gizmoInteraction.IsDragging()) {
        if (m_input->IsKeyJustPressed(Key::W)) {
            m_mode = GizmoMode::Translate;
            UZ_LOG(Editor, Trace, "Gizmo mode: Translate");
        }
        if (m_input->IsKeyJustPressed(Key::E)) {
            m_mode = GizmoMode::Rotate;
            UZ_LOG(Editor, Trace, "Gizmo mode: Rotate");
        }
        if (m_input->IsKeyJustPressed(Key::R)) {
            m_mode = GizmoMode::Scale;
            UZ_LOG(Editor, Trace, "Gizmo mode: Scale");
        }

        // ── Coordinate space toggle ────────────────────────────────────
        if (m_input->IsKeyJustPressed(Key::Space)) {
            m_space = (m_space == GizmoSpace::World) ? GizmoSpace::Local : GizmoSpace::World;
            UZ_LOG(Editor, Trace, "Gizmo space: {}",
                   m_space == GizmoSpace::World ? "World" : "Local");
        }
    }

    // ── Determine selected entity state ─────────────────────────────────
    const bool hasSelection = m_selection && m_selection->HasSelection();
    const bool hasRenderer = m_renderer != nullptr;
    const bool hasDeps = m_camera && m_world && hasRenderer;

    // Get viewport dimensions from the renderer.
    float vpWidth = 0.f;
    float vpHeight = 0.f;
    if (hasRenderer) {
        vpWidth  = static_cast<float>(m_renderer->GetWidth());
        vpHeight = static_cast<float>(m_renderer->GetHeight());
    }

    const float mx = m_input->GetMouseX();
    const float my = m_input->GetMouseY();

    // ── Entity transform for gizmo operations ───────────────────────────
    Vec3 entityPos{};
    Quat entityRot = Quat::Identity();
    Vec3 entityScale = Vec3::One();
    bool entityValid = false;

    if (hasSelection && m_world) {
        const Entity selected = m_selection->GetSelected();
        if (m_world->HasComponent<SceneComponent>(selected)) {
            auto& sc = m_world->GetComponent<SceneComponent>(selected);
            entityPos = sc.GetPosition();
            entityRot = sc.GetRotation();
            entityScale = sc.GetScale();
            entityValid = true;
        }
    }

    // ── Gizmo hover hit testing (every frame) ───────────────────────────
    if (entityValid && hasDeps && !m_gizmoInteraction.IsDragging()) {
        // Compute screen scale to match the renderer's formula.
        const Vec3 camPos = m_camera->GetPosition();
        const float dx = entityPos.x - camPos.x;
        const float dy = entityPos.y - camPos.y;
        const float dz = entityPos.z - camPos.z;
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        const float tanHalfFov = std::tan(m_camera->GetFovY() * 0.5f);
        const float screenScale = dist * tanHalfFov * (120.0f / 720.0f);

        const GizmoHitResult hovered = m_gizmoInteraction.HitTest(
            mx, my, vpWidth, vpHeight, *m_camera,
            entityPos, screenScale,
            static_cast<uint8_t>(m_mode),
            static_cast<uint8_t>(m_space),
            entityRot);

        m_gizmoInteraction.SetHoveredAxis(hovered);
    } else if (!m_gizmoInteraction.IsDragging()) {
        m_gizmoInteraction.SetHoveredAxis(GizmoHitResult::None);
    }

    // ── Left mouse down: begin drag or select ───────────────────────────
    if (m_input->IsMouseJustPressed(MouseButton::Left) &&
        !m_input->IsMouseDown(MouseButton::Right)) {

        const GizmoHitResult hovered = m_gizmoInteraction.GetHoveredAxis();

        if (entityValid && hasDeps && hovered != GizmoHitResult::None) {
            // Gizmo was hit — start drag instead of selection.
            const Vec3 camPos = m_camera->GetPosition();
            const float dx = entityPos.x - camPos.x;
            const float dy = entityPos.y - camPos.y;
            const float dz = entityPos.z - camPos.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float tanHalfFov = std::tan(m_camera->GetFovY() * 0.5f);
            const float screenScale = dist * tanHalfFov * (120.0f / 720.0f);

            m_gizmoInteraction.BeginDrag(
                hovered, mx, my, vpWidth, vpHeight, *m_camera,
                entityPos, entityRot, entityScale,
                screenScale,
                static_cast<uint8_t>(m_mode),
                static_cast<uint8_t>(m_space));

            UZ_LOG(Editor, Trace, "Gizmo drag started: axis={}",
                   static_cast<int>(hovered));
        } else if (m_selection && m_camera && m_world && hasRenderer) {
            // No gizmo hit — perform entity selection.
            m_selection->SelectAtScreenPos(mx, my, vpWidth, vpHeight,
                                            m_camera, m_world, m_renderer);
        }
    }

    // ── Left mouse held: update drag ────────────────────────────────────
    if (m_gizmoInteraction.IsDragging() && m_input->IsMouseDown(MouseButton::Left)) {
        if (m_camera && entityValid) {
            // Check if snapping is active (Ctrl held).
            const bool snapEnabled = m_input->IsKeyDown(Key::LeftCtrl);

            // Choose snap grid based on current mode.
            float snapGrid = 0.f;
            if (snapEnabled) {
                switch (m_mode) {
                case GizmoMode::Translate: snapGrid = kDefaultTranslateSnap; break;
                case GizmoMode::Rotate:    snapGrid = kDefaultRotateSnap;    break;
                case GizmoMode::Scale:     snapGrid = kDefaultScaleSnap;     break;
                }
            }

            Vec3 deltaPos;
            Quat deltaRot;
            Vec3 deltaScale;

            if (m_gizmoInteraction.UpdateDrag(mx, my, vpWidth, vpHeight, *m_camera,
                                               snapEnabled, snapGrid,
                                               deltaPos, deltaRot, deltaScale)) {
                // Apply the transform delta to the entity's SceneComponent.
                const Entity selected = m_selection->GetSelected();
                if (m_world->HasComponent<SceneComponent>(selected)) {
                    auto& sc = m_world->GetComponent<SceneComponent>(selected);

                    if (m_mode == GizmoMode::Translate) {
                        // UpdateDrag returns total delta from drag start mouse position,
                        // so we apply it as an absolute offset from the drag start transform.
                        sc.SetPosition(m_gizmoInteraction.GetDragStartPos() + deltaPos);
                    } else if (m_mode == GizmoMode::Rotate) {
                        // Total rotation from drag start, applied to the drag start rotation.
                        sc.SetRotation(deltaRot * m_gizmoInteraction.GetDragStartRot());
                    } else if (m_mode == GizmoMode::Scale) {
                        // Total scale offset from drag start.
                        sc.SetScale(m_gizmoInteraction.GetDragStartScale() + deltaScale);
                    }
                }
            }
        }
    }

    // ── Left mouse up: end drag ─────────────────────────────────────────
    if (m_gizmoInteraction.IsDragging() && m_input->IsMouseJustReleased(MouseButton::Left)) {
        m_gizmoInteraction.EndDrag();
        UZ_LOG(Editor, Trace, "Gizmo drag ended");
    }
}

} // namespace uz
