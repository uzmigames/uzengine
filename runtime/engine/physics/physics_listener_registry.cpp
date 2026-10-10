#include "physics_listener_registry.h"

#include <algorithm>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Contact listeners
// ─────────────────────────────────────────────────────────────────────────────

PhysicsListenerHandle PhysicsListenerRegistry::RegisterContactListener(
    IPhysicsContactListener* listener) {
    if (listener == nullptr) return kInvalidPhysicsListenerHandle;
    const PhysicsListenerHandle h = nextHandle_++;
    contactSlots_.push_back({h, listener});
    return h;
}

void PhysicsListenerRegistry::UnregisterContactListener(
    PhysicsListenerHandle handle) noexcept {
    if (handle == kInvalidPhysicsListenerHandle) return;
    contactSlots_.erase(
        std::remove_if(contactSlots_.begin(), contactSlots_.end(),
                       [handle](const ContactSlot& s) { return s.handle == handle; }),
        contactSlots_.end());
}

void PhysicsListenerRegistry::DispatchContactAdded(
    const PhysicsContactEvent& event) {
    // Copy the slot list before dispatch so subscribers can re-register
    // (add / remove) during the callback without iterator invalidation.
    const auto snapshot = contactSlots_;
    for (const auto& s : snapshot) {
        if (s.listener != nullptr) s.listener->OnContactAdded(event);
    }
}

void PhysicsListenerRegistry::DispatchContactPersisted(
    const PhysicsContactEvent& event) {
    const auto snapshot = contactSlots_;
    for (const auto& s : snapshot) {
        if (s.listener != nullptr) s.listener->OnContactPersisted(event);
    }
}

void PhysicsListenerRegistry::DispatchContactRemoved(uint64_t bodyIdA,
                                                       uint64_t bodyIdB) {
    const auto snapshot = contactSlots_;
    for (const auto& s : snapshot) {
        if (s.listener != nullptr) s.listener->OnContactRemoved(bodyIdA, bodyIdB);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Fracture listeners
// ─────────────────────────────────────────────────────────────────────────────

PhysicsListenerHandle PhysicsListenerRegistry::RegisterFractureListener(
    IPhysicsFractureListener* listener) {
    if (listener == nullptr) return kInvalidPhysicsListenerHandle;
    const PhysicsListenerHandle h = nextHandle_++;
    fractureSlots_.push_back({h, listener});
    return h;
}

void PhysicsListenerRegistry::UnregisterFractureListener(
    PhysicsListenerHandle handle) noexcept {
    if (handle == kInvalidPhysicsListenerHandle) return;
    fractureSlots_.erase(
        std::remove_if(fractureSlots_.begin(), fractureSlots_.end(),
                       [handle](const FractureSlot& s) { return s.handle == handle; }),
        fractureSlots_.end());
}

void PhysicsListenerRegistry::DispatchFracture(
    const VfxFractureEventInput& event) {
    const auto snapshot = fractureSlots_;
    for (const auto& s : snapshot) {
        if (s.listener != nullptr) s.listener->OnFracture(event);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Introspection
// ─────────────────────────────────────────────────────────────────────────────

uint32_t PhysicsListenerRegistry::ContactListenerCount() const noexcept {
    return static_cast<uint32_t>(contactSlots_.size());
}

uint32_t PhysicsListenerRegistry::FractureListenerCount() const noexcept {
    return static_cast<uint32_t>(fractureSlots_.size());
}

void PhysicsListenerRegistry::Clear() noexcept {
    contactSlots_.clear();
    fractureSlots_.clear();
}

} // namespace uz
