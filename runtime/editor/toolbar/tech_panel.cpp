/// TechPanel — runtime lighting-technology switchboard (see tech_panel.h).
// build-cache: layout constants live in the header; bump this line when they change.

#include "tech_panel.h"

#include "cvar.h"
#include "debug_view_mode.h"
#include "editor/font/system_font.h"
#include "input.h"
#include "log.h"
#include "scene_renderer.h"
#include "shader_pipeline.h"
#include "upload_context.h"

#include <cstdio>
#include <string>

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool TechPanel::Init(IRHIDevice* device, IRHICommandList* cmdList,
                     UploadContext& upload, ShaderPipeline& shaderPipeline,
                     RHIFormat rtvFormat,
                     uint32_t screenWidth, uint32_t screenHeight,
                     bool bindless) {
    m_device       = device;
    m_screenWidth  = screenWidth;
    m_screenHeight = screenHeight;

    const std::string fontPath = ResolveDefaultMonoFont();
    if (fontPath.empty() || !m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(Renderer, Warning, "TechPanel: font load failed (resolved='{}'), panel disabled", fontPath);
        return false;
    }
    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat, screenWidth, screenHeight, bindless)) {
        UZ_LOG(Renderer, Warning, "TechPanel: TextRenderer init failed");
        m_font.Shutdown(device);
        return false;
    }

    m_initialized = true;
    BuildRows();
    LayoutRows();
    UZ_LOG(Renderer, Info, "TechPanel initialized ({} rows, {}x{}) — F3 or r.TechPanel.Show",
           m_rows.size(), screenWidth, screenHeight);
    return true;
}

void TechPanel::Shutdown() {
    if (!m_initialized) return;
    m_textRenderer.Shutdown();
    m_font.Shutdown(m_device);
    m_rows.clear();
    m_initialized = false;
}

void TechPanel::SetScreenSize(uint32_t width, uint32_t height) {
    m_screenWidth  = width;
    m_screenHeight = height;
    m_textRenderer.SetScreenSize(width, height);
    LayoutRows();
}

// ─────────────────────────────────────────────────────────────────────────────
// Rows — one CVar each. Only CVars a pass reads every frame belong here:
// `ApplyCVars()` and `SyncShowFlagsFromCVars()` run per frame, so a click acts
// on the next frame and the row always reflects the CVar, whoever set it.
// ─────────────────────────────────────────────────────────────────────────────

void TechPanel::BuildRows() {
    m_rows.clear();
    auto header = [&](const char* text) {
        Row r; r.kind = RowKind::Header; r.label = text; m_rows.push_back(r);
    };
    auto toggle = [&](const char* label, const char* cvar, const char* needs = "") {
        Row r; r.kind = RowKind::Toggle; r.label = label; r.cvar = cvar; r.needs = needs;
        m_rows.push_back(r);
    };
    auto scalar = [&](const char* label, const char* cvar) {
        Row r; r.kind = RowKind::Scalar; r.label = label; r.cvar = cvar; r.saved = 1.f;
        m_rows.push_back(r);
    };
    auto view = [&](const char* label, DebugViewMode mode) {
        Row r; r.kind = RowKind::ViewMode; r.label = label; r.viewMode = static_cast<int>(mode);
        m_rows.push_back(r);
    };

    header("GI / indirect  (consumer: DDGI > radiance cache > sky SH)");
    toggle("DXR master switch",              "r.RayTracing");
    toggle("DDGI probe atlas (DXR)",         "r.RayTracing.GI",          "r.RayTracing");
    toggle("DDGI directional sky",           "r.GI.DDGI.DirectionalSky", "r.RayTracing.GI");
    toggle("Radiance cache (SDF probes)",    "r.RadianceCache");
    toggle("Probe sides: back-face rays",    "r.GI.RadianceCache.ProbeClassification", "r.RadianceCache");
    toggle("Final gather (SDF diffuse rays)","r.GI.FinalGather",         "r.RadianceCache");
    toggle("SDF reflections",                "r.GI.Reflections",         "r.RadianceCache");
    toggle("SSGI (screen space)",            "r.SSGI");
    scalar("GI intensity",                   "r.GI.Intensity");

    header("Sky and direct light");
    scalar("Sky light",                      "r.SkyLight.Intensity");
    toggle("Atmosphere (sky)",               "r.Show.Atmosphere");
    toggle("Shadow maps (CSM + local)",      "r.Show.Shadows");
    toggle("Contact shadows",                "r.ContactShadows");
    toggle("GTAO",                           "r.GTAO");
    toggle("Height fog",                     "r.Fog");
    toggle("Volumetric fog",                 "r.VolumetricFog");
    toggle("Clouds",                         "r.Clouds");
    toggle("Aerial perspective",             "r.AerialPerspective");

    header("Materials (G-Buffer inputs)");
    toggle("Normal maps",                    "r.Material.NormalMap");
    toggle("AO maps (material / ORM)",       "r.Material.AOMap");

    header("Reflections, translucency, decals");
    toggle("SSR",                            "r.SSR");
    toggle("Translucent pass",               "r.Show.Translucency");
    toggle("Translucent depth test",         "r.Translucency.DepthTest", "r.Show.Translucency");
    toggle("Decals",                         "r.Show.Decals");

    header("Post-processing");
    toggle("TAA",                            "r.TAA");
    toggle("Auto exposure",                  "r.AutoExposure");
    toggle("Bloom",                          "r.Bloom");
    toggle("Motion blur",                    "r.MotionBlur");
    toggle("CAS sharpen",                    "r.CAS");

    header("View (debug view mode)");
    view("Lit (final)",                      DebugViewMode::Lit);
    view("Direct light only (Lo)",           DebugViewMode::Lighting);
    view("Direct + indirect, no albedo",     DebugViewMode::LightingOnly);
    view("Shadow factor",                    DebugViewMode::Shadows);
    view("AO",                               DebugViewMode::AO);
    view("Albedo",                           DebugViewMode::Albedo);
    view("World normal",                     DebugViewMode::WorldNormal);
}

