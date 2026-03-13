# Content Validation — Schema Checking & Automated Testing

> **Status**: Planned (Phase 2–3)
> **Source**: `engine/ai/validator.h`, `engine/ai/validator.cpp`
> **References**: JSON Schema (RFC 7020), nlohmann/json-schema validator

## Purpose

Generated content from LLMs must be validated before use. The ContentValidator ensures that AI-generated `.uzlevel`, `.uzdialogue`, `.uzquest`, and `.uzcharacter` files conform to schema, satisfy game rules, pass automated tests (e.g., "Is this level completable?"), and contain valid entity references. Invalid content is rejected with detailed error messages suitable for re-prompting the LLM.

## Architecture

```
Generated Content (JSON)
    │
    ├─ Schema Validation
    │  ├─ Field types match spec
    │  ├─ Required fields present
    │  ├─ No unknown properties
    │  └─ Nested object structure correct
    │
    ├─ Reference Resolution
    │  ├─ All entity IDs resolve
    │  ├─ All asset paths exist (in VFS or .import/)
    │  ├─ All dialogue branches reachable
    │  └─ No circular dependencies
    │
    ├─ Constraint Checking
    │  ├─ Numeric ranges (health > 0, positions finite)
    │  ├─ Enum validity (entity classes exist)
    │  ├─ Trigger conditions valid
    │  └─ Reward totals reasonable
    │
    ├─ Automated Testing
    │  ├─ Simulate playthrough
    │  ├─ Check level completability
    │  ├─ Verify no softlocks
    │  └─ Measure difficulty metrics
    │
    └─ If all pass: Write to VFS, hot-reload
       If any fail: Collect errors, suggest fixes to LLM
```

## JSON Schema Definitions

Each asset type has a corresponding JSON schema file (`.uzschema`):

### Level Schema

```json
{
    "$schema": "http://json-schema.org/draft-07/schema",
    "$id": "https://uzengine.io/schema/level.json",
    "title": "UzEngine Level (.uzlevel)",
    "type": "object",
    "required": ["format", "version", "name", "entities", "environment"],

    "properties": {
        "format": {
            "const": "uzlevel",
            "description": "File format identifier"
        },
        "version": {
            "type": "integer",
            "minimum": 1,
            "description": "Format version for migration"
        },
        "name": {
            "type": "string",
            "minLength": 1,
            "maxLength": 255,
            "description": "Human-readable level name"
        },
        "description": {
            "type": "string",
            "description": "Designer notes (optional)"
        },

        "environment": {
            "type": "object",
            "properties": {
                "sky": { "type": "string", "pattern": "^content/.*\\.uzskybox$" },
                "fog": {
                    "type": "object",
                    "properties": {
                        "density": { "type": "number", "minimum": 0, "maximum": 1 },
                        "color": { "$ref": "#/definitions/color_rgb" }
                    }
                },
                "lighting": {
                    "type": "object",
                    "properties": {
                        "directional": { "$ref": "#/definitions/color_rgb" },
                        "intensity": { "type": "number", "minimum": 0 }
                    }
                }
            }
        },

        "entities": {
            "type": "array",
            "minItems": 1,
            "items": { "$ref": "#/definitions/entity" }
        },

        "encounters": {
            "type": "array",
            "items": { "$ref": "#/definitions/encounter" }
        },

        "nav_mesh": {
            "type": "string",
            "pattern": "^content/.*\\.uznavmesh$",
            "description": "Path to navigation mesh asset"
        }
    },

    "definitions": {
        "color_rgb": {
            "type": "array",
            "items": { "type": "number", "minimum": 0, "maximum": 1 },
            "minItems": 3,
            "maxItems": 3
        },

        "vector3": {
            "type": "array",
            "items": { "type": "number" },
            "minItems": 3,
            "maxItems": 3
        },

        "entity": {
            "type": "object",
            "required": ["name", "class", "position"],
            "properties": {
                "name": {
                    "type": "string",
                    "pattern": "^[a-zA-Z_][a-zA-Z0-9_]*$"
                },
                "class": {
                    "type": "string",
                    "enum": ["SpawnPoint", "EnemyCharacter", "Container", "Door", "Trigger", "Light", "Camera", "Custom"]
                },
                "position": { "$ref": "#/definitions/vector3" },
                "rotation": {
                    "type": "array",
                    "items": { "type": "number" },
                    "minItems": 3,
                    "maxItems": 3,
                    "description": "Euler angles (degrees)"
                },
                "scale": {
                    "type": "array",
                    "items": { "type": "number", "minimum": 0.001 },
                    "minItems": 3,
                    "maxItems": 3
                },
                "properties": { "type": "object" }
            }
        },

        "encounter": {
            "type": "object",
            "required": ["id", "type"],
            "properties": {
                "id": { "type": "string" },
                "type": { "enum": ["combat", "puzzle", "social", "environmental"] },
                "enemies": { "type": "array", "items": { "type": "string" } },
                "reward_xp": { "type": "integer", "minimum": 0 }
            }
        }
    }
}
```

