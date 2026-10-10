#include "engine/vfx/editor/vfx_editor_slate.h"

#include "engine/ui/slate_box.h"
#include "engine/ui/slate_button.h"
#include "engine/ui/slate_combobox.h"
#include "engine/ui/slate_declarative.h"
#include "engine/ui/slate_editable_text.h"
#include "engine/ui/slate_scrollbox.h"
#include "engine/ui/slate_spacer.h"
#include "engine/ui/slate_style.h"
#include "engine/ui/slate_text_block.h"
#include "engine/ui/slate_widget.h"

#include <memory>
#include <string>
#include <vector>

namespace uz::vfx {

using namespace uz::ui;

// ─────────────────────────────────────────────────────────────────────────────
// Impl
// ─────────────────────────────────────────────────────────────────────────────

struct VfxEditorSlate::Impl {
    VFXSystem*       system{nullptr};
    ParticleEmitter* emitter{nullptr};

    VfxEditorTelemetry telemetry{};

    int          activeTool{0};
    int          selectedModule{-1};
    std::string  emitterName{"particle.uzfx"};

    SP<Widget>      root;
    SP<Widget>      titleBar;
    SP<Widget>      toolbar;
    SP<Widget>      workbench;
    SP<Widget>      statusBar;

    // Editor-state caches the property panel reads.
    float emissionRate{10.0f};
    float lifetime{2.0f};
    bool  isEmitting{true};

