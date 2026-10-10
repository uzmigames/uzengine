#include "vfx_inheritance_inspector_widget.h"

#include "engine/logging/log.h"

#include <json.hpp>

#include <algorithm>

UZ_DECLARE_LOG_CATEGORY(VFX);

namespace uz {

const char* VfxInheritanceRowKindName(VfxInheritanceRowKind kind) noexcept {
    switch (kind) {
        case VfxInheritanceRowKind::Inherited:       return "inherited";
        case VfxInheritanceRowKind::OverrideAdd:     return "add";
        case VfxInheritanceRowKind::OverrideReplace: return "replace";
        case VfxInheritanceRowKind::OverrideRemove:  return "remove";
        default: return "unknown";
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SetInputModel
// ─────────────────────────────────────────────────────────────────────────────

void VfxInheritanceInspectorWidget::SetInputModel(const InputModel& model) {
    model_      = model;
    parentPath_.clear();
    lastError_.clear();
    rows_.clear();
    Resolve();
}

// ─────────────────────────────────────────────────────────────────────────────
// Resolve
// ─────────────────────────────────────────────────────────────────────────────

void VfxInheritanceInspectorWidget::Resolve() {
    rows_.clear();
    lastError_.clear();
    ResolveInternal();
    if (onResolved) onResolved();
}

void VfxInheritanceInspectorWidget::ResolveInternal() {
    using json = nlohmann::json;

    if (!model_.childJson) {
        lastError_ = "no input model bound";
        return;
    }

    json child;
    try {
        child = json::parse(*model_.childJson);
    } catch (const std::exception& e) {
        lastError_ = std::string("child JSON parse error: ") + e.what();
        return;
    }

    // Read parent path from child.
    if (child.contains("parent") && child["parent"].is_string()) {
        parentPath_ = child["parent"].get<std::string>();
    } else {
        parentPath_.clear();
    }

    // Read child's modules array.
    std::vector<std::pair<std::string, std::string>> childMods; // (type, action)
    if (child.contains("modules") && child["modules"].is_array()) {
        for (const auto& entry : child["modules"]) {
            if (!entry.is_object()) continue;
            std::string type   = entry.value("type",   std::string{});
            std::string action = entry.value("action", std::string{"add"});
            childMods.emplace_back(std::move(type), std::move(action));
        }
    }

    // Load parent modules (by type) if a parent is bound.
    std::vector<std::string> parentModTypes;
    if (!parentPath_.empty() && model_.parentLoader) {
        std::string parentText = model_.parentLoader(parentPath_);
        if (parentText.empty()) {
            lastError_ = "parent '" + parentPath_ + "' not found";
            // Still populate rows from child only.
        } else {
            try {
                json parent = json::parse(parentText);
                if (parent.contains("modules") && parent["modules"].is_array()) {
                    for (const auto& entry : parent["modules"]) {
                        if (!entry.is_object()) continue;
                        parentModTypes.push_back(entry.value("type", std::string{}));
                    }
                }
            } catch (const std::exception& e) {
                lastError_ = std::string("parent JSON parse error: ") + e.what();
            }
        }
    }

    // Walk parent modules first; classify as Inherited unless a child
    // override with action=replace/remove targets the same type.
    for (const auto& ptype : parentModTypes) {
        VfxInheritanceRowKind kind = VfxInheritanceRowKind::Inherited;
        uint32_t childIdx = UINT32_MAX;
        for (size_t i = 0; i < childMods.size(); ++i) {
            if (childMods[i].first != ptype) continue;
            if (childMods[i].second == "replace") {
                kind = VfxInheritanceRowKind::OverrideReplace;
                childIdx = static_cast<uint32_t>(i);
                break;
            }
            if (childMods[i].second == "remove") {
                kind = VfxInheritanceRowKind::OverrideRemove;
                childIdx = static_cast<uint32_t>(i);
                break;
            }
        }
        VfxInheritanceRow row;
        row.moduleType = ptype;
        row.kind = kind;
        row.childModulesIndex = childIdx;
        rows_.push_back(std::move(row));
    }

    // Append the child's own modules, mirroring `VfxResolveInheritance`:
    //   - no parent: the resolver keeps every child entry verbatim (the
    //     action verb is stripped), so every entry is the child's own;
    //   - parent bound: matched replace/remove entries are already shown
    //     on their parent row, an unmatched `remove` is dropped, and an
    //     unmatched `replace` is appended like an `add`.
    // A child "add" duplicating a parent type still renders as add.
    const bool hasParent = !parentPath_.empty();
    const auto parentHasType = [&parentModTypes](const std::string& type) {
        return std::find(parentModTypes.begin(), parentModTypes.end(), type) !=
               parentModTypes.end();
    };
    for (size_t i = 0; i < childMods.size(); ++i) {
        const auto& [type, action] = childMods[i];
        if (hasParent) {
            if (action == "remove") continue;
            if (action == "replace" && parentHasType(type)) continue;
        }
        VfxInheritanceRow row;
        row.moduleType = type;
        row.kind = VfxInheritanceRowKind::OverrideAdd;
        row.childModulesIndex = static_cast<uint32_t>(i);
        rows_.push_back(std::move(row));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ClearParent / RevertOverride
// ─────────────────────────────────────────────────────────────────────────────

void VfxInheritanceInspectorWidget::ClearParent() {
    if (!model_.childJson) return;
    using json = nlohmann::json;
    json child;
    try {
        child = json::parse(*model_.childJson);
    } catch (...) {
        return;
    }
    child["parent"] = "";
    *model_.childJson = child.dump(2);
    parentPath_.clear();
    Resolve();
}

void VfxInheritanceInspectorWidget::RevertOverride(uint32_t rowIndex) {
    if (!model_.childJson) return;
    if (rowIndex >= rows_.size()) return;
    const uint32_t childIdx = rows_[rowIndex].childModulesIndex;
    if (childIdx == UINT32_MAX) return;

    using json = nlohmann::json;
    json child;
    try {
        child = json::parse(*model_.childJson);
    } catch (...) {
        return;
    }
    if (!child.contains("modules") || !child["modules"].is_array()) return;
    auto& mods = child["modules"];
    if (childIdx >= mods.size()) return;

    mods.erase(mods.begin() + childIdx);
    *model_.childJson = child.dump(2);
    Resolve();
}

} // namespace uz
