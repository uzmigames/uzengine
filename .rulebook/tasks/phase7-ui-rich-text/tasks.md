## 1. BBCode Markup Language Specification

- [ ] 1.1 Define the full closed allow-list of supported tags in `src/ui/RichTextParser.h` as a constexpr tag table: `[b]`, `[i]`, `[u]`, `[s]`, `[color=#rrggbb]`, `[size=N]`, `[font=name]`, `[img src="asset://..." w=N h=N/]`, `[shadow dx=N dy=N color=#rrggbb]`, `[outline width=N color=#rrggbb]`, `[gradient from=#rrggbb to=#rrggbb angle=N]`, `[emoji size=N]`
- [ ] 1.2 Document that every tag not in the allow-list is escaped and emitted as literal text (e.g., `<script>` becomes `&lt;script&gt;`)
- [ ] 1.3 Define the expected HTML output for each tag in the spec table: `[b]` → `<span style="font-weight:bold">`, `[color=#ff0000]` → `<span style="color:#ff0000">`, `[img]` → `<img src="{data-uri}" width="{w}" height="{h}" style="vertical-align:middle">`, etc.

## 2. RichTextParser Implementation

- [ ] 2.1 Create `src/ui/RichTextParser.h` and `src/ui/RichTextParser.cpp` implementing `RichTextParser::parse(const std::string& markup) -> std::string` (returns safe HTML fragment)
- [ ] 2.2 Implement a hand-written recursive-descent O(n) parser: scan the input character by character; when `[` is encountered, attempt to match a tag name from the allow-list; on mismatch, emit `&lt;` and continue; on match, parse tag attributes
- [ ] 2.3 Implement attribute parsing for `[color=...]`: validate the value matches regex `#[0-9a-fA-F]{6}` exactly; reject and emit as escaped literal if invalid
- [ ] 2.4 Implement `[size=N]` attribute parsing: validate N is a positive integer in range [1, 256]; reject values outside this range (emit as escaped literal, log a warning)
- [ ] 2.5 Implement `[img src="asset://..."]` parsing: extract the asset path, call `AssetManager::resolve_to_data_uri(path)` which returns a `data:image/png;base64,...` URI; emit `<img src="{data-uri}" width="{w}" height="{h}">` with width/height attributes validated as positive integers ≤ 2048
- [ ] 2.6 Cache asset-path-to-data-URI resolutions in `RichTextParser` using an `std::unordered_map<std::string, std::string>` keyed by asset path; populate on first reference, reuse on subsequent parses of the same asset path
- [ ] 2.7 Implement `[gradient from=#rrggbb to=#rrggbb angle=N]` as a `<span>` with CSS `background: linear-gradient({angle}deg, {from}, {to}); -webkit-background-clip: text; -webkit-text-fill-color: transparent; background-clip: text`
- [ ] 2.8 Implement emoji passthrough: `[emoji size=N]U+1F525[/emoji]` is emitted as `<span style="font-size:{N}px">🔥</span>` (raw Unicode code point decoded from the `U+XXXX` notation)
- [ ] 2.9 Implement unclosed-tag handling: if a closing tag is missing at end-of-input, emit the closing `</span>` automatically and log a parse warning with the character position of the unclosed tag

## 3. XSS Sanitization

- [ ] 3.1 In `RichTextParser::parse()`, after constructing the HTML fragment, run a second-pass sanitizer that scans for any `<script`, `<style`, `javascript:`, `data:` (outside of sanitized asset img src), `on[a-z]+` event attribute patterns and replaces them with escaped equivalents — this is the defense-in-depth layer
- [ ] 3.2 Strip all HTML attribute names not in the explicit allow-list (`style`, `src`, `width`, `height`) from `<span>` and `<img>` elements produced by the parser
- [ ] 3.3 Escape all text content nodes using HTML entity encoding: `<` → `&lt;`, `>` → `&gt;`, `&` → `&amp;`, `"` → `&quot;`, `'` → `&#39;`

## 4. IPC Handler and Lua API

