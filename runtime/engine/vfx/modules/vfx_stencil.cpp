#include "vfx_stencil.h"

#include <cstring>

namespace uz {

bool VfxStencilConfig::IsValid() const noexcept {
    // `Replace` with ref == 0 writes 0 — effectively a no-op relative to
    // the cleared stencil. Flag as invalid so the authoring UI can warn.
    if (writeMode == VfxStencilWriteMode::Replace && stencilRef == 0u) {
        return false;
    }
    return true;
}

const char* VfxStencilWriteModeToString(VfxStencilWriteMode mode) noexcept {
    switch (mode) {
        case VfxStencilWriteMode::Disabled:  return "disabled";
        case VfxStencilWriteMode::Replace:   return "replace";
        case VfxStencilWriteMode::Increment: return "increment";
    }
    return "disabled";
}

bool VfxStencilWriteModeFromString(const char* name,
                                     VfxStencilWriteMode& out) noexcept {
    if (name == nullptr) return false;
    if (std::strcmp(name, "disabled")  == 0) { out = VfxStencilWriteMode::Disabled;  return true; }
    if (std::strcmp(name, "replace")   == 0) { out = VfxStencilWriteMode::Replace;   return true; }
    if (std::strcmp(name, "increment") == 0) { out = VfxStencilWriteMode::Increment; return true; }
    return false;
}

bool VfxStencilMaskMatches(uint8_t sampledStencil,
                             const VfxStencilConfig& cfg) noexcept {
    switch (cfg.writeMode) {
        case VfxStencilWriteMode::Disabled:
            return false;
        case VfxStencilWriteMode::Replace:
            return sampledStencil == cfg.stencilRef;
        case VfxStencilWriteMode::Increment:
            return sampledStencil >= cfg.stencilRef;
    }
    return false;
}

} // namespace uz
