#include "engine/ui/slate_style.h"

namespace uz::ui {

namespace {

/// Hex literal RGB → Color (alpha defaults to 1).
constexpr Color RGB(int r, int g, int b, float a = 1.0f) {
    return Color{
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        a};
}

} // anonymous namespace

SP<StyleSet> CreateClaudeDarkStyleSet() {
    auto ss = std::make_shared<StyleSet>();

    // ── Background tier (5 levels: deepest → shallowest) ──────────────
    // Approximates oklch(0.18..0.30 .005 240..245) from styles.css.
    ss->SetColor("bg.0", RGB(0x15, 0x17, 0x1a));
    ss->SetColor("bg.1", RGB(0x1d, 0x21, 0x27));
    ss->SetColor("bg.2", RGB(0x23, 0x28, 0x2f));
    ss->SetColor("bg.3", RGB(0x2c, 0x33, 0x3b));
    ss->SetColor("bg.4", RGB(0x35, 0x3b, 0x44));

    // ── Line tier ─────────────────────────────────────────────────────
    ss->SetColor("line.0", RGB(0x2a, 0x30, 0x39));
    ss->SetColor("line.1", RGB(0x36, 0x3d, 0x47));
    ss->SetColor("line.2", RGB(0x46, 0x4e, 0x5a));

    // ── Foreground tier ───────────────────────────────────────────────
    ss->SetColor("fg.0", RGB(0xea, 0xee, 0xf3));
    ss->SetColor("fg.1", RGB(0xc1, 0xc8, 0xd2));
    ss->SetColor("fg.2", RGB(0x8e, 0x97, 0xa3));
    ss->SetColor("fg.3", RGB(0x55, 0x5c, 0x67));

    // ── Accent palette (oklch(0.72 0.13 235) ≈ #4ba3d6) ───────────────
    ss->SetColor("accent",       RGB(0x4b, 0xa3, 0xd6));
    ss->SetColor("accent.soft",  RGB(0x4b, 0xa3, 0xd6, 0.18f));
    ss->SetColor("accent.line",  RGB(0x4b, 0xa3, 0xd6, 0.55f));

    // ── Status colors ─────────────────────────────────────────────────
    ss->SetColor("warn", RGB(0xe6, 0xb4, 0x4f));
    ss->SetColor("ok",   RGB(0x60, 0xc6, 0x8f));
    ss->SetColor("err",  RGB(0xd8, 0x6a, 0x6a));

    // ── Secondary palette ─────────────────────────────────────────────
    ss->SetColor("magenta", RGB(0xc8, 0x6a, 0xd8));
    ss->SetColor("purple",  RGB(0x9a, 0x6a, 0xd8));
    ss->SetColor("cyan",    RGB(0x6a, 0xc8, 0xd8));

    // ── Axis colors (vec3 prefix RGB convention from VFX widgets) ─────
    ss->SetColor("axis.x", RGB(0xd8, 0x66, 0x66));
    ss->SetColor("axis.y", RGB(0x66, 0xd8, 0x6e));
    ss->SetColor("axis.z", RGB(0x66, 0x86, 0xd8));

    // ── Brushes ───────────────────────────────────────────────────────
    // panel.head: gradient bg.2 → bg.1 with bottom 1px line
    {
        Brush head = MakeGradient(ss->GetColor("bg.2"), ss->GetColor("bg.1"), 4.0f);
        head.borderThickness = 1.0f;
        head.borderColor = ss->GetColor("line.0");
        ss->SetBrush("panel.head", head);
    }
    // panel.body: flat bg.1 with 1px border line.0
    ss->SetBrush("panel.body", MakeBordered(
        ss->GetColor("bg.1"), 1.0f, ss->GetColor("line.0"), 4.0f));

    // tab.active: accent.soft fill + accent.line border
    ss->SetBrush("tab.active", MakeBordered(
        ss->GetColor("accent.soft"), 1.0f, ss->GetColor("accent.line"), 4.0f));

    // tab.inactive: bg.2 transparent body, no border
    ss->SetBrush("tab.inactive", MakeSolidColor(ss->GetColor("bg.2"), 4.0f));

    // button.primary: accent.soft + accent.line border
    ss->SetBrush("button.primary", MakeBordered(
        ss->GetColor("accent.soft"), 1.0f, ss->GetColor("accent.line"), 4.0f));

    // button.ghost: zero alpha fill + line.1 border (outline button)
    ss->SetBrush("button.ghost", MakeBordered(
        Color{0, 0, 0, 0}, 1.0f, ss->GetColor("line.1"), 4.0f));

    return ss;
}

StyleStack& Styles() {
    static StyleStack g_stack;
    static bool g_initialized = false;
    if (!g_initialized) {
        g_initialized = true;
        g_stack.Push(CreateClaudeDarkStyleSet());
    }
    return g_stack;
}

} // namespace uz::ui
