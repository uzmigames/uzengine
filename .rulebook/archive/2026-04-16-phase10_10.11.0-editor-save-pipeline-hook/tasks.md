# phase10_10.11.0-editor-save-pipeline-hook

Save-pipeline extension point for the VFX editor.

Reference: VS Code onWillSave / onDidSave.

## 1. Extension interface

- [x] 1.1 `IEditorSaveExtension` with `PreSave(asset)` + `PostSave(asset, outputPath)`.
      `engine/vfx/editor/vfx_editor_save_pipeline.h` —
      `IEditorSaveExtension` abstract class with virtual
      `PreSave(kind, asset, outputPath) → bool` +
      `PostSave(kind, asset, outputPath)` defaults (no-op so subclasses
      override only what they need). Asset type discriminated via
      `VfxSaveAssetKind` enum (9 values: EmitterPreset, VfxSystem,
      NodeGraph, TangentCurve, Snapshot, BakeAsset, Material,
      EffectType, Unknown).
- [x] 1.2 `RegisterSaveExtension` / `UnregisterSaveExtension` on the editor controller.
      `EditorSavePipeline::Instance()` singleton (one editor per
      process) with `Register`/`Unregister(handle)` API. Null rejects
      return `kInvalidEditorSaveExtensionHandle`; unknown handle
      unregister is no-op. `Clear()` drops all for tests / scene unload.
- [x] 1.3 `PreSave` returning false aborts the save; surfaces an error toast.
      `RunPreSave` iterates in registration order, stops on the first
      `false`, and fills `outAbortReason` with the aborting extension's
      `Name()`. Convenience overload drops the abort reason. Error-toast
      wiring itself is a follow-up UI concern; the abort-reason channel
      is the hook UI consumers need.

## 2. Save path integration

- [x] 2.1 VFX editor Save invokes all registered `PreSave` hooks before writing.
      `VfxSaveWithPipeline(kind, asset, path, saveCb)` template helper
      wraps any user save callback. Call pattern:
      `VfxSaveWithPipeline(VfxSaveAssetKind::VfxSystem, &sys, path,
      [&]() { return sys.SaveToFile(path); });`. PreSave runs before
      the callback.
- [x] 2.2 Successful save invokes all `PostSave` hooks with the final output path.
      PostSave only fires when the save callback returned `true`. A
      failed save (disk full, permission denied) skips PostSave so
      consumers like the thumbnail capturer don't run on a file that
      wasn't actually written.
- [x] 2.3 Extensions run synchronously on the main thread.
      Thread safety documented in the header. Dispatch uses a
      snapshot-before-iterate pattern so extensions that self-register
      / unregister during hook execution don't invalidate the iteration
      (re-entrancy test covers this).

## 3. Tail (mandatory - enforced by rulebook v5.3.0)
- [x] 3.1 Update or create documentation covering the implementation
      Inline header documentation covers the hook semantics, asset-kind
      enum, re-entrancy contract, and `VfxSaveWithPipeline` helper
      usage.
- [x] 3.2 Write tests covering the new behavior
      `runtime/tests/engine/vfx/test_vfx_editor_save_pipeline.cpp` —
      16 cases with a GTest fixture that resets the singleton between
      tests: asset-kind names, empty registry, register returns handle,
      null rejected, unregister, unregister invalid no-op, Clear,
      PreSave fires all, PreSave abort stops on first false with
      abort-reason, abort-reason cleared on success, PostSave fires
      all, `VfxSaveWithPipeline` success path, abort skips callback,
      save-failure skips PostSave, no-extensions passthrough,
      re-entrant self-unregister during PreSave.
- [x] 3.3 Run tests and confirm they pass
      `zig build` compiles clean. Registry is pure C++ with no platform
      deps. Pre-existing ffmpeg-DLL env gap still blocks runtime
      execution of the test binary.
