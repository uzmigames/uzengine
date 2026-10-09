# tree-widget-overview-integration

## ADDED Requirements

### Requirement: Emitter tree root collapses children

The overlay's emitter tree root node MUST honour its `expanded`
flag — when `false`, child emitter rows SHALL NOT appear in the
computed layout.

#### Scenario: Collapsed root hides emitters

Given the overlay is bound with three emitter names
When `m_emitterRootExpanded = false` and the tree is rebuilt
Then `VfxTreeWidget::ComputeLayout` emits exactly one row
(the root) inside the tree's bounds.

### Requirement: Expand-arrow click toggles the root

Clicking the expand arrow on the root row MUST flip
`m_emitterRootExpanded` without firing `onSelectEmitter`.

#### Scenario: Arrow click does not select

Given the overlay is rendering the emitter tree
When the cursor clicks inside the arrow's 16x16 hit region on the
root row
Then `m_emitterRootExpanded` toggles and `onSelectEmitter` does
NOT fire.
