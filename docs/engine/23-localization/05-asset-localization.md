# Asset Localization — Texture/Audio Variants, Per-Locale Fonts, Hot-Reload

> **Depends on**: Localization system (§23), asset cooking (§43), resource system (§06)
> **Used by**: UI system (§21), Audio system (§25), rendering (§02)
> **File formats**: `.uzlocalized` (JSON asset variants), locale-specific font configs

## Purpose

Not all assets are language-neutral. Some require localization:

- **UI graphics**: Menu backgrounds, buttons with text embedded (e.g., Japanese requires different proportions than English)
- **Voice actors**: Dubbed audio in different languages
- **Textures**: Some cultures use different colors/symbols (e.g., red = good in China, unlucky in Thailand)
- **Fonts**: Latin fonts don't render CJK well; CJK fonts often too heavy for Latin text
- **Icons**: Same concepts may be represented differently across cultures

The localization system automatically selects the right variant for the current locale.

## Asset Variant Manifest — `.uzlocalized` Format

A metadata file points to locale-specific variants:

```json
{
  "type": "texture_variants",
  "default_locale": "en",
  "description": "Main menu background with locale-specific graphics",

  "locales": {
    "en": {
      "path": "content/ui/menu_bg_en.png"
    },
    "pt_BR": {
      "path": "content/ui/menu_bg_pt.png"
    },
    "ja": {
      "path": "content/ui/menu_bg_ja.png"
    },
    "ar": {
      "path": "content/ui/menu_bg_ar.png"
    }
  }
}
```

At runtime, the resource system loads the variant matching the current locale:

```cpp
namespace uz::Resource {
    /// Load asset, automatically selecting locale variant.
    /// If current locale unavailable, falls back to default.
    template<typename T>
    ResourceHandle<T> Load(const uz::Path& uzlocalized_path);

    // Usage:
    auto bg = uz::Resource::Load<TextureAsset>("content/ui/menu_background.uzlocalized");
    // Loads "content/ui/menu_bg_en.png" if locale=en
    // Loads "content/ui/menu_bg_ja.png" if locale=ja
    // Falls back to "content/ui/menu_bg_en.png" if locale unavailable
}
```

## Variants by Type

### Texture Variants

```json
{
  "type": "texture_variants",
  "locales": {
    "en": "content/ui/button_en.png",
    "ja": "content/ui/button_ja.png"
  }
}
```

Common reasons:

- **Text in image**: Button labels, UI text already baked into texture
- **Cultural symbols**: Different colors/imagery for different markets
- **Proportions**: English text needs different layout than Asian text
- **Resolution**: Some high-res languages need larger canvases

### Audio Variants

```json
{
  "type": "audio_variants",
  "description": "Narration track with localized voice actors",
  "locales": {
    "en": "content/audio/narrator_en.ogg",
    "es": "content/audio/narrator_es.ogg",
    "ja": "content/audio/narrator_ja.ogg"
  }
}
```

Common reasons:

- **Dubbed dialogue**: Different voice actors per language
- **SFX naming**: Different names/languages in audio files
- **Compression**: Different languages may need different bit rates

### Material Variants

Some materials need locale-specific parameters:

```json
{
  "type": "material_variants",
  "base_material": "content/materials/ui_text.uzmaterial",
  "locales": {
    "ja": {
      "overrides": {
        "font_scale": 0.9,
        "line_spacing": 1.2
      }
    },
    "ar": {
      "overrides": {
        "text_direction": "rtl",
        "glyph_spacing": 1.1
      }
    }
  }
}
```

## Font Assignment per Locale

Fonts are assigned at runtime based on locale and script:

