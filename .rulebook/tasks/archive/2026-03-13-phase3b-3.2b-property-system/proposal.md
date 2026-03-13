# Proposal: 3.2b — Property System (MVP)

## Why
Hard gate for scene loading. Reflection enables serialization, inspector UI, and hot-reload. Without it, components can't be saved/loaded from .uzscene files or displayed in the editor inspector.

## What Changes
- ClassRegistry: type registration + lookup by name
- UZ_PROPERTY macro: field metadata (name, type, offset)
- UZ_COMPONENT macro: auto-register component in ClassRegistry
- JSON serializer/deserializer for components
- Runtime/Editor split: RuntimePropertyInfo vs EditorPropertyMeta

## Impact
- Affected specs: `docs/engine/21-property-system/`
- Affected code: `runtime/core/reflection/`
- Breaking change: NO (new system, but all components must add UZ_PROPERTY macros)
- User benefit: Scene serialization, inspector editing, hot-reload of component data
