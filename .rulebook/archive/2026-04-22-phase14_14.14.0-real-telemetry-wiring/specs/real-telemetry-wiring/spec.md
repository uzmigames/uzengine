# real-telemetry-wiring

## ADDED Requirements

### Requirement: UpdateTelemetry forwards to the status bar

`VfxEditorOverlay::UpdateTelemetry(fps, drawCalls, vramUsedMb,
vramTotalMb)` SHALL call the matching status-bar setters so the
chips reflect the host-provided values.

#### Scenario: FPS chip tint follows the provided value

Given `UpdateTelemetry(62.0f, 0u, 0.0f, 0.0f)` was called
When the next render runs
Then `m_statusBar.Fps()` equals 62 and the chip tints
`stateSuccess` (>= 55).

### Requirement: EMA filter produces stable FPS

Demo smoothing SHALL use `ema += (instant - ema) * alpha` where
`alpha = dt / (dt + 0.1f)` so a 10 % step in dt produces
less-than-10 % jump in the smoothed FPS.

#### Scenario: Single frame hitch smooths out

Given `m_fpsSmoothed = 60.0f`
When `OnUpdate(dt=0.1f)` runs once (instantaneous FPS 10)
Then the EMA after one update equals approximately
`60 + (10 - 60) * 0.5 = 35` (still above the error threshold,
stays in the warning colour not error).