```cpp
namespace uz::Localization {
    /// Font assignment for a locale.
    struct LocaleFontConfig {
        std::string locale;

        // Font per script
        std::unordered_map<std::string, ResourceHandle<FontAsset>> fonts;
        // "latin" → Roboto
        // "cjk" → NotoSansCJK
        // "arabic" → NotoSansArabic
        // "devanagari" → NotoSansDevanagari
    };

    /// Register locale font config.
    void SetLocaleFontConfig(const LocaleFontConfig& config);

    /// Get font for script in current locale.
    /// Falls back: current locale → language root → English → Latin (safe default)
    ResourceHandle<FontAsset> GetFontForScript(StringView script);
}
```

Configuration (from project `.uzproj`):

```toml
[localization.fonts]
# Default fonts (used by any locale that doesn't override)
latin = "content/fonts/Roboto-Regular.uzfont"
cjk = "content/fonts/NotoSansCJK-Regular.uzfont"
arabic = "content/fonts/NotoSansArabic-Regular.uzfont"

[localization.locales.ja]
# Override CJK font for Japanese
fonts.cjk = "content/fonts/NotoSerifJP-Regular.uzfont"

[localization.locales.ar]
# Override for Arabic (RTL, different shaping)
fonts.arabic = "content/fonts/GEDinarTwoArabic-Regular.uzfont"
```

When rendering text:

```cpp
// Automatic font selection based on script detection
std::string text = uz::Localization::GetString("ui.menu.play");
// "プレイ" (Japanese)

auto script = uz::Localization::DetectScriptFromString(text);
// Detects: CJK

auto font = uz::Localization::GetFontForScript(script);
// Returns: Japanese NotoSerifJP (locale ja override)

auto shaped = uz::Localization::ShapeText(text, font);
// Text shaped with correct font
```

## Loading Localized Assets

Asset loader handles variants transparently:

```cpp
namespace uz::Resource {
    template<typename T>
    class AssetLoader {
    public:
        /// Load asset, checking for .uzlocalized variant first.
        /// If .uzlocalized exists: select variant for current locale
        /// If .uzlocalized doesn't exist: load asset directly
        ResourceHandle<T> Load(const uz::Path& path);
    };
}

// Usage (same for all asset types)
auto texture = uz::Resource::Load<TextureAsset>("content/ui/menu_bg.uzlocalized");
auto audio = uz::Resource::Load<AudioClip>("content/audio/narrator.uzlocalized");
auto font = uz::Resource::Load<FontAsset>("content/fonts/ui_font.uzlocalized");
```

Implementation:

```cpp
// Pseudocode
template<typename T>
ResourceHandle<T> AssetLoader::Load(const uz::Path& path) {
    uz::Path localized_path = path.with_extension("uzlocalized");

    if (FileSystem::Exists(localized_path)) {
        // Load .uzlocalized metadata
        auto manifest = LoadJSON(localized_path);
        StringView locale = uz::Localization::GetCurrentLocale();

        // Find variant for locale
        auto variant_it = manifest["locales"].find(locale);
        if (variant_it == manifest["locales"].end()) {
            // Fallback to default
            variant_it = manifest["locales"].find(manifest["default_locale"]);
        }

        uz::Path variant_path = variant_it->second["path"];
        return AssetCache::Load<T>(variant_path);
    } else {
        // No variants, load directly
        return AssetCache::Load<T>(path);
    }
}
```

## Hot-Reload on Locale Change

When the player switches locales (language selection menu), affected assets reload:

```cpp
namespace uz::Localization {
    /// Called when locale changes.
    /// Clears cached assets and reloads all .uzlocalized variants.
    void OnLocaleChanged(StringView new_locale);
}

// Behind the scenes:
// 1. Clear asset cache entries for .uzlocalized files
// 2. Unload old locale-specific fonts
// 3. Load new locale-specific fonts
// 4. Re-trigger dependent assets (UI texts, textures, etc.)
// 5. Signal observers: UI refreshes, animations restart
```

Usage in game code:

```lua
-- scripts/settings_menu.lua
function on_language_select(ctx, new_locale)
    -- Change locale
    ctx.locale:set_locale(new_locale)
    -- Assets automatically reload; UI updates
end
```

## Asset Organization Convention

