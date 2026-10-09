/// UzEngine VFX Editor — Slate UI rebuild.
///
/// Phase 18 / Step 10. The Niagara-style VFX editor reconstructed on
/// top of the Slate UI framework (Steps 1-9). The editor's visual
/// identity follows the Claude design (`.sandbox/claude-design/
/// vfx-template/project/styles.css`):
///
///   - 4-row top chrome:     TitleBar 28px / Toolbar 36px / Workbench 1fr / StatusBar 22px.
///   - 5-cell workbench:     Viewport / Graph (top-left) | Details (full-height) /
///                            Stack / Curve (right column).
///
/// The editor builds its widget tree once in `BuildRoot()` and retains
/// it. Per-frame, `Tick(dt)` pulls live data via `Attribute<T>` lambdas
/// (FPS / Alive / Max / VRAM telemetry from the `ParticleEmitter`).
/// Selective invalidation keeps the redraw cost small.
///
/// All colors, brushes, and fonts come from `Styles().GetColor(...)` /
/// `Styles().GetBrush(...)` (Step 6) — no hard-coded RGB literals.
///
/// The editor exposes its built tree via `GetRoot()` for the host
/// application to drive (Tick + Arrange + Paint + InputRouter
/// dispatch). Step 10 ships the editor *class*; integration into
/// `vfx_viewport_app.exe` follows in a downstream task once the host
/// app's render-target / TextRenderer plumbing is wired to the Slate
/// renderer.

#pragma once

#include "engine/ui/slate_core.h"

#include <cstdint>
#include <string>

namespace uz {
class ParticleEmitter;
class VFXSystem;
} // namespace uz

namespace uz::ui {
class Widget;
class VerticalBox;
} // namespace uz::ui

namespace uz::vfx {

/// Snapshot of editor telemetry that drives the StatusBar bindings.
/// The host updates these fields each frame; the editor's StatusBar
/// reads via `Attribute<T>` lambdas so updates are zero-cost when the
/// values don't change.
struct VfxEditorTelemetry {
    int    fps{0};
    int    aliveCount{0};
    int    maxCount{0};
    float  vramUsedMb{0.0f};
};

class VfxEditorSlate {
public:
    /// Constructs the editor. `system` and `emitter` may be nullptr at
    /// construction; they MUST be set via `SetEmitter` before `Tick`.
    VfxEditorSlate();
    ~VfxEditorSlate();

    /// Replaces the active VFX system / particle emitter pointer. The
    /// editor pulls all live data through these on each Tick.
    void SetEmitter(VFXSystem* system, ParticleEmitter* emitter);

    /// Builds (or rebuilds) the entire widget tree. Idempotent — calling
    /// it twice replaces the existing root. Most callers invoke it
    /// exactly once after construction.
    void BuildRoot();

    /// Returns the root widget. nullptr until `BuildRoot()` runs.
    [[nodiscard]] ui::SP<ui::Widget> GetRoot() const noexcept;

    /// Per-frame data pull. Updates the telemetry snapshot the
    /// StatusBar reads from. Call before Arrange + Paint.
    void Tick(float dt);

    /// Direct write into the telemetry snapshot for hosts that own
    /// the data acquisition (e.g. the runtime's Stats overlay).
    void SetTelemetry(const VfxEditorTelemetry& t) noexcept;

    /// Currently selected toolbar tool (0..N-1; matches the toolbar
    /// button order returned by `BuildToolbar`).
    [[nodiscard]] int  GetActiveTool() const noexcept;
    void SetActiveTool(int idx) noexcept;

    /// Currently selected stack module index (-1 = none).
    [[nodiscard]] int  GetSelectedModule() const noexcept;
    void SetSelectedModule(int idx) noexcept;

    /// Visible name in the title bar / stack header. Defaults to
    /// "particle.uzfx".
    [[nodiscard]] const std::string& GetEmitterName() const noexcept;
    void SetEmitterName(std::string name);

    /// Test accessors for individual sub-trees built by `BuildRoot`.
    /// nullptr if BuildRoot hasn't run.
    [[nodiscard]] ui::SP<ui::Widget> GetTitleBar() const noexcept;
    [[nodiscard]] ui::SP<ui::Widget> GetToolbar() const noexcept;
    [[nodiscard]] ui::SP<ui::Widget> GetWorkbench() const noexcept;
    [[nodiscard]] ui::SP<ui::Widget> GetStatusBar() const noexcept;

private:
    struct Impl;
    Impl* m_impl{nullptr};
};

} // namespace uz::vfx
