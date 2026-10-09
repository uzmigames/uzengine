# CVar registrations

## Purpose
Every CVar has one registration, so its default and range live in one place.

## ADDED Requirements

### Requirement: One registration per CVar
Every `UZ_CVAR_*` name SHALL be registered exactly once across the engine, tools and demos, in a
translation unit and never in a header; `scripts/test_cvar_registrations.py` SHALL audit this.

#### Scenario: The audit
Given the sources
When the audit runs
Then no name is registered twice and none in a header.

#### Scenario: The negative control
Given the fixture that registers a name twice
When the audit scans the fixture alone
Then it reports the duplicate.

#### Scenario: The SSR CVars
Given the reference scene with SSR on
When the SSR passes run with the single registrations
Then they run and cost as before.
