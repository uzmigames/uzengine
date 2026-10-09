# ue5-outliner-tree

## ADDED Requirements

### Requirement: Per-depth indentation

Each tree node SHALL indent its expand arrow by `depth * 16 px`
from the row's left edge. Subsequent visual elements (type icon,
label) shift by the same offset so every node at the same depth
aligns vertically.

#### Scenario: Depth-2 node offsets correctly

Given a tree node with `depth = 2`
When the widget renders the node row at `row.x = 100`
Then the expand arrow renders at x ∈ `[134, 146]` (100 + 2*16 + 2
to + 14), not at `[102, 114]`.

### Requirement: Full-row selection highlight

When a node is selected, the widget SHALL fill the entire row
rectangle from `row.x` to `row.x + row.w` with `cardBgSelected`,
regardless of the label length.

#### Scenario: Short label still fills the whole row

Given a selected node with a 3-character label and `row.w = 300`
When the render runs
Then the captured rect for the selection fill has width 300 and
color `cardBgSelected`.

### Requirement: Expand arrow is a separate hit region

A 16x16 hit region at `[row.x + depth*16, row.y + 2, 16, 16]`
SHALL fire `onToggleExpand` instead of `onSelect` when clicked.

#### Scenario: Arrow click toggles expand

Given a collapsed node at depth 0 with `row.x = 0, row.y = 0`
When `HitTest(8, 10)` is called and click is processed
Then `onToggleExpand(nodeId)` fires and `onSelect` does NOT fire.
