## 1. Prerequisites

- [ ] 1.1 Confirm `phase2-ui-system` is complete — custom element base class and UIPass exist
- [ ] 1.2 Confirm `phase1-ui-gamepad-navigation` is complete — `data-focusable` and `FocusManager` work
- [ ] 1.3 Read `docs/specs/ui-system.md` — understand custom element lifecycle (`connectedCallback`, `attributeChangedCallback`, `disconnectedCallback`)
- [ ] 1.4 Define CSS variable naming convention: `--uz-{widget}-{property}` (e.g., `--uz-button-bg`, `--uz-slider-track-color`)

## 2. Widget Base Infrastructure

- [ ] 2.1 Create `src/ui/widgets/uz_widget_base.h` — `UzWidgetBase` base class for all `uz-*` elements
- [ ] 2.2 Implement `UzWidgetBase::render_shadow_dom(html)` — inserts shadow DOM content
- [ ] 2.3 Implement `UzWidgetBase::emit(event_name, detail_table)` — dispatches CustomEvent on element
- [ ] 2.4 Implement `UzWidgetBase::apply_focus()` — calls `FocusManager::set_focus(this->focus_id)`
- [ ] 2.5 Create `src/ui/widgets/uz_theme.css` — default CSS variable values for all widgets
- [ ] 2.6 Ensure `uz_theme.css` is injected as first stylesheet in every UIDocument

## 3. uz-button

- [ ] 3.1 Create `src/ui/widgets/uz_button.h/cpp`
- [ ] 3.2 Attributes: `label`, `disabled`, `variant` (`primary`/`secondary`/`danger`)
- [ ] 3.3 Render: `<button data-focusable role="button" aria-label="{label}">{label}</button>`
- [ ] 3.4 Events: `click`, `focus`, `blur`
- [ ] 3.5 CSS vars: `--uz-button-bg`, `--uz-button-fg`, `--uz-button-hover-bg`, `--uz-button-disabled-opacity`
- [ ] 3.6 Gamepad: auto-`data-focusable`; A/Enter triggers `click`

## 4. uz-slider

- [ ] 4.1 Create `src/ui/widgets/uz_slider.h/cpp`
- [ ] 4.2 Attributes: `min`, `max`, `step`, `value`, `disabled`, `label`
- [ ] 4.3 Render: `<div role="slider" aria-valuemin aria-valuemax aria-valuenow data-focusable>` + track + thumb
- [ ] 4.4 Events: `change` (value changed), `input` (during drag)
- [ ] 4.5 CSS vars: `--uz-slider-track-color`, `--uz-slider-fill-color`, `--uz-slider-thumb-color`, `--uz-slider-thumb-size`
- [ ] 4.6 Gamepad: left/right D-pad adjusts value by `step`; held for 300ms → auto-repeat at 10Hz

## 5. uz-checkbox

- [ ] 5.1 Create `src/ui/widgets/uz_checkbox.h/cpp`
- [ ] 5.2 Attributes: `label`, `checked`, `disabled`
- [ ] 5.3 Render: `<label><input type="checkbox" aria-checked data-focusable>{label}</label>`
- [ ] 5.4 Events: `change` with `detail.checked: bool`
- [ ] 5.5 CSS vars: `--uz-checkbox-color`, `--uz-checkbox-check-color`, `--uz-checkbox-size`

## 6. uz-dropdown

- [ ] 6.1 Create `src/ui/widgets/uz_dropdown.h/cpp`
- [ ] 6.2 Attributes: `placeholder`, `value`, `disabled`; child `<uz-option value="">` elements define options
- [ ] 6.3 Render: collapsed button showing selected value; expanded listbox with options
- [ ] 6.4 ARIA: `role="combobox"`, `aria-expanded`, `aria-haspopup="listbox"`, option `role="option"` + `aria-selected`
- [ ] 6.5 Events: `change` with `detail.value`; `open`; `close`
- [ ] 6.6 CSS vars: `--uz-dropdown-bg`, `--uz-dropdown-border`, `--uz-dropdown-option-hover-bg`
- [ ] 6.7 Gamepad: A opens; D-pad navigates options; A selects; B closes without selection

## 7. uz-scroll-view

- [ ] 7.1 Create `src/ui/widgets/uz_scroll_view.h/cpp`
- [ ] 7.2 Attributes: `direction` (`vertical`/`horizontal`/`both`), `scroll-bar` (`auto`/`always`/`never`)
- [ ] 7.3 Render: clipping container with overflow hidden; custom scrollbar overlay
- [ ] 7.4 Events: `scroll` with `detail.scrollTop`, `detail.scrollLeft`
- [ ] 7.5 CSS vars: `--uz-scrollbar-color`, `--uz-scrollbar-track-color`, `--uz-scrollbar-width`
- [ ] 7.6 Gamepad: right stick Y scrolls vertically when scroll-view has focus

