# Proposal: phase2-inspector-color-resource-pickers

## Why

The Phase 1 InspectorPanel supports `string`, `float`, `int`, `bool`, and `vec3` property
types. Two property types were explicitly deferred to Phase 2:

1. **Color properties** — material albedo, emissive, fog color, etc. need a proper color picker
   (`<input type="color">`) with hex↔float [0..1] conversion, since the engine uses float RGB.

2. **Resource path properties** — mesh paths, texture paths, material paths, shader paths need
   a text field + file-picker button so artists can browse to assets without typing raw paths.

These widgets are required before the Inspector can be used for real asset authoring workflows.

## What Changes

- `src/inspector-panel.ts`: extend `PropertyType` with `'color'` and `'resource'`; add rendering
  in `_renderWidget()`; add color conversion logic in the webview script
- `src/extension.ts`: register `uzengine.browseResource` command
- `package.json`: add `uzengine.browseResource` command declaration
- `src/__tests__/inspector-panel.test.ts`: new test cases for color and resource widgets
- `src/__tests__/extension.test.ts`: test new command registration

## Impact

- Affected specs: `.rulebook/specs/` (inspector panel spec)
- Affected code: `src/inspector-panel.ts`, `src/extension.ts`, `package.json`
- Breaking change: NO — additive extension of existing `PropertyType` union
- User benefit: Artists can visually pick colors and browse for asset paths directly in VSCode
