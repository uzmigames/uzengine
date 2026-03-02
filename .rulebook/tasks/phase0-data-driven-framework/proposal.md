# Proposal: phase0-data-driven-framework

## Why

Multiple subsystems — audio events, physics materials, entity templates, behavior trees, particle systems — each require their own data file format. Without a unified convention decided upfront, each subsystem invents its own parser, different key naming styles, different section headers, and different hot-reload hooks. This produces a fragmented codebase that is hard to extend and confusing for AI agents trying to generate valid data files.

Defining a shared data-driven framework spec before implementation locks in: the INI-like section syntax, required header fields, the `[meta]` block convention, importer registration, hot-reload protocol, and Lua access patterns. All subsequent subsystem tasks reference this spec instead of reinventing it.

## What Changes

- **No code changes** — this is a design-only task.
- **`docs/specs/data-driven-framework.md`** — new spec defining:
  - Shared text format grammar: `[section]`, `key = value`, `key[] = array item`, `#comments`
  - Required `[meta]` block fields: `type`, `version`, `uuid`
  - File type registry: `.uzevent`, `.uzphysmat`, `.uzentity`, `.uzflow`, `.uzbt`, `.uzfx`, `.uznavmesh`, `.uzterrain`
  - `IAssetImporter` C++ interface for registering file-type parsers
  - Hot-reload protocol: file watcher → `AssetImporter::reimport(path)` → registry invalidation → subscriber notification
  - Lua access pattern: `ctx.assets:load(path) -> userdata`
  - Error reporting: parse errors surfaced as VSCode diagnostics via IPC

## Impact

- Affected specs: `docs/specs/data-driven-framework.md` (new)
- Affected downstream: `phase2-audio-event-system`, `phase2-physics-materials`, `phase1-entity-templates`, `phase2-behavior-trees`, `phase3-particle-system`, `phase4-navmesh`
- Breaking change: NO — design only
- User benefit: consistent, AI-friendly data files across all subsystems; single hot-reload path for all data types