## 8. uz-list-view

- [ ] 8.1 Create `src/ui/widgets/uz_list_view.h/cpp`
- [ ] 8.2 Attributes: `selection-mode` (`none`/`single`/`multi`)
- [ ] 8.3 Render: `<ul role="listbox">` containing `uz-list-item` children
- [ ] 8.4 Events: `selection-change` with `detail.selected_indices[]`
- [ ] 8.5 Virtualized rendering: only render visible rows + 2 buffer rows (for long lists)
- [ ] 8.6 CSS vars: `--uz-list-item-height`, `--uz-list-selected-bg`, `--uz-list-hover-bg`
- [ ] 8.7 Gamepad: D-pad up/down moves selection; A toggles; focus ring on selected item

## 9. uz-modal

- [ ] 9.1 Create `src/ui/widgets/uz_modal.h/cpp`
- [ ] 9.2 Attributes: `open`, `title`, `close-on-backdrop`
- [ ] 9.3 Render: backdrop overlay + modal container; `role="dialog"`, `aria-modal="true"`, `aria-labelledby`
- [ ] 9.4 Events: `open`, `close`
- [ ] 9.5 Focus trap: while modal open, Tab cycles only within modal; B/Escape triggers close
- [ ] 9.6 CSS vars: `--uz-modal-bg`, `--uz-modal-backdrop-color`, `--uz-modal-border-radius`, `--uz-modal-max-width`

## 10. uz-toast

- [ ] 10.1 Create `src/ui/widgets/uz_toast.h/cpp`
- [ ] 10.2 Attributes: `message`, `type` (`info`/`success`/`warning`/`error`), `duration` (ms)
- [ ] 10.3 Render: fixed-position notification container; `role="alert"`, `aria-live="polite"`
- [ ] 10.4 Auto-dismiss after `duration` ms; slide-in/slide-out animation
- [ ] 10.5 CSS vars: `--uz-toast-bg-{type}`, `--uz-toast-fg`, `--uz-toast-border-radius`

## 11. uz-tab-bar

- [ ] 11.1 Create `src/ui/widgets/uz_tab_bar.h/cpp`
- [ ] 11.2 Attributes: `active-tab` (index); child `<uz-tab label="">` elements define tabs
- [ ] 11.3 Render: `<div role="tablist">` with `<button role="tab" aria-selected aria-controls>` children
- [ ] 11.4 Events: `tab-change` with `detail.index`
- [ ] 11.5 CSS vars: `--uz-tab-active-bg`, `--uz-tab-inactive-bg`, `--uz-tab-indicator-color`
- [ ] 11.6 Gamepad: left/right D-pad switches tabs when tab bar focused

## 12. uz-tooltip

- [ ] 12.1 Create `src/ui/widgets/uz_tooltip.h/cpp`
- [ ] 12.2 Attributes: `text`, `placement` (`top`/`bottom`/`left`/`right`)
- [ ] 12.3 Render: wraps child element; shows `role="tooltip"` popup on hover/focus-within
- [ ] 12.4 Delay: 500ms hover delay before show; immediate hide on mouse leave
- [ ] 12.5 CSS vars: `--uz-tooltip-bg`, `--uz-tooltip-fg`, `--uz-tooltip-border-radius`

## 13. Registration

- [ ] 13.1 Register all 10 widget types in `uz_ui_html.dll` initialization via custom element registry
- [ ] 13.2 Inject `uz_theme.css` as global stylesheet in `UIDocument` constructor

## 14. Testing

- [ ] 14.1 Unit test per widget: attributes set → shadow DOM renders expected structure
- [ ] 14.2 Unit test per widget: ARIA attributes present and correctly valued
- [ ] 14.3 Unit test per widget: CSS variables applied to rendered output
- [ ] 14.4 Unit test: `uz-button` click event fires when Enter pressed while focused
- [ ] 14.5 Unit test: `uz-slider` value clamped to [min, max] on gamepad input
- [ ] 14.6 Unit test: `uz-dropdown` opens on A, closes on B without selection
- [ ] 14.7 Unit test: `uz-modal` focus trap — Tab key stays within modal while `open`
- [ ] 14.8 Unit test: `uz-list-view` virtualization — only visible rows in DOM
- [ ] 14.9 Unit test: `uz-toast` auto-dismiss fires `close` event after `duration` ms
- [ ] 14.10 Integration test: compose `uz-modal` with `uz-button` cancel/confirm; full keyboard + gamepad flow

## 15. Documentation

- [ ] 15.1 Update `docs/specs/ui-system.md` — "Widget Library" section with all 10 widgets, attributes, events, CSS vars
- [ ] 15.2 Create `docs/guides/ui-widget-guide.md` — annotated HTML examples for each widget