### Dialogue Schema (Simplified)

```json
{
    "$schema": "http://json-schema.org/draft-07/schema",
    "$id": "https://uzengine.io/schema/dialogue.json",
    "title": "UzEngine Dialogue Tree (.uzdialogue)",
    "type": "object",
    "required": ["format", "version", "character", "branches"],

    "properties": {
        "branches": {
            "type": "array",
            "items": {
                "type": "object",
                "required": ["id", "npc_line"],
                "properties": {
                    "id": { "type": "string" },
                    "npc_line": { "type": "string" },
                    "options": {
                        "type": ["array", "null"],
                        "items": {
                            "type": "object",
                            "required": ["id", "player_line"],
                            "properties": {
                                "next_branch": { "type": ["string", "null"] }
                            }
                        }
                    }
                }
            }
        }
    }
}
```

## Validator API

```cpp
/// Content validator — schema, references, constraints, automated tests.
class ContentValidator {
public:
    /// Result of validation.
    struct Result {
        bool is_valid = true;
        std::vector<ValidationError> errors;
        std::vector<ValidationWarning> warnings;

        operator bool() const { return is_valid; }
    };

    /// Validate level JSON against .uzlevel schema and game rules.
    /// Checks: entity references exist, positions finite, no duplicate IDs, etc.
    Result ValidateLevel(const nlohmann::json& level_json, const GameAssetRegistry& registry);

    /// Validate dialogue tree for consistency.
    /// Checks: no circular branches, all referenced branches exist, no orphaned nodes.
    Result ValidateDialogueTree(const nlohmann::json& dialogue_json);

    /// Validate quest chain for coherence.
    /// Checks: stages have valid objectives, rewards are reasonable, completion conditions are satisfiable.
    Result ValidateQuest(const nlohmann::json& quest_json, const GameAssetRegistry& registry);

    /// Validate character definition for balance.
    /// Checks: stats in reasonable ranges, equipment referenced entities exist, AI behavior is valid.
    Result ValidateCharacter(const nlohmann::json& char_json, const GameAssetRegistry& registry);

    /// Run automated test: simulate playthrough, check completability.
    /// Only for levels; simulates enemy spawning, player progression, goal achievement.
    Result AutomatedPlaytest(const LevelAsset& level, uint32_t num_simulations = 3);

    /// Get detailed diagnostics (can be fed back to LLM for re-generation).
    std::string GetDiagnostics(const Result& result);
};

/// Single validation error with location and remediation hint.
struct ValidationError {
    int line_number = -1;           // Line in JSON file (if available)
    std::string json_path;          // JSON path: "entities[0].position" or "branches[2].options[1].next_branch"
    std::string message;            // Human & LLM-readable error
    Severity severity;              // Error, Critical
    std::string suggestion;         // How to fix it (e.g., "Position must be finite, got [inf, 0, 0]")
    std::string referenced_entity;  // If error is about missing reference, which entity
};

struct ValidationWarning {
    std::string json_path;
    std::string message;
    std::string suggestion;         // Optional improvement hint for LLM
};

enum class Severity { Info, Warning, Error, Critical };
```

## Schema Validation