- [ ] 4.1 Add `ui.set_rich_text` IPC handler in `src/ipc/IpcServer.cpp`: receives `{element_id, markup}`, calls `RichTextParser::parse(markup)`, injects the result via `element.innerHTML = ...` in the browser JS bridge; store the original markup string server-side keyed by `element_id`
- [ ] 4.2 Add `ui.get_rich_text` IPC handler: returns the stored original markup string for the given `element_id` (not the DOM HTML)
- [ ] 4.3 Add `ui.measure_rich_text` IPC handler: call `element.getBoundingClientRect()` in the browser JS bridge, return `{width, height, line_count, baseline_y}` as a JSON response
- [ ] 4.4 Add Lua binding `ctx.ui:set_rich_text(element_id, markup)` in `src/scripting/lua_ui.cpp`; call the `ui.set_rich_text` IPC handler
- [ ] 4.5 Add Lua binding `ctx.ui:get_rich_text(element_id) -> string`
- [ ] 4.6 Add Lua binding `ctx.ui:measure_rich_text(element_id) -> {width, height, line_count, baseline_y}`

## 5. Localisation Integration

- [ ] 5.1 Verify that `ctx.locale:t(key)` returns a plain string (possibly containing markup); document that `ctx.ui:set_rich_text(id, ctx.locale:t("key"))` is the idiomatic pattern for rich-formatted localized strings
- [ ] 5.2 Ensure `RichTextParser::parse()` handles strings with no markup tags correctly: output should equal the HTML-entity-encoded input string without any `<span>` wrappers

## 6. VSCode Rich Text Preview Pane

- [ ] 6.1 Add a "Rich Text Preview" section to `vscode-ext/src/panels/UIEditorPanel.ts`: a text input field accepting markup and a `webview.html` live preview iframe rendering the parsed HTML
- [ ] 6.2 On each keystroke in the markup input, call the preview panel's backend to parse the markup via `RichTextParser` (or a JS-side equivalent for fast preview) and update the webview innerHTML
- [ ] 6.3 Display parse errors below the preview with the character position and error message highlighted in red; errors come from `RichTextParser` warning log output piped to the extension

## 7. Testing

- [ ] 7.1 Write `tests/ui/rich_text_parser_test.cpp`: test each supported tag produces the correct HTML output — verify `[b]hello[/b]` produces `<span style="font-weight:bold">hello</span>`
- [ ] 7.2 Test nested tags: `[b][color=#ff0000]bold red[/color][/b]` → verify correct nested `<span>` output with proper closing order
- [ ] 7.3 Test `[color=#gghhii]` (invalid hex) → verify emitted as escaped literal text, no HTML attribute produced
- [ ] 7.4 Test `[size=0]` and `[size=257]` → verify both are emitted as escaped literal text and a warning is logged
- [ ] 7.5 Test `[img src="asset://icons/fire.png" w=16 h=16/]` → verify data URI resolution is called with path `icons/fire.png` and output is `<img src="data:image/png;base64,..." width="16" height="16">`
- [ ] 7.6 Test XSS injection: input `[b]<script>alert(1)</script>[/b]` → verify `<script>` appears as `&lt;script&gt;` in output, no raw `<script>` tag
- [ ] 7.7 Test XSS injection: input `[color=javascript:alert(1)]text[/color]` → verify attribute is rejected and emitted as literal text
- [ ] 7.8 Test empty input string → verify output is empty string, no crash
- [ ] 7.9 Test unclosed tag: `[b]hello` → verify `</span>` is appended automatically and a parse warning is logged
- [ ] 7.10 Test plain string passthrough (no tags): `"Hello World"` → verify output is `"Hello World"` (HTML-entity-encoded)
- [ ] 7.11 Test `measure_rich_text` IPC handler: mock the browser `getBoundingClientRect()` return value; verify the Lua table fields match the mock values
- [ ] 7.12 Verify ≥95% line and branch coverage on `RichTextParser.cpp` including all attribute validation branches, the XSS sanitizer, and the cache hit/miss paths

## 8. Documentation

- [ ] 8.1 Update `docs/specs/ui-system.md`: document the full markup tag allow-list with examples, the XSS security model, the `set_rich_text` / `get_rich_text` / `measure_rich_text` Lua API signatures, and localisation integration pattern
- [ ] 8.2 Update `docs/specs/asset-pipeline.md`: document `asset://` URI scheme for `[img]` tags, data-URI cache behavior, and supported image formats
- [ ] 8.3 Update `docs/specs/localization.md`: document how rich markup in localisation CSV strings interacts with `ctx.ui:set_rich_text` and BiDi text rendering
