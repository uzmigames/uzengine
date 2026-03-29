# Proposal: phase6-6.4.0-behavior-trees

## Why
NPCs need decision-making logic. Behavior trees are the industry standard (UE5, Unity DOTS, Halo) for AI — they compose complex behaviors from simple nodes (Sequence, Selector, decorators, services). Combined with navigation (phase6-6.3.0) and a blackboard (per-entity key-value store), behavior trees enable patrol routes, combat AI, and any goal-driven NPC behavior. Data-driven .uzbt format enables hot-reload and visual editing.

## What Changes
- BT runtime: Sequence, Selector, Parallel composite nodes
- Decorator nodes: Cooldown, Loop, Conditional, ForceSuccess, ForceFailure
- Service nodes: background polling (update blackboard at interval)
- Blackboard: per-entity key-value store (float, int, bool, Vec3, entity handle)
- Built-in action nodes: MoveTo, Wait, PlayAnimation, PlaySound, SetBlackboard
- .uzbt JSON format for tree definitions (hot-reloadable)
- Lua custom nodes: register action/condition nodes from Lua
- Lua API: uz.bt.set_tree, set_blackboard, get_blackboard

## Impact
- New directory: `runtime/engine/ai/`
- Depends on: Navigation (MoveTo), Animation (PlayAnimation), Audio (PlaySound)
- Spec: partial (needs full spec)
- Breaking change: NO
