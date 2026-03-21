# Lua script error handling: mark started=true on load failure to prevent retry spam
**Source**: manual
**Date**: 2026-03-15
**Tags**: lua, scripting, error-handling, resilience, lifecycle
When a Lua script fails to load (file not found, syntax error), the CScript component is marked started=true even though on_start never ran successfully. This prevents the ScriptSystem from retrying the load every frame (which would spam error logs at 60+ Hz). Runtime errors during on_update are logged but the entity continues receiving updates. on_stop is always called during shutdown (protected call). No Lua error can crash the engine — all calls use lua_pcall.