    // Stack module list — minimal model for drag-drop demo.
    std::vector<std::string> modules{"Spawn", "Update", "Render"};
};

VfxEditorSlate::VfxEditorSlate() : m_impl(new Impl) {}

VfxEditorSlate::~VfxEditorSlate() {
    delete m_impl;
}

void VfxEditorSlate::SetEmitter(VFXSystem* system, ParticleEmitter* emitter) {
    m_impl->system = system;
    m_impl->emitter = emitter;
}

void VfxEditorSlate::SetTelemetry(const VfxEditorTelemetry& t) noexcept {
    m_impl->telemetry = t;
}

int  VfxEditorSlate::GetActiveTool() const noexcept { return m_impl->activeTool; }
void VfxEditorSlate::SetActiveTool(int idx) noexcept { m_impl->activeTool = idx; }

int  VfxEditorSlate::GetSelectedModule() const noexcept { return m_impl->selectedModule; }
void VfxEditorSlate::SetSelectedModule(int idx) noexcept { m_impl->selectedModule = idx; }

const std::string& VfxEditorSlate::GetEmitterName() const noexcept {
    return m_impl->emitterName;
}
void VfxEditorSlate::SetEmitterName(std::string name) {
    m_impl->emitterName = std::move(name);
}

SP<Widget> VfxEditorSlate::GetRoot()      const noexcept { return m_impl->root; }
SP<Widget> VfxEditorSlate::GetTitleBar()  const noexcept { return m_impl->titleBar; }
SP<Widget> VfxEditorSlate::GetToolbar()   const noexcept { return m_impl->toolbar; }
SP<Widget> VfxEditorSlate::GetWorkbench() const noexcept { return m_impl->workbench; }
SP<Widget> VfxEditorSlate::GetStatusBar() const noexcept { return m_impl->statusBar; }

namespace {

/// Helper that wraps a panel body in the standard "panel.body" Brush
/// rectangle so each cell of the workbench has consistent chrome.
SP<Widget> WrapPanel(const std::string& title, SP<Widget> content) {
    auto root = std::make_shared<VerticalBox>();
    root->AddSlot(New<TextBlock>().Text(title).FontSize(11.0f)).Padding_(Vec4{8, 4, 8, 4});
    root->AddSlot(content).FillSize(1.0f);
    return root;
}

} // anonymous namespace

void VfxEditorSlate::BuildRoot() {
    // ── Title bar (28 px) ─────────────────────────────────────────────
    auto title = std::make_shared<HorizontalBox>();
    // 3 traffic-light dots — placeholder until icon atlas integration.
    for (int i = 0; i < 3; ++i) {
        auto dot = std::make_shared<Spacer>(Vec2{12.0f, 12.0f});
        title->AddSlot(dot).Padding_(Vec4{4, 8, 4, 8});
    }
    auto titleLabel = New<TextBlock>().FontSize(11.5f);
    // Lambda binding so renaming the emitter live-updates the title.
    titleLabel.Get()->Text =
        std::function<std::string()>([this]() { return m_impl->emitterName; });
    title->AddSlot(titleLabel).FillSize(1.0f).HAlign_(HAlign::Center);
    title->AddSlot(std::make_shared<Spacer>(Vec2{22.0f, 22.0f}))
        .Padding_(Vec4{0, 3, 8, 3}); // profile circle placeholder
    m_impl->titleBar = title;

    // ── Toolbar (36 px) ───────────────────────────────────────────────
    auto toolbar = std::make_shared<HorizontalBox>();
    const std::vector<std::string> toolLabels = {
        "Select", "Move", "Rotate", "Scale", "Play", "Pause", "Stop", "Reset"};
    for (std::size_t i = 0; i < toolLabels.size(); ++i) {
        auto btn = std::make_shared<Button>();
        btn->Padding = Vec4{6, 4, 6, 4};
        btn->Content = New<TextBlock>().Text(toolLabels[i]).FontSize(11.0f);
        const int idx = static_cast<int>(i);
        btn->OnClick = [this, idx]() { SetActiveTool(idx); };
        toolbar->AddSlot(btn).Padding_(Vec4{2, 4, 2, 4});
    }
    toolbar->AddSlot(std::make_shared<Spacer>()).FillSize(1.0f); // pushes the rest right
    m_impl->toolbar = toolbar;

    // ── Workbench (1fr) ───────────────────────────────────────────────
    // Two-column: left = (Viewport fill + Graph 200px), right = (Details fill + Stack 200px + Curve 180px).
    auto leftCol = std::make_shared<VerticalBox>();
    leftCol->AddSlot(WrapPanel("Viewport", New<TextBlock>().Text("[3D viewport]").FontSize(11.0f)))
        .FillSize(1.0f).Padding_(Vec4{4, 4, 2, 2});
    leftCol->AddSlot(WrapPanel("Graph", New<TextBlock>().Text("Spawn → Update → Render").FontSize(10.5f)))
        .Padding_(Vec4{4, 2, 2, 4});

    auto rightCol = std::make_shared<VerticalBox>();
    {
        // Details panel: ScrollBox of 3 PropertyRows.
        auto detailsContent = std::make_shared<VerticalBox>();
        auto addRow = [&](const std::string& label, SP<Widget> editor) {
            auto row = std::make_shared<HorizontalBox>();
            row->AddSlot(New<TextBlock>().Text(label).FontSize(11.0f))
                .FillSize(0.4f).Padding_(Vec4{8, 4, 4, 4});
            row->AddSlot(editor).FillSize(0.6f).Padding_(Vec4{4, 4, 8, 4});
            detailsContent->AddSlot(row).Padding_(Vec4{0, 2, 0, 2});
        };
        addRow("Emission Rate", New<TextBlock>().Text("10.0").FontSize(11.0f));
        addRow("Lifetime",      New<TextBlock>().Text("2.0").FontSize(11.0f));
        addRow("Is Emitting",   New<TextBlock>().Text("On").FontSize(11.0f));

        auto scrollBox = std::make_shared<ScrollBox>();
        scrollBox->SetContent(detailsContent);
        rightCol->AddSlot(WrapPanel("Details", scrollBox))
            .FillSize(1.0f).Padding_(Vec4{2, 4, 4, 2});
    }
    {
        // Stack panel: header (EditableText name) + module list.
        auto stackBody = std::make_shared<VerticalBox>();
        auto nameInput = std::make_shared<EditableText>();
        nameInput->Text = m_impl->emitterName;
        nameInput->OnCommitted = [this](const std::string& v) { SetEmitterName(v); };
        stackBody->AddSlot(nameInput).Padding_(Vec4{6, 4, 6, 4});

        for (const auto& mod : m_impl->modules) {
            auto row = std::make_shared<HorizontalBox>();
            row->AddSlot(New<TextBlock>().Text(mod).FontSize(11.0f))
                .FillSize(1.0f).Padding_(Vec4{8, 3, 4, 3});
            stackBody->AddSlot(row);
        }
        rightCol->AddSlot(WrapPanel("Stack", stackBody)).Padding_(Vec4{2, 2, 4, 2});
    }
    {
        // Curve panel: minimal placeholder.
        rightCol->AddSlot(WrapPanel("Curve",
            New<TextBlock>().Text("Color over Life").FontSize(11.0f)))
            .Padding_(Vec4{2, 2, 4, 4});
    }

    auto workbench = std::make_shared<HorizontalBox>();
    workbench->AddSlot(leftCol).FillSize(2.0f);  // 2/3 of width
    workbench->AddSlot(rightCol).FillSize(1.0f); // 1/3 of width
    m_impl->workbench = workbench;

    // ── Status bar (22 px) ────────────────────────────────────────────
    auto statusBar = std::make_shared<HorizontalBox>();
    auto fpsTb = New<TextBlock>().FontSize(10.5f);
    fpsTb.Get()->Text = std::function<std::string()>([this]() {
        return std::string("FPS: ") + std::to_string(m_impl->telemetry.fps);
    });
    auto aliveTb = New<TextBlock>().FontSize(10.5f);
    aliveTb.Get()->Text = std::function<std::string()>([this]() {
        return std::string("Alive: ") + std::to_string(m_impl->telemetry.aliveCount);
    });
    auto maxTb = New<TextBlock>().FontSize(10.5f);
    maxTb.Get()->Text = std::function<std::string()>([this]() {
        return std::string("Max: ") + std::to_string(m_impl->telemetry.maxCount);
    });
    auto vramTb = New<TextBlock>().FontSize(10.5f);
    vramTb.Get()->Text = std::function<std::string()>([this]() {
        char buf[32];
        snprintf(buf, sizeof(buf), "VRAM: %.1f MB", m_impl->telemetry.vramUsedMb);
        return std::string(buf);
    });

    statusBar->AddSlot(fpsTb).Padding_(Vec4{12, 4, 16, 4});
    statusBar->AddSlot(aliveTb).Padding_(Vec4{0, 4, 16, 4});
    statusBar->AddSlot(maxTb).Padding_(Vec4{0, 4, 16, 4});
    statusBar->AddSlot(std::make_shared<Spacer>()).FillSize(1.0f);
    statusBar->AddSlot(vramTb).Padding_(Vec4{0, 4, 12, 4});
    m_impl->statusBar = statusBar;

    // ── Root VBox ─────────────────────────────────────────────────────
    auto root = std::make_shared<VerticalBox>();
    root->AddSlot(m_impl->titleBar);                      // 28 px (Auto)
    root->AddSlot(m_impl->toolbar);                       // 36 px (Auto)
    root->AddSlot(m_impl->workbench).FillSize(1.0f);      // fill
    root->AddSlot(m_impl->statusBar);                     // 22 px (Auto)
    m_impl->root = root;
}

void VfxEditorSlate::Tick(float dt) {
    if (!m_impl->root) return;
    m_impl->root->Tick(dt);
}

} // namespace uz::vfx
