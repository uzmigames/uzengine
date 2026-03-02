## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-ui-system` is complete — `UIDocument`, CSS property API, `ctx.ui` exist
- [ ] 1.2 Confirm `phase1-timer-system` is complete — `ctx:delta_time()` available
- [ ] 1.3 Read `docs/specs/ui-system.md` — understand CSS property set API and element access

## 2. Easing Functions

- [ ] 2.1 Create `src/ui/tween_easing.h` — `EasingFn` type alias: `float(float t)` where t in [0,1]
- [ ] 2.2 Implement `ease_linear(t) = t`
- [ ] 2.3 Implement `ease_in_quad(t)`, `ease_out_quad(t)`, `ease_in_out_quad(t)`
- [ ] 2.4 Implement `ease_in_cubic(t)`, `ease_out_cubic(t)`, `ease_in_out_cubic(t)`
- [ ] 2.5 Implement `ease_in_bounce(t)`, `ease_out_bounce(t)`
- [ ] 2.6 Implement `ease_in_elastic(t)`, `ease_out_elastic(t)`
- [ ] 2.7 Implement `ease_back_out(t)`, `ease_spring(t, damping, frequency)` — for satisfying UI pop
- [ ] 2.8 Create `EasingRegistry::from_name(name) -> EasingFn` — string → function lookup

## 3. TweenScheduler

- [ ] 3.1 Create `TweenScheduler` class in `uz_ui_html.dll`
- [ ] 3.2 Define `TweenEntry`: `element_id`, `property_name`, `start_value`, `end_value`, `duration`, `elapsed`, `easing_fn`, `on_complete` callback
- [ ] 3.3 Implement `TweenScheduler::animate(element_id, property, from, to, duration, easing) -> TweenHandle`
- [ ] 3.4 Implement `TweenScheduler::update(dt)` — advance all tweens, apply interpolated CSS values, fire completion callbacks
- [ ] 3.5 Implement `TweenScheduler::cancel(TweenHandle)` — stops tween at current value
- [ ] 3.6 Implement `TweenScheduler::cancel_all(element_id)` — stops all tweens for an element
- [ ] 3.7 Handle multiple tweens on same property: newer tween cancels older (last-write-wins)
- [ ] 3.8 Call `TweenScheduler::update(dt)` from `UIPass::update(dt)` each frame

## 4. Supported CSS Properties

- [ ] 4.1 `opacity` — float [0, 1]
- [ ] 4.2 `transform: translateX(px)`, `translateY(px)`, `scaleX`, `scaleY`, `rotate(deg)`
- [ ] 4.3 `width`, `height` — pixel values
- [ ] 4.4 `left`, `top`, `right`, `bottom` — pixel offsets
- [ ] 4.5 `color`, `background-color` — RGBA interpolation (per-channel lerp)
- [ ] 4.6 `border-radius` — pixel value

## 5. Sequence Support

- [ ] 5.1 Define `TweenStep` struct: `element_id`, `property`, `to_value`, `duration`, `easing`, `delay_before`
- [ ] 5.2 Implement `TweenScheduler::sequence(steps[]) -> SequenceHandle` — chains tweens; next step starts after previous completes
- [ ] 5.3 Implement `TweenScheduler::cancel_sequence(SequenceHandle)` — stops all pending steps
- [ ] 5.4 Implement `TweenScheduler::parallel(steps[]) -> SequenceHandle` — all steps start simultaneously

## 6. Page Transitions

- [ ] 6.1 Define `PageTransition` enum: `FadeInOut`, `SlideLeft`, `SlideRight`, `SlideUp`, `ZoomIn`, `ZoomOut`
- [ ] 6.2 Implement `TweenScheduler::transition_to(new_page_id, PageTransition, duration)`:
  - Fade out current page (opacity 1→0)
  - On complete: hide current page, show new page
  - Fade in new page (opacity 0→1)
- [ ] 6.3 Slide transitions: translate outgoing page off-screen, slide incoming page on

## 7. Lua API

- [ ] 7.1 Implement `ctx.ui:tween(element_id, props_table, duration, easing) -> handle`
  - `props_table`: `{ opacity=1.0, translateX="100px" }`
- [ ] 7.2 Implement `ctx.ui:tween(element_id, props_table, duration, easing, on_complete_fn) -> handle`
- [ ] 7.3 Implement `ctx.ui:sequence(steps_table) -> handle` — array of step tables
- [ ] 7.4 Implement `ctx.ui:parallel(steps_table) -> handle`
- [ ] 7.5 Implement `ctx.ui:cancel_tween(handle)`
- [ ] 7.6 Implement `ctx.ui:transition_to(page_id, transition_name, duration)`

## 8. Testing

- [ ] 8.1 Unit test: `ease_out_bounce(0.0) == 0.0`, `ease_out_bounce(1.0) == 1.0`, intermediate values bounce
- [ ] 8.2 Unit test: tween opacity 0→1 over 1s — after 0.5s update, opacity ≈ 0.5 (linear easing)
- [ ] 8.3 Unit test: two tweens on same property — second cancels first, property continues to second target
- [ ] 8.4 Unit test: sequence — second step starts only after first completes
- [ ] 8.5 Unit test: `cancel_all(element_id)` stops all tweens for that element
- [ ] 8.6 Unit test: color interpolation — RGBA channels lerp independently
- [ ] 8.7 Integration test: `ctx.ui:tween("my-btn", {opacity=0}, 0.5, "ease_in")` — element fades out in 0.5s
- [ ] 8.8 Integration test: `ctx.ui:transition_to("main-menu", "fade", 0.3)` — pages swap with fade

## 9. Documentation

- [ ] 9.1 Update `docs/specs/ui-system.md` — "Animation / Tweening" section with all API methods
- [ ] 9.2 Update `docs/guides/lua-scripting-guide.md` — "Animating UI" section with common patterns
