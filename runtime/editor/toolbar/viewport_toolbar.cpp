/// ViewportToolbar — In-engine editor toolbar overlay implementation.
///
/// Renders a fixed-height button strip at the top of the viewport using the
/// BitmapFont + TextRenderer system shared with StatsOverlay.

#include "viewport_toolbar.h"

#include "cvar.h"
#include "editor/font/system_font.h"
#include "editor_input_handler.h"
#include "input.h"
#include "log.h"
#include "quality_presets.h"
#include "scene_renderer.h"
#include "show_flags.h"
#include "shader_pipeline.h"
#include "upload_context.h"

#include <algorithm>
#include <cstring>
#include <string>

UZ_DECLARE_LOG_CATEGORY(Toolbar);
UZ_DEFINE_LOG_CATEGORY(Toolbar, Info);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// Init / Shutdown
// ─────────────────────────────────────────────────────────────────────────────

bool ViewportToolbar::Init(IRHIDevice* device, IRHICommandList* cmdList,
                            UploadContext& upload, ShaderPipeline& shaderPipeline,
                            RHIFormat rtvFormat,
                            uint32_t screenWidth, uint32_t screenHeight,
                            bool bindless) {
    m_device = device;
    m_screenWidth  = screenWidth;
    m_screenHeight = screenHeight;

    // ── Load font (platform-resolved monospace, same as StatsOverlay) ────
    const std::string fontPath = ResolveDefaultMonoFont();
    if (fontPath.empty() ||
        !m_font.Init(device, cmdList, upload, fontPath, kFontSize)) {
        UZ_LOG(Toolbar, Warning,
               "ViewportToolbar: font load failed (resolved='{}'), toolbar disabled",
               fontPath);
        return false;
    }

    // ── Init text renderer ───────────────────────────────────────────────
    if (!m_textRenderer.Init(device, shaderPipeline, rtvFormat,
                              screenWidth, screenHeight, bindless)) {
        UZ_LOG(Toolbar, Warning, "ViewportToolbar: TextRenderer init failed");
        m_font.Shutdown(device);
        return false;
    }

    m_initialized = true;
    RebuildLayout();
    UZ_LOG(Toolbar, Info, "ViewportToolbar initialized ({}x{})", screenWidth, screenHeight);
    return true;
}

void ViewportToolbar::Shutdown() {
    if (!m_initialized) return;
    m_textRenderer.Shutdown();
    if (m_device) m_font.Shutdown(m_device);
    m_initialized = false;
    m_device = nullptr;
}

// ─────────────────────────────────────────────────────────────────────────────
// SetScreenSize
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::SetScreenSize(uint32_t width, uint32_t height) {
    m_screenWidth  = width;
    m_screenHeight = height;
    m_textRenderer.SetScreenSize(width, height);
    RebuildLayout();
}

