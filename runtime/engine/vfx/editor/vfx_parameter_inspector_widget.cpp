/// VfxParameterInspectorWidget — implementation.
///
/// Phase 11.4.0. See `vfx_parameter_inspector_widget.h`. All mutations
/// route through the bound `VfxAttributeStore` + local type registry;
/// rename rewrites reference tokens in every `VfxModuleInstance::configJson`
/// on the bound stack.

#include "vfx_parameter_inspector_widget.h"

#include "editor/font/text_renderer.h"
#include "editor/font/bitmap_font.h"
#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_editor_icons.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <algorithm>
#include <cstdio>

namespace uz {

namespace {

// ─── Palette (phase 12.1.0 — theme-sourced) ─────────────────────────────────

const Vec4& kColorBg         = VfxThemeColors().panelBg;
const Vec4& kColorHeaderBg   = VfxThemeColors().barBg;
const Vec4& kColorRow        = VfxThemeColors().cardBg;
const Vec4& kColorRowSel     = VfxThemeColors().cardBgSelected;
const Vec4& kColorAddRow     = VfxThemeColors().panelBgAlt;
const Vec4& kColorField      = VfxThemeColors().inputBg;
const Vec4& kColorDeleteBtn  = VfxThemeColors().stateError;
const Vec4& kColorJumpBtn    = VfxThemeColors().accentMuted;
const Vec4& kColorText       = VfxThemeColors().textPrimary;
const Vec4& kColorTextDim    = VfxThemeColors().textDim;
const Vec4& kColorTextAccent = VfxThemeColors().textAccent;
const Vec4& kColorDisabled   = VfxThemeColors().textDisabled;

constexpr float kTextBaseY = 4.0f;

[[nodiscard]] const char* ScopeLabel(VfxAttributeScope s) noexcept {
    switch (s) {
        case VfxAttributeScope::Particles: return "Particles";
        case VfxAttributeScope::Emitter:   return "Emitter";
        case VfxAttributeScope::System:    return "System";
    }
    return "?";
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Reference token
// ─────────────────────────────────────────────────────────────────────────────

std::string VfxParameterInspectorWidget::BuildReferenceToken(
    const std::string& name) {
    std::string t;
    t.reserve(name.size() + 1u);
    t.push_back('@');
    t.append(name);
    return t;
}

// ─────────────────────────────────────────────────────────────────────────────
// Type registry helpers
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::SetTypeEntry(
    VfxAttributeScope scope, const std::string& name, VfxParamType type) {
    m_types[{scope, name}] = type;
}

void VfxParameterInspectorWidget::EraseTypeEntry(
    VfxAttributeScope scope, const std::string& name) {
    m_types.erase({scope, name});
    // Phase 12.4.0 — also drop section + range companion entries so the
    // widget doesn't leak annotations for removed parameters.
    m_sectionNames.erase({scope, name});
    m_ranges.erase({scope, name});
}

VfxParamType VfxParameterInspectorWidget::GetType(
    VfxAttributeScope scope, const std::string& name) const noexcept {
    auto it = m_types.find({scope, name});
    if (it == m_types.end()) return VfxParamType::Float;
    return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Override flags (phase 11.11)
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::SetOverrideFlag(
    VfxAttributeScope scope, const std::string& name, VfxOverrideFlag flag) {
    if (flag == VfxOverrideFlag::None) {
        m_overrideFlags.erase({scope, name});
        return;
    }
    m_overrideFlags[{scope, name}] = flag;
}

VfxOverrideFlag VfxParameterInspectorWidget::GetOverrideFlag(
    VfxAttributeScope scope, const std::string& name) const noexcept {
    auto it = m_overrideFlags.find({scope, name});
    if (it == m_overrideFlags.end()) return VfxOverrideFlag::None;
    return it->second;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 12.4.0 — Section grouping
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::SetParameterSection(
    VfxAttributeScope scope, const std::string& name,
    const std::string& sectionName) {
    if (m_store == nullptr) return;
    if (!m_store->Has(scope, name)) return;
    if (sectionName.empty() || sectionName == "General") {
        m_sectionNames.erase({scope, name});
        return;
    }
    m_sectionNames[{scope, name}] = sectionName;
}

std::string VfxParameterInspectorWidget::GetParameterSection(
    VfxAttributeScope scope, const std::string& name) const {
    auto it = m_sectionNames.find({scope, name});
    if (it == m_sectionNames.end() || it->second.empty()) return "General";
    return it->second;
}

std::vector<std::string> VfxParameterInspectorWidget::DistinctSections() const {
    std::vector<std::string> out;
    if (m_store == nullptr) return out;
    // Order of first appearance: iterate scopes in enum order, then
    // names in sorted order (same as ListParameters).
    const VfxAttributeScope kScopes[] = {
        VfxAttributeScope::System,
        VfxAttributeScope::Emitter,
        VfxAttributeScope::Particles,
    };
    auto push = [&](const std::string& s) {
        for (const auto& existing : out) {
            if (existing == s) return;
        }
        out.push_back(s);
    };
    for (auto scope : kScopes) {
        auto names = m_store->GetNames(scope);
        std::sort(names.begin(), names.end());
        for (const auto& n : names) {
            push(GetParameterSection(scope, n));
        }
    }
    return out;
}

bool VfxParameterInspectorWidget::IsSectionExpanded(
    const std::string& section) const {
    auto it = m_sectionExpanded.find(section);
    return (it == m_sectionExpanded.end()) ? true : it->second;
}

void VfxParameterInspectorWidget::SetSectionExpanded(
    const std::string& section, bool expanded) {
    m_sectionExpanded[section] = expanded;
}

bool VfxParameterInspectorWidget::ToggleSectionExpanded(
    const std::string& section) {
    const bool newState = !IsSectionExpanded(section);
    m_sectionExpanded[section] = newState;
    return newState;
}

// ─────────────────────────────────────────────────────────────────────────────
// Phase 12.4.0 — Range validation
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::AddParameterRange(
    VfxAttributeScope scope, const std::string& name,
    float minValue, float maxValue) {
    if (m_store == nullptr || !m_store->Has(scope, name)) return;
    if (maxValue < minValue) std::swap(minValue, maxValue);
    m_ranges[{scope, name}] = ParamRange{minValue, maxValue};
}

bool VfxParameterInspectorWidget::IsValueInRange(
    VfxAttributeScope scope, const std::string& name,
    float value) const noexcept {
    auto it = m_ranges.find({scope, name});
    if (it == m_ranges.end()) return true;
    return value >= it->second.minValue && value <= it->second.maxValue;
}

// ─────────────────────────────────────────────────────────────────────────────
// Enumeration
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxParameterRow> VfxParameterInspectorWidget::ListParameters(
    VfxAttributeScope scope) const {
    std::vector<VfxParameterRow> out;
    if (m_store == nullptr) return out;

    auto names = m_store->GetNames(scope);
    std::sort(names.begin(), names.end());
    out.reserve(names.size());
    for (const auto& n : names) {
        VfxParameterRow row;
        row.scope = scope;
        row.name  = n;
        row.type  = GetType(scope, n);
        // GetVec4 returns the whole stored value regardless of authored type.
        row.value = m_store->GetVec4(scope, n, Vec4{});
        out.push_back(std::move(row));
    }
    return out;
}

std::string VfxParameterInspectorWidget::MakeUniqueName(
    VfxAttributeScope scope, const std::string& baseName) const {
    if (m_store == nullptr) return baseName;

    // If baseName is free, use it verbatim.
    if (!m_store->Has(scope, baseName)) return baseName;

    // Otherwise append a numeric suffix until free.
    char buf[64];
    for (int i = 1; i < 1000; ++i) {
        std::snprintf(buf, sizeof(buf), "%s%d", baseName.c_str(), i);
        if (!m_store->Has(scope, buf)) return std::string(buf);
    }
    return baseName; // give up — caller will see the collision
}

// ─────────────────────────────────────────────────────────────────────────────
// Add / remove / rename / retype / rescope
// ─────────────────────────────────────────────────────────────────────────────

bool VfxParameterInspectorWidget::AddParameter(
    VfxAttributeScope scope, const std::string& name,
    VfxParamType type, const Vec4& value) {
    if (m_store == nullptr) return false;
    if (name.empty()) return false;
    if (m_store->Has(scope, name)) return false;

    switch (type) {
        case VfxParamType::Float:
            m_store->SetFloat(scope, name, value.x);
            break;
        case VfxParamType::Vec3:
            m_store->SetVec3(scope, name, Vec3{value.x, value.y, value.z});
            break;
        case VfxParamType::Vec4:
            m_store->SetVec4(scope, name, value);
            break;
    }
    SetTypeEntry(scope, name, type);
    return true;
}

bool VfxParameterInspectorWidget::RemoveParameter(
    VfxAttributeScope scope, const std::string& name) {
    if (m_store == nullptr) return false;
    if (name.empty()) return false;
    if (!m_store->Has(scope, name)) return false;
    if (CountReferences(name) > 0u) return false;

    const bool removed = m_store->Remove(scope, name);
    if (removed) {
        EraseTypeEntry(scope, name);
        if (m_selectedScope == scope && m_selectedName == name) {
            m_selectedName.clear();
        }
    }
    return removed;
}

bool VfxParameterInspectorWidget::RenameParameter(
    VfxAttributeScope scope, const std::string& oldName,
    const std::string& newName) {
    if (m_store == nullptr) return false;
    if (oldName.empty() || newName.empty()) return false;
    if (oldName == newName) return true;
    if (!m_store->Has(scope, oldName)) return false;
    if (m_store->Has(scope, newName)) return false;

    // Snapshot current value + type.
    const Vec4          value = m_store->GetVec4(scope, oldName, Vec4{});
    const VfxParamType  type  = GetType(scope, oldName);

    // Reinsert under new key, erase old.
    switch (type) {
        case VfxParamType::Float:
            m_store->SetFloat(scope, newName, value.x);
            break;
        case VfxParamType::Vec3:
            m_store->SetVec3(scope, newName, Vec3{value.x, value.y, value.z});
            break;
        case VfxParamType::Vec4:
            m_store->SetVec4(scope, newName, value);
            break;
    }
    (void)m_store->Remove(scope, oldName);

    EraseTypeEntry(scope, oldName);
    SetTypeEntry(scope, newName, type);

    // Rewrite every `@oldName` token in every module's configJson on the
    // bound stack.
    if (m_stack != nullptr) {
        const std::string oldToken = BuildReferenceToken(oldName);
        const std::string newToken = BuildReferenceToken(newName);
        for (auto& entry : m_stack->Entries()) {
            for (auto& mod : entry.modules) {
                std::string& s = mod.configJson;
                if (s.empty()) continue;
                size_t p = 0;
                while ((p = s.find(oldToken, p)) != std::string::npos) {
                    s.replace(p, oldToken.size(), newToken);
                    p += newToken.size();
                }
            }
        }
    }

    if (m_selectedScope == scope && m_selectedName == oldName) {
        m_selectedName = newName;
    }
    return true;
}

bool VfxParameterInspectorWidget::ChangeType(
    VfxAttributeScope scope, const std::string& name, VfxParamType newType) {
    if (m_store == nullptr) return false;
    if (!m_store->Has(scope, name)) return false;

    const Vec4 current = m_store->GetVec4(scope, name, Vec4{});
    switch (newType) {
        case VfxParamType::Float:
            m_store->SetFloat(scope, name, current.x);
            break;
        case VfxParamType::Vec3:
            m_store->SetVec3(scope, name,
                              Vec3{current.x, current.y, current.z});
            break;
        case VfxParamType::Vec4:
            m_store->SetVec4(scope, name, current);
            break;
    }
    SetTypeEntry(scope, name, newType);
    return true;
}

bool VfxParameterInspectorWidget::ChangeScope(
    VfxAttributeScope fromScope, const std::string& name,
    VfxAttributeScope toScope) {
    if (m_store == nullptr) return false;
    if (name.empty()) return false;
    if (fromScope == toScope) return true;
    if (!m_store->Has(fromScope, name)) return false;
    if (m_store->Has(toScope, name)) return false;

    const Vec4         value = m_store->GetVec4(fromScope, name, Vec4{});
    const VfxParamType type  = GetType(fromScope, name);

    switch (type) {
        case VfxParamType::Float:
            m_store->SetFloat(toScope, name, value.x);
            break;
        case VfxParamType::Vec3:
            m_store->SetVec3(toScope, name, Vec3{value.x, value.y, value.z});
            break;
        case VfxParamType::Vec4:
            m_store->SetVec4(toScope, name, value);
            break;
    }
    (void)m_store->Remove(fromScope, name);

    EraseTypeEntry(fromScope, name);
    SetTypeEntry(toScope, name, type);

    if (m_selectedScope == fromScope && m_selectedName == name) {
        m_selectedScope = toScope;
    }
    return true;
}

bool VfxParameterInspectorWidget::SetDefaultValue(
    VfxAttributeScope scope, const std::string& name, const Vec4& value) {
    if (m_store == nullptr) return false;
    if (!m_store->Has(scope, name)) return false;

    switch (GetType(scope, name)) {
        case VfxParamType::Float:
            m_store->SetFloat(scope, name, value.x);
            break;
        case VfxParamType::Vec3:
            m_store->SetVec3(scope, name, Vec3{value.x, value.y, value.z});
            break;
        case VfxParamType::Vec4:
            m_store->SetVec4(scope, name, value);
            break;
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Usage count + first-usage lookup
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxParameterInspectorWidget::CountReferences(
    const std::string& name) const {
    if (m_stack == nullptr || name.empty()) return 0;
    const std::string token = BuildReferenceToken(name);
    size_t n = 0;
    for (const auto& entry : m_stack->Entries()) {
        for (const auto& mod : entry.modules) {
            const std::string& s = mod.configJson;
            if (s.empty()) continue;
            // Count every occurrence — matches Niagara's parameter-panel count.
            size_t p = 0;
            while ((p = s.find(token, p)) != std::string::npos) {
                ++n;
                p += token.size();
            }
        }
    }
    return n;
}

VfxParameterInspectorWidget::UsageHit
VfxParameterInspectorWidget::FindFirstUsage(const std::string& name) const {
    UsageHit hit;
    if (m_stack == nullptr || name.empty()) return hit;
    const std::string token = BuildReferenceToken(name);
    for (const auto& entry : m_stack->Entries()) {
        for (const auto& mod : entry.modules) {
            if (!mod.configJson.empty() &&
                mod.configJson.find(token) != std::string::npos) {
                hit.stage      = entry.stage;
                hit.instanceId = mod.instanceId;
                return hit;
            }
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::SelectParameter(
    VfxAttributeScope scope, const std::string& name) {
    if (m_selectedScope == scope && m_selectedName == name) return;
    m_selectedScope = scope;
    m_selectedName  = name;
    if (onParameterSelected) onParameterSelected(scope, name);
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxParamLayoutItem> VfxParameterInspectorWidget::ComputeLayout(
    const VfxStackRect& panelBounds) const {
    std::vector<VfxParamLayoutItem> out;
    if (m_store == nullptr) return out;

    const float leftX    = panelBounds.x;
    const float rightX   = panelBounds.x + listWidth;
    const float rightW   = panelBounds.width - listWidth;
    float       y        = panelBounds.y;

    // Author-facing scopes only (hide `Particles` — it carries per-particle
    // SoA fields, not user knobs).
    const VfxAttributeScope scopes[] = {
        VfxAttributeScope::System,
        VfxAttributeScope::Emitter,
    };

    // Phase 12.4.0 — 40/55/5 label/editor/actions column split helper.
    auto splitRow = [&](float rowX, float rowY, float rowW,
                         VfxStackRect& outLabel,
                         VfxStackRect& outEditor,
                         VfxStackRect& outActions) {
        const float labelW   = std::round(rowW * 0.40f);
        const float editorW  = std::round(rowW * 0.55f);
        const float actionsW = rowW - labelW - editorW;
        outLabel   = { rowX,                     rowY, labelW,   rowHeight };
        outEditor  = { rowX + labelW,            rowY, editorW,  rowHeight };
        outActions = { rowX + labelW + editorW,  rowY, actionsW, rowHeight };
    };

    // Phase 12.4.0 — pick the typed-editor kind from the param's VfxParamType.
    auto typedEditorKind = [&](VfxAttributeScope s,
                                const std::string& n) -> VfxParamItemType {
        switch (GetType(s, n)) {
            case VfxParamType::Float: return VfxParamItemType::EditorFloat;
            case VfxParamType::Vec3:  return VfxParamItemType::EditorVec3;
            case VfxParamType::Vec4:  return VfxParamItemType::EditorColor;
        }
        return VfxParamItemType::EditorFloat;
    };

    for (const VfxAttributeScope scope : scopes) {
        // Scope header
        VfxParamLayoutItem hdr;
        hdr.type  = VfxParamItemType::ScopeHeader;
        hdr.scope = scope;
        hdr.rect  = { leftX, y, listWidth, headerHeight };
        out.push_back(hdr);
        y += headerHeight;

        if (!IsScopeExpanded(scope)) {
            y += scopeGap;
            continue;
        }

        // Add button
        VfxParamLayoutItem add;
        add.type  = VfxParamItemType::AddButton;
        add.scope = scope;
        add.rect  = { leftX, y, listWidth, addButtonHeight };
        out.push_back(add);
        y += addButtonHeight;

        // Phase 12.4.0 — group parameters by section.
        const auto rows = ListParameters(scope);
        std::vector<std::string> sectionOrder;
        std::unordered_map<std::string, std::vector<const VfxParameterRow*>> bySection;
        for (const auto& r : rows) {
            const std::string sec = GetParameterSection(scope, r.name);
            auto ins = bySection.emplace(sec, std::vector<const VfxParameterRow*>{});
            if (ins.second) sectionOrder.push_back(sec);
            ins.first->second.push_back(&r);
        }

        for (const std::string& section : sectionOrder) {
            // Section header (always emitted for hit-test / render).
            VfxParamLayoutItem shdr;
            shdr.type  = VfxParamItemType::Section;
            shdr.scope = scope;
            shdr.name  = section;
            shdr.rect  = { leftX, y, listWidth, headerHeight };
            out.push_back(shdr);
            y += headerHeight;

            // Skip rows when the section is collapsed.
            if (!IsSectionExpanded(section)) {
                continue;
            }

            // Parameter rows + per-row typed editor + reset + bind sub-rects.
            for (const auto* r : bySection[section]) {
                VfxParamLayoutItem row;
                row.type  = VfxParamItemType::ParameterRow;
                row.scope = scope;
                row.name  = r->name;
                row.rect  = { leftX, y, listWidth, rowHeight };
                out.push_back(row);

                // Split the row into 40/55/5 and emit the typed editor
                // + reset + bind sub-items as separate hit-testable rects.
                VfxStackRect labelR, editR, actR;
                splitRow(leftX, y, listWidth, labelR, editR, actR);

                VfxParamLayoutItem ed;
                ed.type  = typedEditorKind(scope, r->name);
                ed.scope = scope;
                ed.name  = r->name;
                ed.rect  = editR;
                out.push_back(ed);

                // ResetToDefault — half of the actions column.
                VfxParamLayoutItem rst;
                rst.type  = VfxParamItemType::ResetToDefault;
                rst.scope = scope;
                rst.name  = r->name;
                rst.rect  = { actR.x, actR.y,
                               actR.width * 0.5f, actR.height };
                out.push_back(rst);

                // BindToCurve — second half of the actions column. Only
                // emit for scalar types that a curve binding makes
                // sense for (Float today; future Vec3 = ramp binding).
                if (GetType(scope, r->name) == VfxParamType::Float) {
                    VfxParamLayoutItem bnd;
                    bnd.type  = VfxParamItemType::BindToCurve;
                    bnd.scope = scope;
                    bnd.name  = r->name;
                    bnd.rect  = { actR.x + actR.width * 0.5f, actR.y,
                                   actR.width * 0.5f, actR.height };
                    out.push_back(bnd);
                }

                y += rowHeight;
            }
        }

        y += scopeGap;
    }

    // ── Right pane: inspector form (only when something is selected) ────
    if (!m_selectedName.empty() && m_store->Has(m_selectedScope, m_selectedName)) {
        float iy = panelBounds.y;
        auto emit = [&](VfxParamItemType t, size_t idx = static_cast<size_t>(-1)) {
            VfxParamLayoutItem it;
            it.type  = t;
            it.scope = m_selectedScope;
            it.name  = m_selectedName;
            it.index = idx;
            it.rect  = { rightX + inspectorPadX, iy,
                          rightW - inspectorPadX * 2.0f, inspectorFieldH };
            out.push_back(it);
            iy += inspectorFieldH + inspectorFieldGap;
        };

        emit(VfxParamItemType::NameField);
        emit(VfxParamItemType::ScopeDropdown);
        emit(VfxParamItemType::TypeDropdown);

        const uint32_t comps =
            VfxParamTypeComponentCount(GetType(m_selectedScope, m_selectedName));
        for (uint32_t i = 0; i < comps; ++i) {
            emit(VfxParamItemType::ValueField, i);
        }

        emit(VfxParamItemType::JumpToUsages);
        emit(VfxParamItemType::DeleteButton);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxParamHitResult VfxParameterInspectorWidget::HitTest(
    const std::vector<VfxParamLayoutItem>& layout,
    float x, float y) const {
    VfxParamHitResult hit;
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        if (static_cast<int>(item.type) > static_cast<int>(hit.type) ||
            hit.type == VfxParamItemType::None) {
            hit.type  = item.type;
            hit.scope = item.scope;
            hit.name  = item.name;
            hit.index = item.index;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxParameterInspectorWidget::Render(
    TextRenderer& text, const BitmapFont& font,
    const VfxStackRect& panelBounds) const {
    if (m_store == nullptr) return;

    text.AddRect(panelBounds.x, panelBounds.y,
                 panelBounds.width, panelBounds.height, kColorBg);

    // Phase 14.3 — reset zebra-stripe index each frame.
    m_rowIndex = 0u;

    const auto layout = ComputeLayout(panelBounds);
    for (const auto& item : layout) {
        switch (item.type) {
        case VfxParamItemType::ScopeHeader: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorHeaderBg);
            const char* arrow = IsScopeExpanded(item.scope) ? "v" : ">";
            std::string label = arrow;
            label += "  ";
            label += ScopeLabel(item.scope);
            text.AddText(font, label, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, kColorText);
            break;
        }
        case VfxParamItemType::AddButton: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorAddRow);
            text.AddText(font, "+ Add Parameter",
                         item.rect.x + 8.0f,
                         item.rect.y + kTextBaseY, kColorTextAccent);
            break;
        }
        case VfxParamItemType::ParameterRow: {
            const bool selected = (item.scope == m_selectedScope) &&
                                   (item.name == m_selectedName);
            // Phase 14.3 — UE5 Details row:
            //   1. Zebra stripe fill (panelBg even / panelBgAlt odd).
            //   2. 40 % label column + 60 % value column.
            //   3. 1-px `borderSubtle` vertical divider at column boundary.
            //   4. 1-px `borderSubtle` bottom divider.
            //   5. `textSecondary` colour for the label column.
            const auto& palette = VfxThemeColors();
            const bool oddRow = (m_rowIndex & 1u) != 0u;
            const Vec4 rowFill = selected ? kColorRowSel
                                  : (oddRow ? palette.panelBgAlt
                                             : palette.panelBg);
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, rowFill);

            constexpr float kLabelColFrac = 0.40f;
            const float labelColW = item.rect.width * kLabelColFrac;
            const float dividerX  = item.rect.x + labelColW;

            const auto type = GetType(item.scope, item.name);
            std::string labelLine = VfxParamTypeName(type);
            labelLine += "  ";
            labelLine += item.name;
            text.AddText(font, labelLine, item.rect.x + 8.0f,
                         item.rect.y + kTextBaseY, palette.textSecondary);

            // Column divider.
            text.AddRect(dividerX, item.rect.y, 1.0f, item.rect.height,
                         palette.borderSubtle);
            // Bottom row divider.
            text.AddRect(item.rect.x, item.rect.y + item.rect.height - 1.0f,
                         item.rect.width, 1.0f, palette.borderSubtle);

            // Usage pill (right edge of label column, inside the label area).
            const size_t refs = CountReferences(item.name);
            if (refs > 0u) {
                char buf[24];
                std::snprintf(buf, sizeof(buf), "[%zu]", refs);
                text.AddText(font, buf,
                             dividerX - 30.0f,
                             item.rect.y + kTextBaseY, palette.textDim);
            }
            ++m_rowIndex;
            break;
        }
        case VfxParamItemType::NameField: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorField);
            std::string l = "Name: ";
            l += item.name;
            text.AddText(font, l, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, kColorText);
            break;
        }
        case VfxParamItemType::ScopeDropdown: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorField);
            std::string l = "Scope: ";
            l += ScopeLabel(item.scope);
            text.AddText(font, l, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, kColorText);
            break;
        }
        case VfxParamItemType::TypeDropdown: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorField);
            std::string l = "Type: ";
            l += VfxParamTypeName(GetType(item.scope, item.name));
            text.AddText(font, l, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, kColorText);
            break;
        }
        case VfxParamItemType::ValueField: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, kColorField);
            const Vec4 v = m_store->GetVec4(item.scope, item.name, Vec4{});
            static const char* kComp[] = { "X", "Y", "Z", "W" };
            const size_t i = (item.index < 4u) ? item.index : 0u;
            char buf[64];
            float comp = 0.0f;
            switch (i) {
                case 0: comp = v.x; break;
                case 1: comp = v.y; break;
                case 2: comp = v.z; break;
                case 3: comp = v.w; break;
                default: break;
            }
            std::snprintf(buf, sizeof(buf), "%s: %.3f", kComp[i], comp);
            text.AddText(font, buf, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, kColorText);
            break;
        }
        case VfxParamItemType::JumpToUsages: {
            const bool any = CountReferences(item.name) > 0u;
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height,
                         any ? kColorJumpBtn : kColorDisabled);
            text.AddText(font, any ? "Jump to usages" : "No usages",
                         item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY,
                         any ? kColorText : kColorTextDim);
            break;
        }
        case VfxParamItemType::DeleteButton: {
            const bool enabled = CountReferences(item.name) == 0u;
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height,
                         enabled ? kColorDeleteBtn : kColorDisabled);
            text.AddText(font, enabled ? "Delete" : "Delete (in use)",
                         item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY,
                         enabled ? kColorText : kColorTextDim);
            break;
        }
        // ── Phase 12.4.0 additions ────────────────────────────────────
        case VfxParamItemType::Section: {
            const bool expanded = IsSectionExpanded(item.name);
            const auto& palette = VfxThemeColors();
            // Phase 14.3 UE5 — section header is a flat bar (no
            // rounded capsule). Chevron SPRITE at `rect.x + 6 px`
            // followed by the uppercase section name.
            text.AddRect(item.rect.x, item.rect.y,
                         item.rect.width, item.rect.height,
                         palette.barBg);
            // Bottom divider.
            text.AddRect(item.rect.x, item.rect.y + item.rect.height - 1.0f,
                         item.rect.width, 1.0f, palette.borderSubtle);
            // Chevron sprite (10x10) centred vertically at left edge.
            const float chevSize = 10.0f;
            text.AddSprite(
                expanded ? VfxUiIcon::ChevronDown : VfxUiIcon::ChevronRight,
                item.rect.x + 6.0f,
                item.rect.y + (item.rect.height - chevSize) * 0.5f,
                chevSize, chevSize, palette.textSecondary);
            // Label uppercase (UE5 convention).
            std::string upper = item.name;
            for (auto& c : upper) {
                if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
            }
            text.AddText(font, upper, item.rect.x + 22.0f,
                         item.rect.y + kTextBaseY,
                         palette.textSecondary);
            break;
        }
        case VfxParamItemType::EditorFloat:
        case VfxParamItemType::EditorVec3:
        case VfxParamItemType::EditorColor:
        case VfxParamItemType::EditorBool:
        case VfxParamItemType::EditorEnum:
        case VfxParamItemType::EditorCurve:
        case VfxParamItemType::EditorRamp:
        case VfxParamItemType::EditorString: {
            const auto& palette = VfxThemeColors();

            // Phase 13.5 — editor background + optional out-of-range
            // border in a single rounded-rect SDF call. Radius 2 px
            // keeps editors visually tight while still reading as
            // distinct from flat panel chrome.
            const Vec4 v = m_store->GetVec4(item.scope, item.name, Vec4{});
            const bool outOfRange =
                (item.type == VfxParamItemType::EditorFloat) &&
                !IsValueInRange(item.scope, item.name, v.x);
            if (outOfRange) {
                text.AddRoundedRect(item.rect.x, item.rect.y,
                                     item.rect.width, item.rect.height,
                                     2.0f /*radius.xs*/,
                                     palette.inputBg,
                                     1.0f, palette.stateError);
            } else {
                text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                             item.rect.height, palette.inputBg);
            }

            // Value preview text.
            char buf[96];
            switch (item.type) {
                case VfxParamItemType::EditorFloat:
                    std::snprintf(buf, sizeof(buf), "%.3f", v.x);
                    break;
                case VfxParamItemType::EditorVec3:
                    std::snprintf(buf, sizeof(buf), "%.2f, %.2f, %.2f",
                                   v.x, v.y, v.z);
                    break;
                case VfxParamItemType::EditorColor:
                    std::snprintf(buf, sizeof(buf), "RGBA %.2f %.2f %.2f %.2f",
                                   v.x, v.y, v.z, v.w);
                    break;
                case VfxParamItemType::EditorBool:
                    std::snprintf(buf, sizeof(buf), "%s",
                                   v.x != 0.0f ? "true" : "false");
                    break;
                default:
                    std::snprintf(buf, sizeof(buf), "-");
                    break;
            }
            text.AddText(font, buf, item.rect.x + 6.0f,
                         item.rect.y + kTextBaseY, palette.textPrimary);

            // Color swatch chip at the right edge for EditorColor.
            if (item.type == VfxParamItemType::EditorColor) {
                const float sw = 16.0f;
                text.AddRect(item.rect.x + item.rect.width - sw - 4.0f,
                             item.rect.y + 2.0f, sw,
                             item.rect.height - 4.0f,
                             Vec4{v.x, v.y, v.z, 1.0f});
            }
            break;
        }
        case VfxParamItemType::ResetToDefault: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, VfxThemeColors().cardBg);
            text.AddText(font, "()",
                         item.rect.x + 2.0f,
                         item.rect.y + kTextBaseY,
                         VfxThemeColors().textSecondary);
            break;
        }
        case VfxParamItemType::BindToCurve: {
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, VfxThemeColors().cardBg);
            text.AddText(font, "~",
                         item.rect.x + 3.0f,
                         item.rect.y + kTextBaseY,
                         VfxThemeColors().accentPrimary);
            break;
        }
        case VfxParamItemType::None:
            break;
        }
    }
}

} // namespace uz