void TechPanel::LayoutRows() {
    m_panelX = static_cast<float>(m_screenWidth) - kRightMargin - kPanelW;
    if (m_panelX < 0.f) m_panelX = 0.f;
    m_panelY = kTopMargin;

    float y = m_panelY + kPad;
    for (auto& r : m_rows) {
        const float h = (r.kind == RowKind::Header) ? kHeaderH : kRowH;
        r.x = m_panelX + kPad;
        r.y = y;
        r.w = kPanelW - 2.f * kPad;
        r.h = h;
        y += h;
    }
    m_panelH = (y - m_panelY) + kRowH + kPad; // + footer line
}

// ─────────────────────────────────────────────────────────────────────────────
// Input
// ─────────────────────────────────────────────────────────────────────────────

float TechPanel::ReadCVar(const std::string& name, float fallback) {
    const CVar* cv = CVarRegistry::Get().Find(name);
    return cv ? cv->value : fallback;
}

void TechPanel::ApplyClick(Row& row, SceneRenderer* renderer) {
    auto& reg = CVarRegistry::Get();
    switch (row.kind) {
    case RowKind::Toggle: {
        const bool on = ReadCVar(row.cvar, 0.f) >= 0.5f;
        reg.Set(row.cvar, on ? 0.f : 1.f);
        UZ_LOG(Renderer, Info, "TechPanel: {} = {}", row.cvar, on ? 0 : 1);
        break;
    }
    case RowKind::Scalar: {
        const float cur = ReadCVar(row.cvar, 1.f);
        if (cur > 0.f) {
            row.saved = cur;
            reg.Set(row.cvar, 0.f);
        } else {
            reg.Set(row.cvar, row.saved > 0.f ? row.saved : 1.f);
        }
        UZ_LOG(Renderer, Info, "TechPanel: {} = {}", row.cvar, ReadCVar(row.cvar, 0.f));
        break;
    }
    case RowKind::ViewMode:
        if (renderer) {
            renderer->SetDebugViewMode(static_cast<DebugViewMode>(row.viewMode));
            UZ_LOG(Renderer, Info, "TechPanel: view mode {}", DebugViewModeName(static_cast<DebugViewMode>(row.viewMode)));
        }
        break;
    case RowKind::Header:
        break;
    }
}

