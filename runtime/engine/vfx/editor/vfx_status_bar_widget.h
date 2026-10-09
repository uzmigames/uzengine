#pragma once

/// VFX Status Bar Widget — phase 14.6.0.
///
/// UE5-style bottom status strip. Exposes four left chips (FPS,
/// particle count, draw calls, VRAM), four right chips (saved
/// state, editor version), and a centre transient-notification
/// slot that auto-dismisses after 3 seconds.

#include "engine/math/vector4.h"
#include "engine/vfx/editor/vfx_stack_panel_widget.h"

#include <cstdint>
#include <deque>
#include <string>

namespace uz {

class TextRenderer;
class BitmapFont;
class MsdfFont;

enum class VfxStatusLevel : uint8_t {
    Info    = 0,
    Warning = 1,
    Error   = 2,
};

struct VfxStatusNotification {
    std::string   message;
    VfxStatusLevel level = VfxStatusLevel::Info;
    /// Seconds remaining before auto-dismiss.
    float         secondsRemaining = 3.0f;
};

class VfxStatusBarWidget {
public:
    // ── Tunables ────────────────────────────────────────────────────────
    float barHeight = 22.0f;
    float chipWidth = 90.0f;
    float chipGap   = 1.0f;

    // ── State setters ───────────────────────────────────────────────────

    void SetFps(float fps)                { m_fps = fps; }
    void SetParticleCount(uint64_t alive, uint64_t maxCount) {
        m_alive = alive; m_maxCount = maxCount;
    }
    void SetDrawCalls(uint32_t dc)        { m_drawCalls = dc; }
    void SetVramUsed(float usedMb, float totalMb) {
        m_vramUsedMb  = usedMb;
        m_vramTotalMb = totalMb;
    }
    void SetDirty(bool dirty)             { m_dirty = dirty; }
    void SetEditorVersion(const std::string& v) { m_editorVersion = v; }

    /// Pushes a notification to the FIFO queue (capped at 8).
    void PushNotification(const std::string& msg, VfxStatusLevel level);

    /// Advances the notification timer. Host calls once per frame.
    void Tick(float dt);

    // ── Read-side ───────────────────────────────────────────────────────

    [[nodiscard]] float Fps() const               { return m_fps; }
    [[nodiscard]] uint64_t AliveCount() const     { return m_alive; }
    [[nodiscard]] uint64_t MaxCount() const       { return m_maxCount; }
    [[nodiscard]] uint32_t DrawCalls() const      { return m_drawCalls; }
    [[nodiscard]] float VramUsedMb() const        { return m_vramUsedMb; }
    [[nodiscard]] float VramTotalMb() const       { return m_vramTotalMb; }
    [[nodiscard]] bool IsDirty() const            { return m_dirty; }
    [[nodiscard]] size_t QueueSize() const        { return m_queue.size(); }
    [[nodiscard]] const VfxStatusNotification* CurrentNotification() const {
        return m_queue.empty() ? nullptr : &m_queue.front();
    }

    // ── Colour helpers (public for tests) ───────────────────────────────

    [[nodiscard]] static Vec4 FpsTint(float fps);
    [[nodiscard]] Vec4 NotificationTint(VfxStatusLevel level) const;

    // ── Render ──────────────────────────────────────────────────────────

    void Render(TextRenderer& text, const BitmapFont& font,
                const MsdfFont* msdf, const VfxStackRect& barBounds) const;

private:
    float    m_fps          = 60.0f;
    uint64_t m_alive        = 0;
    uint64_t m_maxCount     = 0;
    uint32_t m_drawCalls    = 0;
    float    m_vramUsedMb   = 0.0f;
    float    m_vramTotalMb  = 0.0f;
    bool     m_dirty        = false;
    std::string m_editorVersion = "0.1";
    std::deque<VfxStatusNotification> m_queue;
};

} // namespace uz
