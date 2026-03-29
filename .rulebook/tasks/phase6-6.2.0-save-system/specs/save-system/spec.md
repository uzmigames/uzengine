# Save System — Specification

## ADDED Requirements

### Requirement: Save/Load Slots
The save system SHALL provide slot-based save/load with metadata.

#### Scenario: Save to Slot
Given the game state with 50 entities
When SaveManager::Save(slot=1, "My Save") is called
Then a .uzsave file SHALL be written to `%APPDATA%/UzEngine/<project>/saves/slot_1.uzsave`
And metadata SHALL include: timestamp, scene_name, display_name, play_time
And a thumbnail SHALL be captured to slot_1.uzsave.jpg (320x180)

#### Scenario: Load from Slot
Given a valid slot_1.uzsave
When SaveManager::Load(slot=1) is called
Then the current scene SHALL be unloaded
And entity transforms SHALL be restored from the save
And Lua script instance tables SHALL be deserialized
And game.dll state SHALL be restored via uz_game_load_state()

#### Scenario: List Slots
Given saves in slots 1, 3, 5
When SaveManager::ListSlots() is called
Then the result SHALL contain metadata for slots 1, 3, 5
And empty slots SHALL NOT appear in the list

### Requirement: Autosave
The save system SHALL automatically save at configurable intervals.

#### Scenario: Autosave Trigger
Given autosave_interval=300 (5 minutes)
When 300 seconds of play time have elapsed
Then an autosave SHALL be written to slot -1
And rotating autosave slots (-1 to -3) SHALL cycle (oldest overwritten)

### Requirement: Lua Save API
The scripting system SHALL expose save/load to Lua.

#### Scenario: Lua Save
Given a Lua script calling uz.saves.save(1, "Checkpoint")
Then SaveManager::Save(1, "Checkpoint") SHALL be called
And the result SHALL indicate success or failure
