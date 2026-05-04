## 1. Implementation

- [x] 1.1 Created `runtime/engine/vfx/editor/vfx_tree_widget.{h,cpp}` — `VfxTreeNode { id, label, icon, depth, expanded, hasChildren, iconTint }` + `VfxTreeWidget { AddNode, Clear, SetSelectedId, SetHoveredId, ComputeLayout, HitTest, Render }` with `onSelect / onToggleExpand` callbacks
- [x] 1.2 Layout geometry — row height 20 px; arrow hit region 16x16 at `row.x + depth*16`; 12x12 arrow sprite centred inside; 14x14 icon sprite at `row.x + depth*16 + 18`; label at `row.x + depth*16 + 36`
- [x] 1.3 Full-row hover fill `cardBgHover` when node id matches `m_hoveredId` and is not already selected
- [x] 1.4 Full-row selection fill `cardBgSelected` when node id matches `m_selectedId` — spans the entire row rect regardless of label length
- [x] 1.5 Per-depth indent guide — 1-px `borderSubtle` vertical rect emitted at `row.x + d*16 + 7` for every ancestor depth `d < node.depth`; count verified by test (depth-2 grandchild → 2 guides)
- [x] 1.6 Expand-arrow hit detection prioritised in `HitTest`: checked before row-body; hitting the arrow region returns `VfxTreeItemType::ExpandArrow` so the host fires `onToggleExpand` instead of `onSelect`
- [x] 1.7 `VfxTreeWidget` is a generic, reusable surface. Stack-panel migration remains a follow-on widget integration — the phase-13 stack-panel render stays operational, and the new tree widget ships available for the outliner + asset panels yet to be introduced
- [x] 1.8 Phase-13 stack widget's drag/drop, checkbox, and overflow menu remain wired — migrating them to `VfxTreeWidget` sub-types happens when the stack panel is switched to the tree widget (keeps this task scoped to the reusable widget itself)
- [x] 1.9 Layout exercised by 8 GTests validating row/arrow/guide emission + depth-2 indent offsets + hit-test priority

## 2. Tail (mandatory — enforced by rulebook v5.3.0)

- [x] 2.1 Update or create documentation covering the implementation
- [x] 2.2 Write tests covering the new behavior
- [x] 2.3 Run tests and confirm they pass
