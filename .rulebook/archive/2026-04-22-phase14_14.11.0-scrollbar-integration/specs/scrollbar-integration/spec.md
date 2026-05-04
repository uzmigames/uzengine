# scrollbar-integration

## ADDED Requirements

### Requirement: Stack panel scrollbar mirrors content overflow

When the stack panel's computed content height exceeds its bounds
height, `VfxEditorOverlay` SHALL render a `VfxScrollBar` docked at
the right edge of the stack panel rect.

#### Scenario: Scrollbar appears on overflow

Given a stack panel bounds of `{x,y,220,400}` with a content
height of 800 px
When `BuildNiagaraUI` renders the frame
Then the overlay emits a scrollbar track rect at
`{x + 220 - 6, y, 6, 400}` and a thumb at y equal to
`y + (scrollOffset / 400) * (400 - thumbH)`.

### Requirement: Mouse wheel routes to the scrollbar under the cursor

When the cursor is inside a scrollable panel and
`InputSystem::GetScrollDelta` is non-zero, the overlay MUST feed
that delta to the matching panel's `VfxScrollBar::Scroll` call.

#### Scenario: Wheel scroll moves the thumb

Given the cursor is over the stack panel and scrollDelta == -1
When the frame runs
Then `m_stackPanel.ScrollOffset` advances by 40 px (one notch).
