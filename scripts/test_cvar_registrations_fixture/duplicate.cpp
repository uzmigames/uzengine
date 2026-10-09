// Negative control of scripts/test_cvar_registrations.py: a CVar registered twice with two defaults.
// Not compiled by any build; the audit reads it as text.
static auto* cv_FixtureA = UZ_CVAR_FLOAT("r.Fixture.Duplicate", 1.0f, "first registration", 0.0f, 2.0f);
static auto* cv_FixtureB = UZ_CVAR_FLOAT("r.Fixture.Duplicate", 0.5f, "second registration (the trap)", 0.0f, 2.0f);
