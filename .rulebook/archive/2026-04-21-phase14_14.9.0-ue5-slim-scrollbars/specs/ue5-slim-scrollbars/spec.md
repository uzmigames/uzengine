# ue5-slim-scrollbars

## ADDED Requirements

### Requirement: Slim 6-px idle width

`VfxScrollBarWidget::Render` SHALL emit a track of width 6 px
when neither `hovered` nor `pressed` is true, and 8 px otherwise.

#### Scenario: Idle bar is 6 px

Given `hovered = false` and `pressed = false`
When the widget renders inside a panel at `[0, 0, 240, 400]`
Then the track rect is `[234, 0, 6, 400]`.

### Requirement: Thumb proportional to content

Thumb height SHALL equal `max(20, panelHeight * min(1, panelHeight
/ contentHeight))`, clamped to at most `panelHeight`.

#### Scenario: Half-overflow content → half-height thumb

Given `panelRect.h = 400` and `contentHeight = 800`
When the widget renders at `scrollY = 0`
Then the thumb rect height == 200 (= 400 * 400 / 800).

#### Scenario: Tiny overflow still honours 20-px minimum

Given `panelRect.h = 400` and `contentHeight = 50000`
When the widget renders
Then the thumb rect height == 20 (the floor).

### Requirement: Thumb position tracks scrollY

When `scrollY > 0`, the thumb `y` SHALL equal `panelRect.y +
(scrollY / (contentHeight - panelRect.h)) * (panelRect.h - thumb.h)`.

#### Scenario: Half-scrolled thumb sits at mid-track

Given `panelRect = [0, 0, 240, 400]`, `contentHeight = 800`,
`scrollY = 200`
When the widget renders
Then the thumb y == `(200 / 400) * (400 - 200) = 100`.
