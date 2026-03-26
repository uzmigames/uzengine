/// VFXParameterStore implementation — named parameter storage with GPU packing.

#include "engine/vfx/parameters/vfx_parameter_store.h"

#include <algorithm>
#include <cstring>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Setters — store values as Vec4, track insertion order
// ─────────────────────────────────────────────────────────────────────────────

void VFXParameterStore::SetFloat(const std::string& name, float value) {
    SetVec4(name, Vec4{value, 0.0f, 0.0f, 0.0f});
}

void VFXParameterStore::SetVec3(const std::string& name, const Vec3& value) {
    SetVec4(name, Vec4{value.x, value.y, value.z, 0.0f});
}

void VFXParameterStore::SetVec4(const std::string& name, const Vec4& value) {
    auto [it, inserted] = values_.emplace(name, value);
    if (!inserted) {
        // Overwrite existing value, preserve insertion order
        it->second = value;
    } else {
        insertionOrder_.push_back(name);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Getters — look up by name, return default if missing
// ─────────────────────────────────────────────────────────────────────────────

float VFXParameterStore::GetFloat(const std::string& name,
                                  float defaultValue) const {
    const auto it = values_.find(name);
    if (it == values_.end()) {
        return defaultValue;
    }
    return it->second.x;
}

Vec3 VFXParameterStore::GetVec3(const std::string& name,
                                const Vec3& defaultValue) const {
    const auto it = values_.find(name);
    if (it == values_.end()) {
        return defaultValue;
    }
    return it->second.xyz();
}

Vec4 VFXParameterStore::GetVec4(const std::string& name,
                                const Vec4& defaultValue) const {
    const auto it = values_.find(name);
    if (it == values_.end()) {
        return defaultValue;
    }
    return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Query & mutation
// ─────────────────────────────────────────────────────────────────────────────

bool VFXParameterStore::Has(const std::string& name) const {
    return values_.count(name) > 0;
}

bool VFXParameterStore::Remove(const std::string& name) {
    const auto erased = values_.erase(name);
    if (erased == 0) {
        return false;
    }
    // Remove from insertion-order list
    auto it = std::find(insertionOrder_.begin(), insertionOrder_.end(), name);
    if (it != insertionOrder_.end()) {
        insertionOrder_.erase(it);
    }
    return true;
}

void VFXParameterStore::Clear() {
    values_.clear();
    insertionOrder_.clear();
}

size_t VFXParameterStore::GetCount() const noexcept {
    return values_.size();
}

std::vector<std::string> VFXParameterStore::GetNames() const {
    return insertionOrder_;
}

// ─────────────────────────────────────────────────────────────────────────────
// GPU buffer packing — sequential Vec4 values in insertion order
// ─────────────────────────────────────────────────────────────────────────────

uint32_t VFXParameterStore::GetUploadSize() const noexcept {
    return static_cast<uint32_t>(values_.size()) * 16u;
}

uint32_t VFXParameterStore::PackToBuffer(void* outBuffer,
                                         uint32_t bufferSize) const {
    const uint32_t requiredSize = GetUploadSize();
    if (requiredSize == 0) {
        return 0;
    }
    if (outBuffer == nullptr || bufferSize < requiredSize) {
        return 0;
    }

    auto* dst = static_cast<uint8_t*>(outBuffer);
    for (const auto& name : insertionOrder_) {
        const auto it = values_.find(name);
        // Insertion order and map are always in sync
        std::memcpy(dst, it->second.data(), 16);
        dst += 16;
    }

    return requiredSize;
}

} // namespace uz