Recommended structure for localized assets:

```
content/
├── ui/
│   ├── menu_bg_en.png
│   ├── menu_bg_ja.png
│   ├── menu_bg.uzlocalized          ← metadata file
│   ├── button_en.png
│   └── button.uzlocalized
├── audio/
│   ├── narrator_en.ogg
│   ├── narrator_es.ogg
│   ├── narrator.uzlocalized
│   ├── sfx_wood_hit.ogg             ← no localization needed
├── fonts/
│   ├── Roboto-Regular.uzfont
│   ├── NotoSansCJK-Regular.uzfont
│   ├── NotoSansArabic-Regular.uzfont
│   └── ui_font.uzlocalized          ← points to locale-specific fonts
└── localization/
    ├── ui.uzloc
    ├── npc_dialogue.uzloc
    └── gameplay.uzloc
```

## Cooking Localized Assets

The `uzcook` tool handles variants:

```bash
# Cook all variants for all locales
$ uzcook --locales en,ja,ar,es

# Processes:
# 1. Detects all .uzlocalized files
# 2. For each manifest, cooks all locale variants
# 3. Compresses/optimizes appropriately
# 4. Creates .uzpak with all variants included
```

Cook manifest entry:

```json
{
  "assets": [
    {
      "path": "content/ui/menu_bg.uzlocalized",
      "type": "texture_variants",
      "locales": ["en", "ja", "ar", "es"],
      "compression": "BC7",
      "output": "menu_bg.uzpak"
    }
  ]
}
```

## Example: Multilingual Menu

Complete example with text + localized graphics:

```cpp
// UI code rendering menu
class MainMenu {
    struct MenuBackground {
        ResourceHandle<TextureAsset> texture;
        Vec2 size;
    };

    MenuBackground bg_;

public:
    void Initialize() {
        // Load menu background with locale variants
        bg_.texture = uz::Resource::Load<TextureAsset>("content/ui/menu_bg.uzlocalized");
        // Automatically selects locale variant
    }

    void OnLocaleChanged() {
        // Reload background for new locale
        bg_.texture = uz::Resource::Load<TextureAsset>("content/ui/menu_bg.uzlocalized");
    }

    void Render(uz::Renderer& renderer) {
        // Render background
        renderer.DrawTexture(bg_.texture, {0, 0});

        // Render localized text on top
        std::string play_text = uz::Localization::GetString("ui.menu.play");
        auto font = uz::Localization::GetFontForScript(
            uz::Localization::DetectScriptFromString(play_text)
        );
        auto shaped = uz::Localization::ShapeText(play_text, font);
        auto mesh = uz::Rendering::BakeTextMesh(shaped);
        renderer.DrawMesh(mesh, {100, 200});
    }
};
```

## Performance Considerations

1. **Cache font atlases**: Pre-load all fonts for current locale at startup
2. **Lazy-load textures**: Load variant only when needed
3. **Avoid reloading on every frame**: Cache shaped text
4. **Minimize variant count**: Keep essential variants only (en, ja, ar, es)

## Comparison

| Feature | Unreal 5 | Godot 4 | Unity | **UzEngine** |
|---------|----------|---------|-------|--------------|
| Asset variants | Via StringTables | Limited | Via Addressables | **`.uzlocalized` manifests** |
| Font assignment | Per-locale settings | Basic | Font assets | **Per-script fonts per locale** |
| Texture variants | Manual loading | Via ResourceLoader | Via address variants | **Automatic variant selection** |
| Audio localization | Via localized path names | String tables | Address variants | **`.uzlocalized` manifests** |
| Hot-reload | No | Yes | Yes | **Yes (with asset flush)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 3** | `.uzlocalized` manifest loader, texture variant selection, font assignment per script |
| **Phase 4** | **Audio variant selection**, material parameter overrides, asset hot-reload on locale change |
| **Phase 5** | Intelligent caching, variant preloading strategy, analytics on which variants are used |