```cpp
/// Load and cache JSON schemas for each asset type.
class SchemaValidator {
public:
    /// Load schema from disk (.uzschema files).
    static void Initialize(StringView schema_dir = "engine/schemas/");

    /// Validate JSON object against schema.
    /// Returns detailed errors if validation fails.
    ContentValidator::Result Validate(
        const nlohmann::json& data,
        StringView schema_name  // "level", "dialogue", "quest", "character"
    );

private:
    std::map<std::string, nlohmann::json_schema::json_schema> schemas_;
};
```

## Reference Resolution

Ensures all asset paths and entity references are valid:

```cpp
/// Resolve and validate all references in generated content.
class ReferenceResolver {
public:
    /// Check all asset paths in content exist in VFS.
    /// Returns list of broken references.
    std::vector<ValidationError> ResolvePaths(
        const nlohmann::json& content,
        const AssetRegistry& registry
    );

    /// Check all entity references in encounters, dialogue conditions, etc. exist.
    std::vector<ValidationError> ResolveEntityReferences(
        const nlohmann::json& content,
        const LevelAsset& level
    );

    /// Check dialogue branches form a DAG (directed acyclic graph), no infinite loops.
    std::vector<ValidationError> ValidateDialogueTables(
        const nlohmann::json& dialogue_content
    );

    /// Validate condition expressions (e.g., "player.level >= 5").
    /// Checks: referenced variables exist in game state, operators are valid.
    std::vector<ValidationError> ValidateConditions(
        const nlohmann::json& content,
        const GameContext& context
    );
};

/// Parse and validate a condition expression.
class ConditionEvaluator {
public:
    /// Parse "player.reputation >= 50" → check syntax, variable names, operators.
    bool ValidateSyntax(StringView condition, std::string& error_message);

    /// Evaluate condition against current game state at runtime.
    bool Evaluate(StringView condition, const GameState& state);
};
```

## Constraint Checking

```cpp
/// Check generated content against game balance rules and constraints.
class ConstraintChecker {
public:
    /// Check numeric bounds (health, damage, XP rewards, etc.).
    std::vector<ValidationError> CheckBounds(const nlohmann::json& content, const GameRules& rules);

    /// Check probability distributions (encounter spawn rates, loot drop chances).
    std::vector<ValidationError> CheckProbabilities(const nlohmann::json& content);

    /// Check difficulty scaling (enemy levels match player level, treasure matches difficulty).
    std::vector<ValidationError> CheckDifficultyBalance(
        const LevelAsset& level,
        const GameContext& context
    );

    /// Check economy (total gold reward, item rarity distribution).
    std::vector<ValidationError> CheckEconomy(const LevelAsset& level, const GameRules& rules);
};

/// Game-specific balance rules that validators use.
struct GameRules {
    struct BoundsRule {
        std::string field;      // "health", "damage", "xp_reward"
        float min_value, max_value;
    };

    std::vector<BoundsRule> bounds_rules;
    float max_total_reward_xp = 10000;
    float max_treasure_value = 5000;
    uint32_t max_enemies_per_encounter = 10;
};
```

## Automated Testing

```cpp
/// Simulate playthrough to detect softlocks and balance issues.
class AutomatedPlaytest {
public:
    /// Simulate player progression through level N times.
    /// Tracks: completion rate, average time, difficulty variance, softlock probability.
    struct PlaytestResult {
        uint32_t completion_attempts = 0;
        uint32_t completion_successes = 0;
        float completion_rate = 0.0f;      // Successes / Attempts
        float average_time_seconds = 0.0f;
        std::vector<std::string> detected_issues;  // Softlocks, unreachable areas, etc.
    };

    PlaytestResult Run(
        const LevelAsset& level,
        uint32_t num_simulations = 5,
        uint32_t random_seed = 0
    );

private:
    /// Simulate one player run through level.
    struct SimulatedRun {
        bool success = false;
        float elapsed_time = 0;
        std::vector<std::string> events;  // "spawned_enemy", "opened_chest", "reached_goal"
        std::vector<std::string> warnings;
    };

    SimulatedRun SimulateSingleRun(const LevelAsset& level, uint32_t random_seed);

    /// Check for softlocks: unreachable goal, no valid path, etc.
    std::vector<std::string> DetectSoftlocks(const LevelAsset& level);

    /// Check for overpowered encounters (enemy damage too high relative to player defenses).
    std::vector<std::string> DetectBalanceIssues(const LevelAsset& level);
};
```

