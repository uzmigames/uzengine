# content-browser-modal

## ADDED Requirements

### Requirement: Modal opens centered

`OpenContentBrowser()` SHALL center a 720x480 rect on the current
screen + set `m_contentBrowserOpen = true`.

#### Scenario: Default geometry at 1280x720

Given the overlay's screen dimensions are 1280x720
When `OpenContentBrowser()` is invoked
Then `m_contentBrowserBounds` equals `{280, 120, 720, 480}` and
`m_contentBrowserOpen` is true.

### Requirement: Close dismisses the modal

`CloseContentBrowser()` MUST set `m_contentBrowserOpen = false`
without touching the tile list so a subsequent open restores the
previous content.

#### Scenario: Reopen preserves tiles

Given the modal has been opened and populated
When `CloseContentBrowser()` then `OpenContentBrowser()` run
Then the widget's `Count()` remains the same (tiles persist).
