// gizmo_registry.cpp — GizmoRegistry implementation.

#include "gizmo_registry.h"

namespace uz {

void GizmoRegistry::registerGizmo(uint16_t nodeType, std::unique_ptr<GizmoRenderer> gizmo) {
    if (nodeType < kMaxNodeType) {
        gizmos_[nodeType] = std::move(gizmo);
    }
}

void GizmoRegistry::setDefaultGizmo(std::unique_ptr<GizmoRenderer> gizmo) {
    default_ = std::move(gizmo);
}

void GizmoRegistry::initAll() {
    if (default_) {
        default_->init();
    }
    for (auto& g : gizmos_) {
        if (g) {
            g->init();
        }
    }
}

void GizmoRegistry::shutdownAll() {
    for (auto& g : gizmos_) {
        if (g) {
            g->shutdown();
        }
    }
    if (default_) {
        default_->shutdown();
    }
}

void GizmoRegistry::suppressGizmo(uint16_t nodeType) {
    if (nodeType < kMaxNodeType)
        suppressed_[nodeType] = true;
}

GizmoRenderer* GizmoRegistry::get(uint16_t nodeType) const {
    if (nodeType < kMaxNodeType && suppressed_[nodeType])
        return nullptr;
    if (nodeType < kMaxNodeType && gizmos_[nodeType])
        return gizmos_[nodeType].get();
    return default_.get();
}

} // namespace uz