// ─────────────────────────────────────────────────────────────────────────────
// RebuildLayout — builds the flat button list from current profile
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::RebuildLayout() {
    m_buttons.clear();

    // Approximate monospace char width from pixel height (Consolas ~0.6 ratio).
    const float charW  = m_font.GetPixelHeight() * 0.6f;
    const float y0     = kBtnTop;
    float       curX   = 6.f;
    int         group  = 0;

    auto addBtn = [&](std::string_view label, int actionId, bool enabled = true) {
        Button b;
        b.label    = std::string(label);
        b.w        = static_cast<float>(label.size()) * charW + kBtnPad * 2.f;
        b.h        = kBtnH;
        b.x        = curX;
        b.y        = y0;
        b.groupId  = group;
        b.actionId = actionId;
        b.enabled  = enabled;
        b.active   = false;
        m_buttons.push_back(b);
        curX += b.w + 2.f; // 2px gap between buttons in a group
    };

    auto nextGroup = [&]() {
        curX += kSepGap;
        ++group;
    };

    const bool isFull    = (m_state.profile == ToolbarProfile::Full);
    const bool isMesh    = (m_state.profile == ToolbarProfile::MeshPreview);

    // ── Group 0 — Transform tools (Full profile only) ────────────────────
    if (isFull) {
        addBtn("Q Select",    kActSelect);
        addBtn("W Translate", kActTranslate);
        addBtn("E Rotate",    kActRotate);
        addBtn("R Scale",     kActScale);
        nextGroup();
    }

    // ── Group 1 — View mode (all profiles) ───────────────────────────────
    addBtn("Lit",           kActViewBase + static_cast<int>(DebugViewMode::Lit));
    addBtn("Unlit",         kActViewBase + static_cast<int>(DebugViewMode::Unlit));
    addBtn("Wireframe",     kActViewBase + static_cast<int>(DebugViewMode::Wireframe));
    if (isFull || isMesh) {
        addBtn("LitWire",   kActViewBase + static_cast<int>(DebugViewMode::LitWireframe));
    }
    if (isFull) {
        addBtn("Detail",    kActViewBase + static_cast<int>(DebugViewMode::DetailLighting));
        addBtn("Lighting",  kActViewBase + static_cast<int>(DebugViewMode::LightingOnly));
        addBtn("Reflections", kActViewBase + static_cast<int>(DebugViewMode::Reflections));
    }
    nextGroup();

    // ── Group 2 — Show flags (Full profile only) ─────────────────────────
    if (isFull) {
        addBtn("Grid",    kActShowGrid);
        addBtn("Fog",     kActShowFog);
        addBtn("AO",      kActShowAO);
        addBtn("Shadows", kActShowShadows);
        addBtn("Bloom",   kActShowBloom);
        addBtn("SSR",     kActShowSSR);
        nextGroup();
    }

    // ── Group 3 — Quality (Full + MeshPreview) ───────────────────────────
    if (isFull || isMesh) {
        addBtn("Low",   kActQualLow);
        addBtn("Med",   kActQualMed);
        addBtn("High",  kActQualHigh);
        addBtn("Ultra", kActQualUltra);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// SyncActiveStates
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::SyncActiveStates() {
    ShowFlags sf{};
    sf.bits = m_state.showFlags;
    for (auto& b : m_buttons) {
        b.active = false;
        const int id = b.actionId;
        if      (id == kActSelect)    b.active = (m_state.transformMode == GizmoMode::Select);
        else if (id == kActTranslate) b.active = (m_state.transformMode == GizmoMode::Translate);
        else if (id == kActRotate)    b.active = (m_state.transformMode == GizmoMode::Rotate);
        else if (id == kActScale)     b.active = (m_state.transformMode == GizmoMode::Scale);
        else if (id >= kActViewBase && id < kActViewBase + 256) {
            const auto vm = static_cast<DebugViewMode>(id - kActViewBase);
            b.active = (m_state.viewMode == vm);
        }
        else if (id == kActShowGrid)    b.active = sf.Get(ShowFlag::Grid);
        else if (id == kActShowFog)     b.active = sf.Get(ShowFlag::Fog);
        else if (id == kActShowAO)      b.active = sf.Get(ShowFlag::AO);
        else if (id == kActShowShadows) b.active = sf.Get(ShowFlag::Shadows);
        else if (id == kActShowBloom)   b.active = sf.Get(ShowFlag::Bloom);
        else if (id == kActShowSSR)     b.active = sf.Get(ShowFlag::SSR);
        else if (id == kActQualLow)     b.active = (m_state.qualityLevel == 0);
        else if (id == kActQualMed)     b.active = (m_state.qualityLevel == 1);
        else if (id == kActQualHigh)    b.active = (m_state.qualityLevel == 2);
        else if (id == kActQualUltra)   b.active = (m_state.qualityLevel == 3);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// ExecuteAction
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::ExecuteAction(int actionId, EditorInputHandler* handler,
                                     SceneRenderer* renderer) {
    if (!handler || !renderer) return;

    if (actionId == kActSelect) {
        handler->SetGizmoMode(GizmoMode::Select);
        UZ_LOG(Toolbar, Trace, "Toolbar: Select");
    } else if (actionId == kActTranslate) {
        handler->SetGizmoMode(GizmoMode::Translate);
        UZ_LOG(Toolbar, Trace, "Toolbar: Translate");
    } else if (actionId == kActRotate) {
        handler->SetGizmoMode(GizmoMode::Rotate);
        UZ_LOG(Toolbar, Trace, "Toolbar: Rotate");
    } else if (actionId == kActScale) {
        handler->SetGizmoMode(GizmoMode::Scale);
        UZ_LOG(Toolbar, Trace, "Toolbar: Scale");
    } else if (actionId >= kActViewBase && actionId < kActViewBase + 256) {
        const auto vm = static_cast<DebugViewMode>(actionId - kActViewBase);
        renderer->SetDebugViewMode(vm);
        UZ_LOG(Toolbar, Trace, "Toolbar: view mode {}", static_cast<int>(vm));
    } else if (actionId == kActShowGrid) {
        renderer->SetShowFlag(ShowFlag::Grid,    !renderer->GetShowFlag(ShowFlag::Grid));
    } else if (actionId == kActShowFog) {
        renderer->SetShowFlag(ShowFlag::Fog,     !renderer->GetShowFlag(ShowFlag::Fog));
    } else if (actionId == kActShowAO) {
        renderer->SetShowFlag(ShowFlag::AO,      !renderer->GetShowFlag(ShowFlag::AO));
    } else if (actionId == kActShowShadows) {
        renderer->SetShowFlag(ShowFlag::Shadows, !renderer->GetShowFlag(ShowFlag::Shadows));
    } else if (actionId == kActShowBloom) {
        renderer->SetShowFlag(ShowFlag::Bloom,   !renderer->GetShowFlag(ShowFlag::Bloom));
    } else if (actionId == kActShowSSR) {
        renderer->SetShowFlag(ShowFlag::SSR,     !renderer->GetShowFlag(ShowFlag::SSR));
    } else if (actionId >= kActQualLow && actionId <= kActQualUltra) {
        const auto level = static_cast<QualityLevel>(actionId - kActQualLow);
        QualityPresetManager::Get().ApplyPreset(level);
        UZ_LOG(Toolbar, Trace, "Toolbar: quality {}", actionId - kActQualLow);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Update
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::Update(InputSystem* input, EditorInputHandler* handler,
                              SceneRenderer* renderer) {
    if (!m_initialized || !m_visible) return;

    // ── Check r.Toolbar.Show CVar ────────────────────────────────────────
    m_visible = (CVarRegistry::Get().GetFloat("r.Toolbar.Show", 1.f) >= 0.5f);
    if (!m_visible) return;

    // ── r.Toolbar.Profile CVar — rebuild layout if profile changed ────────
    {
        const auto newProfile = static_cast<ToolbarProfile>(
            CVarRegistry::Get().GetInt("r.Toolbar.Profile", 0));
        if (newProfile != m_state.profile) {
            m_state.profile = newProfile;
            RebuildLayout();
        }
    }

    // ── Sync state from engine systems ───────────────────────────────────
    if (handler) {
        m_state.transformMode = handler->GetGizmoMode();
        m_state.gizmoSpace    = handler->GetGizmoSpace();
    }
    if (renderer) {
        m_state.viewMode     = renderer->GetDebugViewMode();
        m_state.showFlags    = renderer->GetShowFlags().bits;
        m_state.qualityLevel = static_cast<uint8_t>(
            QualityPresetManager::Get().GetOverallLevel());
    }

    SyncActiveStates();

    if (!input) return;

    // ── Mouse hover and click detection ──────────────────────────────────
    const float mx = input->GetMouseX();
    const float my = input->GetMouseY();

    m_hoveredIdx = -1;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        const auto& b = m_buttons[i];
        if (!b.enabled) continue;
        if (mx >= b.x && mx <= b.x + b.w &&
            my >= b.y && my <= b.y + b.h) {
            m_hoveredIdx = i;
            break;
        }
    }

    // LMB click — fire action for hovered button.
    if (m_hoveredIdx >= 0 && input->IsMouseJustPressed(MouseButton::Left)) {
        ExecuteAction(m_buttons[m_hoveredIdx].actionId, handler, renderer);
        // Re-sync immediately so next Render() shows updated state.
        if (handler) {
            m_state.transformMode = handler->GetGizmoMode();
            m_state.gizmoSpace    = handler->GetGizmoSpace();
        }
        if (renderer) {
            m_state.viewMode     = renderer->GetDebugViewMode();
            m_state.showFlags    = renderer->GetShowFlags().bits;
            m_state.qualityLevel = static_cast<uint8_t>(
                QualityPresetManager::Get().GetOverallLevel());
        }
        SyncActiveStates();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void ViewportToolbar::Render(IRHICommandList* cmdList, CBRingAllocator& cbRing,
                              uint32_t frameIndex) {
    if (!m_initialized || !m_visible) return;

    m_textRenderer.BeginFrame(frameIndex);

    // ── Background strip ─────────────────────────────────────────────────
    m_textRenderer.AddRect(0.f, 0.f,
                            static_cast<float>(m_screenWidth), kBarHeight,
                            kColorBar);

    // ── Draw buttons ──────────────────────────────────────────────────────
    int prevGroup = -1;
    for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
        const auto& b = m_buttons[i];

        // Group separator line.
        if (b.groupId != prevGroup && prevGroup >= 0) {
            const float sepX = b.x - kSepGap * 0.5f - 0.5f;
            m_textRenderer.AddRect(sepX, 4.f, 1.f, kBarHeight - 8.f, kColorSep);
        }
        prevGroup = b.groupId;

        // Button background.
        Vec4 bgColor = kColorBtn;
        if (b.active) {
            bgColor = kColorBtnActive;
        } else if (i == m_hoveredIdx) {
            bgColor = kColorBtnHover;
        }
        m_textRenderer.AddRect(b.x, b.y, b.w, b.h, bgColor);

        // Button label.
        const Vec4 textColor = b.enabled ? kColorText : kColorTextDim;
        const float textX = b.x + kBtnPad;
        const float textY = b.y + (kBtnH - kFontSize) * 0.5f;
        m_textRenderer.AddText(m_font, b.label, textX, textY, textColor);
    }

    m_textRenderer.Render(cmdList, cbRing, m_font);
}

} // namespace uz
