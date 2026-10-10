# status-bar-notifications-wiring

## ADDED Requirements

### Requirement: PushStatus forwards to the status bar queue

`VfxEditorOverlay::PushStatus(msg, level)` SHALL call
`VfxStatusBarWidget::PushNotification(msg, level)` on the overlay's
bound status bar.

#### Scenario: PushStatus makes the queue non-empty

Given an overlay with an empty status-bar queue
When `PushStatus("Saved", VfxStatusLevel::Info)` is called
Then `m_statusBar.QueueSize()` equals 1 and the front entry's
`message` equals "Saved".

### Requirement: TickStatus drains auto-dismissed entries

`VfxEditorOverlay::TickStatus(dt)` SHALL forward `dt` to
`VfxStatusBarWidget::Tick`. When a notification has been on screen
for 3 s and `TickStatus(3.01)` is called, the queue MUST shrink
by one.

#### Scenario: Auto-dismiss on Tick

Given a queue with a single entry pushed at t=0
When `TickStatus(3.01)` is called
Then `m_statusBar.QueueSize()` equals 0.
