#include "vfx_attribute_store.h"

#include "log.h"

#include <cstddef>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Scope introspection helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string_view VfxAttributeScopeToString(VfxAttributeScope scope) {
    switch (scope) {
        case VfxAttributeScope::Particles: return "Particles";
        case VfxAttributeScope::Emitter:   return "Emitter";
        case VfxAttributeScope::System:    return "System";
    }
    return "Unknown";
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxAttributeKey — canonical string form
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxAttributeKey::ToString() const {
    std::string out;
    out.reserve(name.size() + 12);
    out.append(VfxAttributeScopeToString(scope));
    out.push_back('.');
    out.append(name);
    return out;
}

bool VfxAttributeKey::FromString(std::string_view canonical, VfxAttributeKey& outKey) {
    const auto dot = canonical.find('.');
    if (dot == std::string_view::npos || dot == 0 || dot + 1 >= canonical.size()) {
        return false;
    }
    const auto prefix = canonical.substr(0, dot);
    if (prefix == "Particles") {
        outKey.scope = VfxAttributeScope::Particles;
    } else if (prefix == "Emitter") {
        outKey.scope = VfxAttributeScope::Emitter;
    } else if (prefix == "System") {
        outKey.scope = VfxAttributeScope::System;
    } else {
        return false;
    }
    outKey.name.assign(canonical.substr(dot + 1));
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// VfxAttributeStore — scope dispatch
// ─────────────────────────────────────────────────────────────────────────────

VFXParameterStore& VfxAttributeStore::StoreFor(VfxAttributeScope scope) {
    const auto idx = static_cast<std::size_t>(scope);
    // Defensive: static_assert at the enum wouldn't catch future additions,
    // so we rely on the kVfxAttributeScopeCount contract here.
    if (idx >= scopes_.size()) {
        UZ_LOG(VFX, Error,
               "VfxAttributeStore: invalid scope index {} (max {})",
               idx, scopes_.size());
        return scopes_[0];
    }
    return scopes_[idx];
}

const VFXParameterStore& VfxAttributeStore::StoreFor(VfxAttributeScope scope) const {
    const auto idx = static_cast<std::size_t>(scope);
    if (idx >= scopes_.size()) {
        UZ_LOG(VFX, Error,
               "VfxAttributeStore: invalid scope index {} (max {})",
               idx, scopes_.size());
        return scopes_[0];
    }
    return scopes_[idx];
}

// ─────────────────────────────────────────────────────────────────────────────
// Typed setters / getters — thin pass-through to the scope-local store.
// ─────────────────────────────────────────────────────────────────────────────

void VfxAttributeStore::SetFloat(VfxAttributeScope scope, const std::string& name,
                                 float value) {
    StoreFor(scope).SetFloat(name, value);
}

void VfxAttributeStore::SetVec3(VfxAttributeScope scope, const std::string& name,
                                const Vec3& value) {
    StoreFor(scope).SetVec3(name, value);
}

void VfxAttributeStore::SetVec4(VfxAttributeScope scope, const std::string& name,
                                const Vec4& value) {
    StoreFor(scope).SetVec4(name, value);
}

float VfxAttributeStore::GetFloat(VfxAttributeScope scope, const std::string& name,
                                  float defaultValue) const {
    return StoreFor(scope).GetFloat(name, defaultValue);
}

Vec3 VfxAttributeStore::GetVec3(VfxAttributeScope scope, const std::string& name,
                                const Vec3& defaultValue) const {
    return StoreFor(scope).GetVec3(name, defaultValue);
}

Vec4 VfxAttributeStore::GetVec4(VfxAttributeScope scope, const std::string& name,
                                const Vec4& defaultValue) const {
    return StoreFor(scope).GetVec4(name, defaultValue);
}

// ─────────────────────────────────────────────────────────────────────────────
// Presence + enumeration + scope clearing
// ─────────────────────────────────────────────────────────────────────────────

bool VfxAttributeStore::Has(VfxAttributeScope scope, const std::string& name) const {
    return StoreFor(scope).Has(name);
}

bool VfxAttributeStore::Remove(VfxAttributeScope scope, const std::string& name) {
    return StoreFor(scope).Remove(name);
}

size_t VfxAttributeStore::GetCount(VfxAttributeScope scope) const noexcept {
    return StoreFor(scope).GetCount();
}

std::vector<std::string> VfxAttributeStore::GetNames(VfxAttributeScope scope) const {
    return StoreFor(scope).GetNames();
}

void VfxAttributeStore::Clear(VfxAttributeScope scope) {
    StoreFor(scope).Clear();
}

void VfxAttributeStore::ClearAll() {
    for (auto& scope : scopes_) {
        scope.Clear();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// CB layout + pack
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VfxAttributeStore::GetUploadSize(VfxAttributeScope scope) const noexcept {
    return StoreFor(scope).GetUploadSize();
}

uint32_t VfxAttributeStore::PackScopeToBuffer(VfxAttributeScope scope,
                                              void* outBuffer,
                                              uint32_t bufferSize) const {
    return StoreFor(scope).PackToBuffer(outBuffer, bufferSize);
}

} // namespace uz
