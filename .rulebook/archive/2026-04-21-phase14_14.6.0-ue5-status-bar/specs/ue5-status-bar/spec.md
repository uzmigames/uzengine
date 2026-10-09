# ue5-status-bar

## ADDED Requirements

### Requirement: FPS chip colour threshold

The FPS chip's value text SHALL be tinted `stateSuccess` when fps
>= 55, `stateWarning` when 30 <= fps < 55, and `stateError` when
fps < 30.

#### Scenario: 42 fps shows warning tint

Given `SetFps(42)` followed by `Render`
When the FPS chip value colour is sampled
Then it matches `stateWarning` within 1 LSB per channel.

### Requirement: Saved-state toggle

The saved-state chip SHALL render `● unsaved` tinted
`stateWarning` when `SetDirty(true)` was the last call, and
`✓ saved` tinted `stateSuccess` after `SetDirty(false)`.

#### Scenario: Dirty flag toggles chip text + colour

Given a clean editor showing "✓ saved" in `stateSuccess`
When `SetDirty(true)` is called and the next render runs
Then the chip renders "● unsaved" in `stateWarning`.

### Requirement: Transient notification auto-dismiss

`PushNotification(msg, level)` SHALL add an entry to a FIFO queue.
The oldest entry renders for 3 s then dismisses automatically; on
dismiss the next entry becomes active. The queue MUST cap at 8
entries — further pushes drop the oldest.

#### Scenario: Notification disappears after 3 seconds

Given an empty queue
When `PushNotification("Saved", Info)` fires at t=0
And the clock advances to t=3.1
Then the centre chip renders empty (no notification).
