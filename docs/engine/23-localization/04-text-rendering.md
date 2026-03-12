# Text Rendering Pipeline — HarfBuzz Shaping, SDF Atlases, BiDi, Line Breaking

> **Depends on**: String tables (§23.2), ICU formatting (§23.3), HarfBuzz library, RHI (§03)
> **Used by**: UI text rendering (§21), in-game text, dialogue boxes
> **Standards**: Unicode Text Shaping, ICU BiDi (UBA), OpenType

## Purpose

Raw Unicode strings need complex processing before rendering:

1. **Glyph shaping**: Convert characters to glyphs with contextual forms (Arabic letters change shape based on position)
2. **Bidirectional reordering**: Right-to-left text (Arabic, Hebrew) mixed with left-to-right (English) requires visual reordering
3. **Line breaking**: Break text into lines respecting script rules and word boundaries
4. **Font lookup**: Complex scripts need specific fonts (CJK fonts differ from Latin)
5. **SDF atlas rendering**: Render shaped glyphs from compressed font atlases

This pipeline is invisible to game code — pass a string and locale, get a renderable text mesh.

## Text Shaping with HarfBuzz

HarfBuzz converts a Unicode string to shaped glyphs. Key transformations:

- **Arabic contextual forms**: The letter ع changes to different glyphs based on position (isolated, initial, medial, final)
- **Ligatures**: Some scripts combine characters (Arabic lam-alef, many scripts' fi/fl)
- **Diacritic placement**: Accent marks positioned correctly above/below base letters
- **Kerning**: Adjust spacing between glyphs (A and V need tighter spacing than A and B)
- **Complex scripts**: Devanagari matras, Thai tone marks, etc.

### Glyph Data

```cpp
namespace uz::Localization {
    /// Per-glyph info after shaping.
    struct ShapedGlyph {
        uint32_t codepoint;     // Original Unicode codepoint
        uint32_t glyph_id;      // Glyph ID in font file (for atlas lookup)
        float advance_x;        // Horizontal advance (pixels)
        float advance_y;        // Vertical advance (for vertical scripts)
        float offset_x;         // X offset from baseline
        float offset_y;         // Y offset from baseline
        uint16_t cluster;       // Original character index (for BiDi)
    };

    /// Complete shaped text.
    struct ShapedText {
        std::vector<ShapedGlyph> glyphs;
        float total_width = 0.0f;
        float total_height = 0.0f;
        StringView locale;
        ResourceHandle<FontAsset> font;
    };
}
```

### HarfBuzz API

```cpp
namespace uz::Localization {
    /// Shape text: convert Unicode → positioned glyphs.
    /// Applies font's OpenType features (contextual substitution, positioning).
    ShapedText ShapeText(
        StringView text,
        ResourceHandle<FontAsset> font,
        float font_size,
        StringView locale = ""  // Current locale if empty
    );
}

// Usage:
std::string text = uz::Localization::GetString("game.npc.greeting");
auto font = uz::Localization::ResolveFont(text, "ar");  // Detect script, get font
auto shaped = uz::Localization::ShapeText(text, font, 48.0f);

// Now we have positioned glyphs ready for atlas lookup and rendering
for (const auto& glyph : shaped.glyphs) {
    uint32_t glyph_id = glyph.glyph_id;
    float x = glyph.offset_x;
    float y = glyph.offset_y;
    // Render glyph from atlas at (x, y)
}
```

## Bidirectional Text (BiDi)

Mixed LTR (left-to-right) and RTL (right-to-left) text requires reordering for display.

### The Problem

Text like `"Hello עברית"` (English + Hebrew) stores characters in logical order (as typed). For display, we need visual order: `"Hello" + mirrored reversed "עברית"`.

### BiDi Algorithm (Unicode Bidirectional Algorithm — UBA)

ICU implements UBA to reorder text:

```
Input (logical): "Hello עברית"
              [H][e][l][l][o][ ][ע][ב][ר][י][ת]

Output (visual): "Hello" + reversed Hebrew text
              [H][e][l][l][o][ ][ת][י][ר][ב][ע]
```

### BiDi Runs

Text is split into runs where each run has a consistent direction:

```cpp
namespace uz::Localization {
    /// One directional run in text.
    struct BiDiRun {
        int start;      // Byte offset in original string
        int end;        // Byte offset
        bool is_rtl;    // True if right-to-left (Arabic, Hebrew)
        uint8_t level;  // Embedding level (UBA concept)
    };

    /// Analyze BiDi and return runs in visual order.
    std::vector<BiDiRun> AnalyzeBiDi(
        StringView text,
        StringView locale = ""
    );
}

// Usage:
std::vector<BiDiRun> runs = uz::Localization::AnalyzeBiDi("Hello עברית", "ar");
// Returns:
//   BiDiRun{0, 5, is_rtl=false}   — "Hello"
//   BiDiRun{5, 6, is_rtl=false}   — space
//   BiDiRun{6, 11, is_rtl=true}   — "עברית" (will be reversed)

// When rendering, iterate runs and reverse character order for RTL runs
for (const auto& run : runs) {
    if (run.is_rtl) {
        RenderReversed(text.substr(run.start, run.end - run.start));
    } else {
        RenderNormal(text.substr(run.start, run.end - run.start));
    }
}
```

### Mirroring

Some characters must be mirrored in RTL context: `(` ↔ `)`, `[` ↔ `]`, etc.

```cpp
namespace uz::Localization {
    /// Get the mirrored character (e.g., '(' ↔ ')').
    /// Returns original if no mirror exists.
    uint32_t GetMirrorCharacter(uint32_t codepoint);
}
```

## Line Breaking

Break text into lines respecting script rules and word boundaries.

### Line Break Rules

- **Latin script**: Break at spaces or hyphens
- **CJK script**: Can break anywhere (dense scripts)
- **Arabic/Thai**: Break at word/syllable boundaries
- **Combining marks**: Never break before combining diacritics

### API

```cpp
namespace uz::Localization {
    /// One line break position.
    struct LineBreak {
        int position;       // Byte offset where line can break
        float width;        // Pixel width to this break
        bool is_hard;       // Hard break (explicit newline)
    };

    /// Find all possible line breaks for text.
    std::vector<LineBreak> FindLineBreaks(
        StringView text,
        float max_width,
        ResourceHandle<FontAsset> font,
        StringView locale = ""
    );
}

// Usage: wrap text to 300 pixel width
auto font = uz::Localization::ResolveFont(text, "en");
auto breaks = uz::Localization::FindLineBreaks(text, 300.0f, font);

std::string current_line;
int last_break = 0;
for (const auto& br : breaks) {
    if (br.width > 300.0f) {
        // Create line from last_break to br.position
        std::string line = text.substr(last_break, br.position - last_break);
        RenderLine(line);
        last_break = br.position;
    }
}
```

## SDF Font Atlas System

Game fonts are rendered offline as Signed Distance Field (SDF) atlases — enables high-quality text at any size.

### SDF Generation

During cooking (uzcook):

```bash
$ uzcook --locale ja_JP --font content/fonts/NotoSansCJK-Regular.otf
  → Generates SDF atlas for CJK glyphs (covers ~20,000 characters)
  → Outputs: content/.cache/fonts/noto_cjk_sdf.uzfont
```

Each locale gets its own set of font atlases (per script/font combination).

### Font Asset Format

```cpp
/// Compiled font asset (SDF atlas).
struct FontAsset {
    ResourceHandle<TextureAsset> atlas;    // SDF texture
    std::vector<GlyphMetrics> glyphs;      // Per-glyph info

    struct GlyphMetrics {
        uint32_t glyph_id;
        float uv_x, uv_y;      // Position in atlas
        float uv_width, uv_height;
        float advance_x;
        float offset_x, offset_y;
        uint16_t atlas_page;   // Which atlas page (for large fonts)
    };
};
```

### SDF Rendering

At runtime, shaped glyphs are looked up in the atlas and rendered:

```cpp
namespace uz::Rendering {
    /// Render shaped text using SDF atlas.
    /// Outputs a mesh with quads, each mapped to atlas region.
    struct TextMesh {
        std::vector<Vertex> vertices;    // Quad vertices
        std::vector<uint32_t> indices;   // Triangle indices
        ResourceHandle<Material> material;  // SDF shader material
    };

    TextMesh BakeTextMesh(
        const uz::Localization::ShapedText& shaped,
        const uz::Localization::FontMetrics& font_metrics
    );
}

// Usage:
auto shaped = uz::Localization::ShapeText("Hello", font, 48.0f);
auto mesh = uz::Rendering::BakeTextMesh(shaped, font->metrics);
renderer->DrawMesh(mesh);
```

## Complete Pipeline Example

```cpp
// Game code: render NPC greeting text
std::string greeting_key = "game.npc.greeting";

// 1. Get localized string
std::string text = uz::Localization::GetString(greeting_key);
// Result: "Hello, traveler!" (or locale-specific variant)

// 2. Detect script and resolve font
auto script = uz::Localization::DetectScriptFromString(text);
auto font = uz::Localization::ResolveFont(script);

// 3. Analyze BiDi (in case of mixed directions)
auto bidi_runs = uz::Localization::AnalyzeBiDi(text);

// 4. Shape text with HarfBuzz
auto shaped = uz::Localization::ShapeText(text, font, 48.0f);

// 5. Find line breaks for 400px width
auto breaks = uz::Localization::FindLineBreaks(text, 400.0f, font);

// 6. Create text mesh (multiple lines if necessary)
uz::Rendering::TextMesh mesh;
for (size_t i = 0; i < breaks.size(); i++) {
    int start = (i == 0) ? 0 : breaks[i - 1].position;
    int end = breaks[i].position;
    std::string line = text.substr(start, end - start);

    auto line_shaped = uz::Localization::ShapeText(line, font, 48.0f);
    auto line_mesh = uz::Rendering::BakeTextMesh(line_shaped, font->metrics);
    mesh.vertices.insert(mesh.vertices.end(), line_mesh.vertices.begin(), line_mesh.vertices.end());
    mesh.indices.insert(mesh.indices.end(), line_mesh.indices.begin(), line_mesh.indices.end());
}

// 7. Render to screen
renderer->DrawMesh(mesh, screen_position, color);
```

## Font Selection Algorithm

```cpp
// When rendering text, algorithm chooses appropriate font:

std::string text = "Hello 世界";
// Contains both Latin (Hello) and CJK (世界)

auto font_for_text = uz::Localization::ResolveFont(text);
// 1. Detect scripts: Latin + CJK
// 2. Look up locale fonts for CJK → "NotoSansCJK-Regular"
// 3. Use CJK font (covers both Latin and CJK)

// If locale-specific font unavailable:
// → Fall back to script default
// → Fall back to Latin (safe default)
```

## SDF Shader

SDF rendering uses a specialized shader to handle anti-aliasing:

```hlsl
// sdf_font.hlsl
cbuffer Constants : register(b0) {
    float4x4 transform;
    float4 color;
    float smoothing;    // [0.0, 1.0] for edge smoothness
};

Texture2D sdf_atlas : register(t0);
SamplerState atlas_sampler : register(s0);

struct PS_Input {
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

float4 main(PS_Input input) : SV_TARGET {
    // Sample SDF atlas
    float sdf = sdf_atlas.Sample(atlas_sampler, input.uv).r;

    // Convert SDF to alpha (smoothstep for anti-aliasing)
    float dist = sdf;
    float alpha = smoothstep(0.5 - smoothing, 0.5 + smoothing, dist);

    return input.color * alpha;
}
```

## Caching

Text shaping is expensive; cache shaped text:

```cpp
class TextRenderCache {
    std::unordered_map<uint64_t, uz::Localization::ShapedText> shaped_;

    uint64_t HashKey(StringView text, StringView locale, float font_size) {
        // Hash text + locale + size for cache key
    }

public:
    const uz::Localization::ShapedText& GetShapedText(
        StringView text,
        StringView locale,
        float font_size
    ) {
        uint64_t key = HashKey(text, locale, font_size);
        if (shaped_.count(key)) {
            return shaped_[key];
        }

        // Shape and cache
        auto shaped = uz::Localization::ShapeText(text, locale, font_size);
        return shaped_[key] = shaped;
    }
};
```

## Comparison

| Feature | Unreal 5 | Godot 4 | Dear ImGui | **UzEngine** |
|---------|----------|---------|-----------|--------------|
| HarfBuzz | Yes | Yes | No (limited) | **Yes** |
| BiDi support | Via FTextBiDi | Via ICU | No | **ICU BiDi** |
| Line breaking | FSlateTextUnderlineBlock | BreakIterator | Simple | **ICU line breaking** |
| SDF fonts | Yes | Yes | Yes | **Yes (per-locale atlases)** |
| Script detection | Manual | Partial | No | **Auto-detect** |
| RTL mirroring | Yes | Yes | No | **Yes** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | HarfBuzz integration, SDF font atlas loading, basic text shaping |
| **Phase 4** | **BiDi support** (ICU UBA), line breaking, SDF shader rendering, per-locale font selection |
| **Phase 5** | Text mesh caching, complex script support (Thai ligatures, Devanagari), vertical text |