bool TechPanel::Update(InputSystem* input, SceneRenderer* renderer) {
    if (!m_initialized) return false;
    auto& reg = CVarRegistry::Get();

    if (input && input->IsKeyJustPressed(Key::F3)) {
        const bool shown = reg.GetFloat("r.TechPanel.Show", 0.f) >= 0.5f;
        reg.Set("r.TechPanel.Show", shown ? 0.f : 1.f);
        UZ_LOG(Renderer, Info, "TechPanel: r.TechPanel.Show = {} (F3)", shown ? 0 : 1);
    }
    m_visible = reg.GetFloat("r.TechPanel.Show", 0.f) >= 0.5f;
    m_hoveredRow = -1;
    if (renderer) m_currentViewMode = static_cast<int>(renderer->GetDebugViewMode());
    if (!input) return false;

    const float mx = input->GetMouseX();
    const float my = input->GetMouseY();
    const bool inside = mx >= m_panelX && mx <= m_panelX + kPanelW
                     && my >= m_panelY && my <= m_panelY + m_panelH;

    if (!m_visible || !inside) return false;

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        if (r.kind == RowKind::Header) continue;
        if (mx >= r.x && mx <= r.x + r.w && my >= r.y && my <= r.y + r.h) {
            m_hoveredRow = i;
            break;
        }
    }
    if (m_hoveredRow >= 0 && input->IsMouseJustPressed(MouseButton::Left)) {
        ApplyClick(m_rows[m_hoveredRow], renderer);
    }
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

std::string TechPanel::StateText(const Row& row, SceneRenderer* renderer) const {
    (void)renderer;
    char buf[32];
    switch (row.kind) {
    case RowKind::Toggle:
        return ReadCVar(row.cvar, 0.f) >= 0.5f ? "[x]" : "[ ]";
    case RowKind::Scalar: {
        const float v = ReadCVar(row.cvar, 0.f);
        std::snprintf(buf, sizeof(buf), "%s %.2f", v > 0.f ? "[x]" : "[ ]", v);
        return buf;
    }
    default:
        return "";
    }
}

void TechPanel::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing, uint32_t frameIndex) {
    if (!m_initialized || !m_visible) return;

    m_textRenderer.BeginFrame(frameIndex);
    m_textRenderer.AddRect(m_panelX, m_panelY, kPanelW, m_panelH, kColorPanel);

    const float charW = m_font.GetPixelHeight() * 0.6f;
    const float textDy = (kRowH - kFontSize) * 0.5f;

    for (int i = 0; i < static_cast<int>(m_rows.size()); ++i) {
        const Row& r = m_rows[i];
        if (r.kind == RowKind::Header) {
            m_textRenderer.AddText(m_font, r.label, r.x, r.y + (kHeaderH - kFontSize) * 0.5f + 2.f, kColorHeader);
            continue;
        }

        if (i == m_hoveredRow) {
            m_textRenderer.AddRect(r.x, r.y, r.w, r.h, kColorRowHover);
        }

        if (r.kind == RowKind::ViewMode) {
            const bool selected = m_currentViewMode == r.viewMode;
            if (selected) m_textRenderer.AddRect(r.x, r.y, r.w, r.h, kColorSelected);
            m_textRenderer.AddText(m_font, std::string(selected ? "(*) " : "( ) ") + r.label,
                                   r.x + 4.f, r.y + textDy, kColorText);
            continue;
        }

        // State token, coloured by state; then the label; then the CVar name, dimmed.
        const std::string state = StateText(r, nullptr);
        const bool on = state.rfind("[x]", 0) == 0;
        m_textRenderer.AddText(m_font, state, r.x + 4.f, r.y + textDy, on ? kColorOn : kColorOff);

        const float labelX = r.x + 4.f + (static_cast<float>(state.size()) + 1.f) * charW;
        bool requirementMet = true;
        if (!r.needs.empty()) requirementMet = ReadCVar(r.needs, 0.f) >= 0.5f;
        m_textRenderer.AddText(m_font, r.label, labelX, r.y + textDy, requirementMet ? kColorText : kColorDim);

        if (!requirementMet) {
            m_textRenderer.AddText(m_font, "needs " + r.needs, r.x + kCvarColumn, r.y + textDy, kColorWarn);
        } else {
            m_textRenderer.AddText(m_font, r.cvar, r.x + kCvarColumn, r.y + textDy, kColorDim);
        }
    }

    // Footer: which indirect-diffuse source the consumer will take, by CVars.
    const bool ddgi = ReadCVar("r.RayTracing", 0.f) >= 0.5f && ReadCVar("r.RayTracing.GI", 0.f) >= 0.5f;
    const bool rc   = ReadCVar("r.RadianceCache", 0.f) >= 0.5f;
    const char* src = ddgi ? "DDGI" : (rc ? "radiance cache" : "sky SH");
    const float footY = m_panelY + m_panelH - kRowH - kPad * 0.5f;
    m_textRenderer.AddText(m_font, std::string("indirect source by CVars: ") + src + "   F3 hides",
                           m_panelX + kPad + 4.f, footY + textDy, kColorDim);

    m_textRenderer.Render(cmdList, cbRing, m_font);
}

} // namespace uz
