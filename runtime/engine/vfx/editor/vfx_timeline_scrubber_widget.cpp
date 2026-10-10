/// VfxTimelineScrubberWidget — implementation.
///
/// Phase 11.6.0. See `vfx_timeline_scrubber_widget.h` for the contract.
/// Widget delegates play / pause / step / scrub / play-rate to the
/// bound `VfxPreviewClock`. Loop + restart are widget-local; host wires
/// them to the `VfxSystemInstance` restart pipeline via callbacks.

#include "vfx_timeline_scrubber_widget.h"

#include "editor/font/text_renderer.h"
#include "editor/font/bitmap_font.h"
#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_editor_theme.h"
#include "engine/vfx/editor/vfx_icon_atlas.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace uz {

namespace {

// ─── Palette (phase 12.1.0 — theme-sourced) ─────────────────────────────────

const Vec4& kColorStrip       = VfxThemeColors().barBg;
const Vec4& kColorBtnBg       = VfxThemeColors().cardBg;
const Vec4& kColorBtnBgActive = VfxThemeColors().accentPrimary;
const Vec4& kColorTrackBg     = VfxThemeColors().inputBg;
const Vec4& kColorTrackFill   = VfxThemeColors().accentMuted;
const Vec4& kColorHandle      = VfxThemeColors().textPrimary;
const Vec4& kColorText        = VfxThemeColors().textPrimary;

/// Fixed step used by the `<` / `>` buttons (matches the engine's
/// default fixed-timestep accumulator).
constexpr float kStepDt = 1.0f / 60.0f;

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Clock passthrough
// ─────────────────────────────────────────────────────────────────────────────

bool VfxTimelineScrubberWidget::IsPlaying() const noexcept {
    return (m_clock != nullptr) && m_clock->IsPlaying();
}

void VfxTimelineScrubberWidget::TogglePlayPause() noexcept {
    if (m_clock == nullptr) return;
    if (m_clock->IsPlaying()) {
        m_clock->Pause();
    } else {
        m_clock->Play();
    }
}

void VfxTimelineScrubberWidget::StepBackward() noexcept {
    if (m_clock == nullptr) return;
    // StepFrame only advances forward — step back by seeking.
    const float prev = m_clock->GetCurrentTime();
    const float target = std::max(0.0f, prev - kStepDt);
    m_clock->Scrub(target);
}

void VfxTimelineScrubberWidget::StepForward() noexcept {
    if (m_clock == nullptr) return;
    m_clock->StepFrame(kStepDt);
}

void VfxTimelineScrubberWidget::SkipToStart() noexcept {
    if (m_clock == nullptr) return;
    m_clock->Scrub(0.0f);
}

void VfxTimelineScrubberWidget::SkipToEnd() noexcept {
    if (m_clock == nullptr) return;
    m_clock->Scrub(Duration());
}

// ─────────────────────────────────────────────────────────────────────────────
// Speed ring
// ─────────────────────────────────────────────────────────────────────────────

size_t VfxTimelineScrubberWidget::ClosestSpeedIndex() const noexcept {
    if (m_clock == nullptr) return 2u;  // default to 1.0x

    const float rate = m_clock->GetPlayRate();
    size_t best = 0;
    float  bestDist = std::fabs(kVfxTimelineSpeeds[0] - rate);
    for (size_t i = 1; i < kVfxTimelineSpeeds.size(); ++i) {
        const float d = std::fabs(kVfxTimelineSpeeds[i] - rate);
        // Tie-break towards 1.0x (index 2).
        if (d < bestDist ||
            (d == bestDist && kVfxTimelineSpeeds[i] == 1.0f)) {
            best = i;
            bestDist = d;
        }
    }
    return best;
}

float VfxTimelineScrubberWidget::Speed() const noexcept {
    if (m_clock == nullptr) return 1.0f;
    return kVfxTimelineSpeeds[ClosestSpeedIndex()];
}

void VfxTimelineScrubberWidget::CycleSpeed() noexcept {
    if (m_clock == nullptr) return;
    const size_t curr = ClosestSpeedIndex();
    const size_t next = (curr + 1u) % kVfxTimelineSpeeds.size();
    m_clock->SetPlayRate(kVfxTimelineSpeeds[next]);
}

void VfxTimelineScrubberWidget::SetSpeed(float rate) noexcept {
    if (m_clock == nullptr) return;
    m_clock->SetPlayRate(rate);
    // Snap to the nearest ring entry — keeps the displayed label stable.
    const size_t idx = ClosestSpeedIndex();
    m_clock->SetPlayRate(kVfxTimelineSpeeds[idx]);
}

// ─────────────────────────────────────────────────────────────────────────────
// Loop + restart
// ─────────────────────────────────────────────────────────────────────────────

bool VfxTimelineScrubberWidget::ToggleLoop() noexcept {
    m_loop = !m_loop;
    if (onLoopChanged) onLoopChanged(m_loop);
    return m_loop;
}

void VfxTimelineScrubberWidget::SetLoop(bool enabled) noexcept {
    if (m_loop == enabled) return;
    m_loop = enabled;
    if (onLoopChanged) onLoopChanged(m_loop);
}

void VfxTimelineScrubberWidget::RequestRestart() noexcept {
    if (onRestartRequested) onRestartRequested();
}

// ─────────────────────────────────────────────────────────────────────────────
// Seek
// ─────────────────────────────────────────────────────────────────────────────

float VfxTimelineScrubberWidget::CurrentTime() const noexcept {
    return (m_clock != nullptr) ? m_clock->GetCurrentTime() : 0.0f;
}

float VfxTimelineScrubberWidget::Duration() const noexcept {
    return (m_clock != nullptr) ? m_clock->GetMaxDuration() : 0.0f;
}

void VfxTimelineScrubberWidget::Seek(float seconds) noexcept {
    if (m_clock == nullptr) return;
    const float target = std::clamp(seconds, 0.0f, m_clock->GetMaxDuration());
    m_clock->Scrub(target);
}

void VfxTimelineScrubberWidget::SeekByNormalised(float t) noexcept {
    if (m_clock == nullptr) return;
    const float clamped = std::clamp(t, 0.0f, 1.0f);
    Seek(clamped * m_clock->GetMaxDuration());
}

float VfxTimelineScrubberWidget::NormalisedTime() const noexcept {
    if (m_clock == nullptr) return 0.0f;
    const float d = m_clock->GetMaxDuration();
    if (d <= 0.0f) return 0.0f;
    return std::clamp(m_clock->GetCurrentTime() / d, 0.0f, 1.0f);
}

std::string VfxTimelineScrubberWidget::FormatTimeDisplay() const {
    char buf[64];
    const float cur = CurrentTime();
    const float tot = Duration();
    std::snprintf(buf, sizeof(buf), "%.*fs / %.*fs",
                   timeDecimals, cur, timeDecimals, tot);
    return std::string(buf);
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-loop
// ─────────────────────────────────────────────────────────────────────────────

bool VfxTimelineScrubberWidget::TickAutoLoop() noexcept {
    if (!m_loop || m_clock == nullptr) return false;
    const float d = m_clock->GetMaxDuration();
    if (d <= 0.0f) return false;
    if (m_clock->GetCurrentTime() < d - 1e-6f) return false;

    // Reached / passed the end — rewind + restart.
    m_clock->Reset();
    m_clock->Play();
    if (onRestartRequested) onRestartRequested();
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────

std::vector<VfxTimelineLayoutItem> VfxTimelineScrubberWidget::ComputeLayout(
    const VfxStackRect& panelBounds) const {
    std::vector<VfxTimelineLayoutItem> out;
    if (m_clock == nullptr) return out;

    const float x0 = panelBounds.x;
    const float y0 = panelBounds.y;
    const float W  = panelBounds.width;
    const float H  = stripHeight;
    const float btnY = y0 + (H - buttonHeight) * 0.5f;

    // Strip background
    VfxTimelineLayoutItem strip;
    strip.type = VfxTimelineItemType::Strip;
    strip.rect = { x0, y0, W, H };
    out.push_back(strip);

    float cursor = x0 + buttonGap;

    // Time label
    {
        VfxTimelineLayoutItem it;
        it.type  = VfxTimelineItemType::TimeLabel;
        it.rect  = { cursor, btnY, timeLabelWidth, buttonHeight };
        it.label = FormatTimeDisplay();
        out.push_back(it);
        cursor += timeLabelWidth + buttonGap;
    }

    // Phase 12.6.0 — transport bar in Niagara order:
    // Skip-to-start | Step-back | Play/Pause | Step-fwd | Skip-to-end | Restart | Speed | Loop
    const VfxTimelineItemType kTransport[] = {
        VfxTimelineItemType::SkipStartBtn,
        VfxTimelineItemType::StepBackBtn,
        VfxTimelineItemType::PlayButton,
        VfxTimelineItemType::StepFwdBtn,
        VfxTimelineItemType::SkipEndBtn,
        VfxTimelineItemType::RestartBtn,
        VfxTimelineItemType::SpeedBtn,
        VfxTimelineItemType::LoopBtn,
    };
    constexpr size_t kTransportCount = sizeof(kTransport) / sizeof(kTransport[0]);

    for (size_t i = 0; i < kTransportCount; ++i) {
        VfxTimelineLayoutItem it;
        it.type = kTransport[i];
        // Speed button is wider to host "<rate>x" + chevron.
        const float w = (kTransport[i] == VfxTimelineItemType::SpeedBtn)
            ? buttonWidth * 1.7f
            : buttonWidth;
        it.rect = { cursor, btnY, w, buttonHeight };

        switch (it.type) {
        case VfxTimelineItemType::SkipStartBtn:
            it.label = "|<";
            break;
        case VfxTimelineItemType::StepBackBtn:
            it.label = "<<";
            break;
        case VfxTimelineItemType::PlayButton:
            // Glyph swaps with bound clock state.
            it.label = IsPlaying() ? "||" : ">";
            break;
        case VfxTimelineItemType::StepFwdBtn:
            it.label = ">>";
            break;
        case VfxTimelineItemType::SkipEndBtn:
            it.label = ">|";
            break;
        case VfxTimelineItemType::RestartBtn:
            it.label = "()";
            break;
        case VfxTimelineItemType::SpeedBtn: {
            // Phase 12.6.0 — "1.0x v" dropdown-trigger label.
            char b[24];
            std::snprintf(b, sizeof(b), "%.1fx v", Speed());
            it.label = b;
            break;
        }
        case VfxTimelineItemType::LoopBtn:
            it.label = "O";
            break;
        default:
            break;
        }
        out.push_back(it);
        cursor += w + buttonGap;
    }

    // Seek track: occupies the remainder of the strip (with a small
    // right-edge pad).
    const float trackX = cursor;
    const float trackW = std::max(seekMinWidth,
                                   (x0 + W) - trackX - buttonGap);
    const float trackY = btnY + (buttonHeight - 8.0f) * 0.5f;
    const float trackH = 8.0f;

    {
        VfxTimelineLayoutItem it;
        it.type = VfxTimelineItemType::SeekTrack;
        it.rect = { trackX, trackY, trackW, trackH };
        out.push_back(it);
    }

    // Burst markers (phase 11.8). Each burst becomes a vertical tick on
    // the track. Tick height scales with log2(count) so a burst of 300
    // renders taller than a burst of 30. Only render bursts whose time
    // falls within [0, duration].
    if (m_bursts != nullptr) {
        const float dur = Duration();
        if (dur > 0.0f) {
            for (size_t i = 0; i < m_bursts->size(); ++i) {
                const auto& b = (*m_bursts)[i];
                if (b.time < 0.0f || b.time > dur) continue;
                const float u = std::clamp(b.time / dur, 0.0f, 1.0f);
                const float mx = trackX + u * trackW;

                // Tick height scales with log2(count).
                const float minH = trackH + 2.0f;
                const float maxH = trackH + 14.0f;
                const float lg = (b.count < 2u)
                    ? 0.0f
                    : std::log2f(static_cast<float>(b.count));
                const float mh = std::clamp(
                    minH + (maxH - minH) * (lg / 10.0f),
                    minH, maxH);

                VfxTimelineLayoutItem mk;
                mk.type = VfxTimelineItemType::BurstMarker;
                mk.burstIndex = i;
                mk.rect = {
                    mx - 1.5f,
                    trackY + (trackH - mh) * 0.5f,
                    3.0f,
                    mh
                };
                out.push_back(mk);
            }
        }
    }

    // Handle — positioned along the track by normalised time.
    const float u = NormalisedTime();
    const float handleX = trackX + u * trackW;
    {
        VfxTimelineLayoutItem it;
        it.type = VfxTimelineItemType::SeekHandle;
        it.rect = {
            handleX - 4.0f,
            trackY - 4.0f,
            8.0f,
            trackH + 8.0f
        };
        out.push_back(it);
    }

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// Hit-test
// ─────────────────────────────────────────────────────────────────────────────

VfxTimelineHitResult VfxTimelineScrubberWidget::HitTest(
    const std::vector<VfxTimelineLayoutItem>& layout,
    float x, float y) const {
    VfxTimelineHitResult hit;
    for (const auto& item : layout) {
        if (!item.rect.Contains(x, y)) continue;
        if (static_cast<int>(item.type) > static_cast<int>(hit.type) ||
            hit.type == VfxTimelineItemType::None) {
            hit.type = item.type;
            hit.burstIndex = item.burstIndex;
        }
    }
    return hit;
}

// ─────────────────────────────────────────────────────────────────────────────
// Render
// ─────────────────────────────────────────────────────────────────────────────

void VfxTimelineScrubberWidget::Render(TextRenderer& text,
                                         const BitmapFont& font,
                                         const VfxStackRect& panelBounds) const {
    if (m_clock == nullptr) return;

    const auto& palette = VfxThemeColors();
    const auto layout = ComputeLayout(panelBounds);

    // Helper: center-align a label inside a rect at baseline y + pad.
    auto renderBtn = [&](const VfxTimelineLayoutItem& item,
                         const Vec4& bg, const Vec4& fg) {
        text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                     item.rect.height, bg);
        const float textW = static_cast<float>(item.label.size()) * 7.0f;
        const float cx = item.rect.x +
                         std::max(2.0f, (item.rect.width - textW) * 0.5f);
        const float cy = item.rect.y + (item.rect.height - 14.0f) * 0.5f;
        text.AddText(font, item.label, cx, cy, fg);
    };

    // Phase 13.1.0 — transport icon button: rect background + centered
    // 16x16 sprite glyph from the VFX icon atlas. The sprite replaces
    // the ASCII/Unicode label used by phase-12; callers still populate
    // `item.label` so non-bindless / sprite-less fallback keeps working.
    auto renderIconBtn = [&](const VfxTimelineLayoutItem& item,
                             const Vec4& bg, const Vec4& tint,
                             VfxUiIcon icon) {
        text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                     item.rect.height, bg);
        constexpr float kIconSize = 16.0f;
        const float ix = item.rect.x + (item.rect.width  - kIconSize) * 0.5f;
        const float iy = item.rect.y + (item.rect.height - kIconSize) * 0.5f;
        text.AddSprite(icon, ix, iy, kIconSize, kIconSize, tint);
    };

    for (const auto& item : layout) {
        switch (item.type) {
        case VfxTimelineItemType::Strip:
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.barBg);
            break;

        case VfxTimelineItemType::TimeLabel:
            // Phase 12.6.0 — renders as TimeReadout ("cur / total s"),
            // monospaced via the BitmapFont (single-size).
            text.AddText(font, item.label,
                         item.rect.x + 4.0f,
                         item.rect.y + (item.rect.height - 14.0f) * 0.5f,
                         palette.textPrimary);
            break;

        case VfxTimelineItemType::PlayButton:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          IsPlaying() ? VfxUiIcon::Pause : VfxUiIcon::Play);
            break;

        case VfxTimelineItemType::SkipStartBtn:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          VfxUiIcon::JumpStart);
            break;
        case VfxTimelineItemType::SkipEndBtn:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          VfxUiIcon::JumpEnd);
            break;
        case VfxTimelineItemType::StepBackBtn:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          VfxUiIcon::StepBack);
            break;
        case VfxTimelineItemType::StepFwdBtn:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          VfxUiIcon::StepFwd);
            break;
        case VfxTimelineItemType::RestartBtn:
            renderIconBtn(item, palette.cardBg, palette.textPrimary,
                          VfxUiIcon::Restart);
            break;

        case VfxTimelineItemType::SpeedBtn:
            // Phase 13.5 — rounded "pill" for the speed dropdown
            // trigger. Radius = height/2 gives a fully rounded capsule.
            text.AddRoundedRect(item.rect.x, item.rect.y,
                                 item.rect.width, item.rect.height,
                                 item.rect.height * 0.5f,
                                 palette.cardBgHover);
            {
                const float textW = static_cast<float>(item.label.size()) * 7.0f;
                const float tx = item.rect.x +
                                  std::max(8.0f, (item.rect.width - textW) * 0.5f - 6.0f);
                const float ty = item.rect.y +
                                  (item.rect.height - 14.0f) * 0.5f;
                text.AddText(font, item.label, tx, ty, palette.textPrimary);
                constexpr float kChevronSize = 10.0f;
                const float cx = item.rect.x + item.rect.width - kChevronSize - 4.0f;
                const float cy = item.rect.y +
                                 (item.rect.height - kChevronSize) * 0.5f;
                text.AddSprite(VfxUiIcon::ChevronDown, cx, cy,
                               kChevronSize, kChevronSize, palette.textDim);
            }
            break;

        case VfxTimelineItemType::LoopBtn:
            // Accent when active, dim when not. Spec §"Loop toggle".
            renderIconBtn(item,
                          m_loop ? palette.accentPrimary : palette.cardBg,
                          m_loop ? palette.textOnAccent  : palette.textDim,
                          VfxUiIcon::Loop);
            break;

        case VfxTimelineItemType::SeekTrack: {
            // Phase 12.6.0 — filled played portion in accentMuted,
            // unplayed in inputBg, plus 1-px tick marks.
            const float dur = Duration();
            text.AddRect(item.rect.x, item.rect.y,
                         item.rect.width, item.rect.height,
                         palette.inputBg);
            const float u = NormalisedTime();
            const float fillW = item.rect.width * u;
            if (fillW > 0.0f) {
                text.AddRect(item.rect.x, item.rect.y, fillW,
                             item.rect.height, palette.accentMuted);
            }
            // Major ticks every 0.5s, minor every 0.1s.
            if (dur > 0.0f && item.rect.width > 4.0f) {
                const auto emitTick = [&](float t, float alpha, const Vec4& c) {
                    const float sx = item.rect.x +
                        std::clamp(t / dur, 0.0f, 1.0f) * item.rect.width;
                    const float th = item.rect.height;
                    Vec4 col = c;
                    col.w *= alpha;
                    text.AddRect(sx, item.rect.y, 1.0f, th, col);
                };
                // Minor — step 0.1s.
                for (float tt = 0.1f; tt < dur - 1e-4f; tt += 0.1f) {
                    emitTick(tt, 0.6f, palette.borderSubtle);
                }
                // Major — step 0.5s.
                for (float tt = 0.5f; tt < dur - 1e-4f; tt += 0.5f) {
                    emitTick(tt, 1.0f, palette.textDim);
                }
            }
            break;
        }

        case VfxTimelineItemType::BurstMarker: {
            // Phase 12.6.0 — upward-pointing triangle approximated with
            // 4 stacked rectangles (narrowing from bottom to tip). The
            // layout already set height via log2(count+1).
            const float w = item.rect.width;
            const float h = item.rect.height;
            const float cx = item.rect.x + w * 0.5f;
            const Vec4 col = palette.stateWarning;
            // 4 rows of rects: each row centered, shrinking in width.
            const int rows = 4;
            for (int r = 0; r < rows; ++r) {
                const float rowH = h / static_cast<float>(rows);
                const float rowY = item.rect.y + rowH * static_cast<float>(r);
                // Top row = narrowest (tip), bottom = widest (base).
                const float widthFrac =
                    static_cast<float>(r + 1) / static_cast<float>(rows);
                const float rowW = std::max(1.0f, w * widthFrac);
                text.AddRect(cx - rowW * 0.5f, rowY, rowW, rowH, col);
            }
            break;
        }

        case VfxTimelineItemType::SeekHandle:
            text.AddRect(item.rect.x, item.rect.y, item.rect.width,
                         item.rect.height, palette.textPrimary);
            break;

        case VfxTimelineItemType::None:
            break;
        }
    }
}

} // namespace uz