## Error Collection & Reporting

```cpp
/// Collect all validation errors and format for human & LLM consumption.
class ValidationReport {
public:
    /// Generate human-readable error report with JSON paths and suggestions.
    std::string AsMarkdown() const;

    /// Generate compact report suitable for feeding back to LLM as prompt.
    std::string AsLLMFeedback() const;

    /// Generate detailed JSON report for debugging tools.
    nlohmann::json AsJSON() const;

    /// Example output:
    // Validation Report for: generated_level_01.uzlevel
    // ==================================================
    //
    // CRITICAL ERRORS (2):
    // - entities[3].position: Position must be finite, got [inf, 0, 0]
    //   Suggestion: Replace inf with a numeric value (e.g., 10.5)
    //
    // - encounters[0].enemies[2]: Entity "boss_dragon" not found in entities list
    //   Suggestion: Either add a "boss_dragon" entity or use an existing entity name
    //
    // WARNINGS (1):
    // - entities[0]: Spawnpoint too close to first enemy (distance: 2.5m)
    //   Suggestion: Move spawnpoint at least 5m away for fair difficulty
    //
    // PASS: Reference resolution, dialogue consistency
    // FAIL: Constraint checking (1 issue), Automated playtesting (completion rate: 20%)

    void AddError(const ValidationError& error);
    void AddWarning(const ValidationWarning& warning);
    void AddPassCheck(StringView check_name);
    void AddFailCheck(StringView check_name, StringView reason);

    const std::vector<ValidationError>& GetErrors() const { return errors_; }
    const std::vector<ValidationWarning>& GetWarnings() const { return warnings_; }
    bool IsValid() const { return errors_.empty(); }

private:
    std::vector<ValidationError> errors_;
    std::vector<ValidationWarning> warnings_;
    std::map<std::string, bool> check_results_;  // "schema_check" → true
};
```

## Integration with ContentGenerator

After LLM generation completes, validators run automatically:

```cpp
/// Full pipeline: generate → validate → hot-reload.
AsyncHandle<LevelAsset> ContentGenerator::GenerateLevel(StringView prompt, ...) {
    // 1. Call LLM
    auto llm_response = g_provider->CallStream(...);

    // 2. Validate result
    ContentValidator validator;
    auto validation = validator.ValidateLevel(llm_response, g_asset_registry);

    if (!validation) {
        // 3. If failed: collect errors, suggest re-prompt
        auto feedback = validation.AsLLMFeedback();
        UZ_LOG(AI, Error, "Generated level failed validation:\n{}", feedback);

        // Auto-retry: regenerate with error hints
        auto retry_prompt = fmt::format(
            "The previous level had these issues:\n{}\n\n"
            "Please fix them in the new level generation.",
            feedback
        );
        return GenerateLevel(retry_prompt, ...);  // Recursive with hints
    }

    // 4. If passed: write to VFS, hot-reload
    WriteToVFS(llm_response, output_path);
    FileWatcher::Trigger(output_path);  // Notifies asset system to reload

    return validation;
}
```

## Comparison

| Validator Feature | UE5 | Godot | Unity | **UzEngine** |
|---|---|---|---|---|
| **Schema validation** | Via DataTable validation | Manually in GDScript | None | **JSON Schema + custom rules** |
| **Reference checking** | Via FAssetRegistry | Via Godot resource load | Via AssetDatabase | **Reference resolver** |
| **Automated testing** | Functional test suite (manual) | None | None | **AutomatedPlaytest** |
| **Error reporting** | Message log | Error console | Console | **Markdown + LLM-friendly format** |
| **Re-prompt support** | No | No | No | **Yes (feedback loop)** |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | SchemaValidator (JSON Schema validation), basic reference resolver, constraint checker (bounds only) |
| **Phase 3** | AutomatedPlaytest, detailed error reporting, ConditionEvaluator, economy checker, integration with ContentGenerator retry logic |
| **Phase 4** | Advanced constraint checking (probability distributions), difficulty curve analysis, semantic validation (does enemy placement make narrative sense?) |
