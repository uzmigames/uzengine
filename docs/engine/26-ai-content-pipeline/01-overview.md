# AI Content Pipeline — LLM-Friendly Authoring & Procedural Generation

> **Status**: Planned (Phase 2–4)
> **Source**: `engine/ai/`, `engine/ai/content_generator.h`, `engine/ai/dialogue_system.h`, `engine/ai/validator.h`
> **Design Philosophy**: All `.uz*` formats are human-readable text/JSON. No binary black boxes. LLMs can read, understand, and generate content.
> **References**: Custom design (AI-first, no direct engine reference)

## Purpose

UzEngine is the **first game engine designed from the ground up for autonomous AI development**. The AI Content Pipeline enables:

- **LLM-driven procedural generation** — Levels, dialogue trees, quests, narrative branching
- **AI-assisted asset creation** — Texture descriptions → generation via DALL-E / Stable Diffusion
- **Automated content validation** — Verify generated content against schema before runtime
- **Hot-reload of AI outputs** — Regenerate a quest, reload the game, see changes instantly
- **Transparent AI reasoning** — Every asset format exposes LLM-friendly structure

This is UzEngine's **unique selling point** and differentiator from Unity, Godot, and UE5.

## Architecture

```
LLM (ChatGPT / Claude / Local)
    │
    ├── Text prompt: "Create a forest level with 3 encounters"
    │
    ▼
Content Generator (content_generator.h)
    │
    ├── 1. Parse prompt + game context (current level, entities, etc.)
    ├── 2. Call LLM API (OpenAI / Anthropic / local)
    ├── 3. LLM generates output (JSON .uzlevel, .uzdialogue, .uzquest)
    ├── 4. Stream output to file (incremental, user sees progress)
    │
    ▼
Content Validator (validator.h)
    │
    ├── 1. Parse JSON/YAML
    ├── 2. Validate against schema (.uzlevel must have entities, spawnpoints, etc.)
    ├── 3. Check constraints (no negative sizes, valid entity references)
    ├── 4. Run automated tests (can level be completed? are all dialogue lines parseable?)
    │
    ▼
Runtime System
    │
    ├── Asset loader (FileWatcher watches generated files)
    ├── Hot-reload on change (editor re-imports asset, re-populates scene)
    └── Lua bindings (game code calls ctx.dialog.start_branch(), etc.)
```

## Core Concept: Text-Based Content Formats

Unlike binary-only content (FBX, PNG, .asset bundles), UzEngine's formats are **always readable and editable by humans and LLMs**:

### Levels (.uzlevel)

```json
{
    "format": "uzlevel",
    "version": 1,
    "name": "Forest Ruins Level 2",
    "description": "Ancient forest temple with 3 combat encounters",

    "environment": {
        "sky": "content/sky/forest_dawn.uzskybox",
        "fog": { "density": 0.05, "color": [0.8, 0.9, 1.0] },
        "lighting": { "directional": [0.9, 0.95, 1.0], "intensity": 1.2 }
    },

    "entities": [
        {
            "name": "player_spawn",
            "class": "SpawnPoint",
            "position": [0, 0, 0],
            "properties": { "team": "player" }
        },
        {
            "name": "enemy_1",
            "class": "EnemyCharacter",
            "position": [10, 0, 5],
            "properties": {
                "character_type": "goblin_archer",
                "level": 5,
                "dialogue_tree": "content/dialogue/goblin_greeting.uzdialogue"
            }
        },
        {
            "name": "treasure_chest",
            "class": "Container",
            "position": [25, 0, 10],
            "properties": {
                "locked": true,
                "difficulty": 3,
                "contents": [
                    { "item": "sword_iron", "quantity": 1 },
                    { "item": "gold_coins", "quantity": 50 }
                ]
            }
        }
    ],

    "encounters": [
        {
            "id": "combat_1",
            "type": "combat",
            "enemies": ["enemy_1"],
            "reward_xp": 100,
            "on_complete": "trigger_checkpoint_1"
        }
    ],

    "nav_mesh": "content/levels/level_02_navmesh.uznavmesh",
    "lighting_bake": { "quality": "medium", "resolution": 1024 }
}
```

### Dialogue Trees (.uzdialogue)

```json
{
    "format": "uzdialogue",
    "version": 1,
    "character": "goblin_archer",
    "language": "en",

    "branches": [
        {
            "id": "greeting",
            "npc_line": "Well, what have we here? A traveler lost in these woods?",
            "options": [
                {
                    "id": "option_friendly",
                    "player_line": "I mean no harm. I'm just passing through.",
                    "next_branch": "response_friendly"
                },
                {
                    "id": "option_hostile",
                    "player_line": "Get out of my way, monster!",
                    "next_branch": "response_hostile"
                },
                {
                    "id": "option_trade",
                    "player_line": "I heard you deal in rare goods?",
                    "next_branch": "response_trade",
                    "requires_condition": "player_has_trade_reputation"
                }
            ]
        },

        {
            "id": "response_friendly",
            "npc_line": "You seem honorable. Perhaps we can come to an arrangement?",
            "options": [
                {
                    "id": "accept_quest",
                    "player_line": "What kind of arrangement?",
                    "next_branch": "quest_offering",
                    "on_select_action": "start_quest(find_crystal)"
                },
                {
                    "id": "decline",
                    "player_line": "I must be going.",
                    "next_branch": null,
                    "on_select_action": "end_dialogue()"
                }
            ]
        },

        {
            "id": "response_hostile",
            "npc_line": "Big words, little mortal! Prepare yourself!",
            "options": null,
            "on_branch_enter": "combat_start(goblin_archer)"
        }
    ],

    "variables": {
        "player_has_trade_reputation": "player.reputation >= 50",
        "npc_is_alive": "not npc.is_dead"
    }
}
```

### Quests (.uzquest)

```json
{
    "format": "uzquest",
    "version": 1,
    "id": "find_crystal",
    "name": "Find the Crystal of Solitude",
    "giver": "goblin_archer",

    "stages": [
        {
            "id": "stage_explore",
            "description": "Travel to the Moonlit Cavern and search for the crystal.",
            "objectives": [
                {
                    "id": "visit_cavern",
                    "type": "location",
                    "location": [50, 0, 100],
                    "radius": 10,
                    "description": "Enter the Moonlit Cavern"
                },
                {
                    "id": "defeat_guardian",
                    "type": "kill_enemy",
                    "enemy_name": "crystal_guardian",
                    "count": 1,
                    "description": "Defeat the crystal guardian"
                }
            ],
            "completion_condition": "visit_cavern AND defeat_guardian"
        },

        {
            "id": "stage_return",
            "description": "Return the crystal to the goblin archer.",
            "objectives": [
                {
                    "id": "return_to_npc",
                    "type": "dialogue",
                    "npc": "goblin_archer",
                    "dialogue_tree": "content/dialogue/goblin_reward.uzdialogue",
                    "description": "Speak to the goblin archer"
                }
            ]
        }
    ],

    "rewards": {
        "xp": 500,
        "gold": 100,
        "items": [
            { "item_id": "ring_wisdom", "quantity": 1 }
        ]
    },

    "journal_entries": [
        { "stage": "stage_explore", "text": "I must find the Crystal of Solitude in the Moonlit Cavern." },
        { "stage": "stage_return", "text": "I have the crystal! Time to return to the goblin archer." }
    ]
}
```

### Characters (.uzcharacter)

```json
{
    "format": "uzcharacter",
    "version": 1,
    "id": "goblin_archer",
    "name": "Grix the Archer",
    "description": "A clever goblin archer who knows the forest's secrets",
    "avatar": "content/art/goblin_archer_portrait.uzimage",

    "stats": {
        "health": 50,
        "mana": 30,
        "strength": 5,
        "dexterity": 8,
        "intelligence": 6,
        "wisdom": 7,
        "charisma": 6
    },

    "dialogue_tree": "content/dialogue/goblin_greeting.uzdialogue",
    "inventory": [
        { "item": "bow_shortwood", "quantity": 1 },
        { "item": "arrows", "quantity": 20 }
    ],

    "ai_behavior": {
        "type": "patrol_combat",
        "patrol_points": [[0, 0, 0], [10, 0, 5], [15, 0, 10]],
        "patrol_speed": 3.0,
        "combat_radius": 20,
        "flee_health_percent": 0.25
    }
}
```

## Content Validation

The validator ensures generated content is safe and correct before runtime:

```cpp
/// Content validator — checks schema, constraints, automated tests.
class ContentValidator {
public:
    /// Validate a level JSON against .uzlevel schema.
    /// Checks: entity references exist, positions are finite, no duplicate IDs.
    Result ValidateLevel(const nlohmann::json& level_json);

    /// Validate a dialogue tree for consistency.
    /// Checks: no circular branches, all referenced branches exist, no orphaned nodes.
    Result ValidateDialogueTree(const nlohmann::json& dialogue_json);

    /// Run automated test on generated content.
    /// E.g., "Can the level be completed? Are all encounters beatable?"
    Result AutomatedTest(const LevelAsset& level);

    /// Get validation error messages with line numbers (helpful for LLM feedback).
    std::vector<ValidationError> GetErrors() const;
};

struct ValidationError {
    int line_number = -1;
    std::string path;           // JSON path to error (e.g., "entities[0].position")
    std::string message;        // Human-readable error
    Severity severity;          // Warning, Error, Critical
};
```

## LLM Integration

The ContentGenerator calls an LLM API and streams results incrementally:

```cpp
/// LLM-driven content generation.
class ContentGenerator {
public:
    enum class Provider { OpenAI, Anthropic, LocalLLM };

    /// Generate level via LLM.
    /// Streams JSON output to file as chunks arrive (user sees progress).
    AsyncHandle<LevelAsset> GenerateLevel(
        StringView prompt,          // "Create a spooky castle with 5 rooms"
        const GameContext& context, // Player level, current biome, etc.
        Provider provider = Provider::OpenAI,
        ProgressCallback* progress = nullptr
    );

    /// Generate dialogue tree for an NPC.
    AsyncHandle<DialogueTree> GenerateDialogue(
        StringView character_name,
        StringView character_description,
        const QuestContext& quest_context,
        ProgressCallback* progress = nullptr
    );

    /// Generate quest chain from story premise.
    AsyncHandle<std::vector<QuestAsset>> GenerateQuests(
        StringView story_premise,
        uint32_t num_quests,
        ProgressCallback* progress = nullptr
    );
};

/// Callback invoked as LLM streams output.
using ProgressCallback = std::function<void(StringView chunk, float percent_done)>;
```

## Hot-Reload Workflow

The editor watches generated content directories and hot-reloads on save:

```
LLM generates content/quests/find_crystal.uzquest
    │
    ├── File written to disk
    │
    └── FileWatcher event → ContentManager
        │
        ├── Parse JSON
        ├── Validate (ContentValidator)
        ├── If valid: Reload into scene
        │   └── Update NPC dialogue trees, objectives, etc.
        │
        └── If invalid: Show error in editor console with line numbers
```

Lua game code can listen to reload events:

```lua
ctx.events:subscribe("quest_reloaded", function(quest_id)
    print("Quest reloaded:", quest_id)
    -- Refresh quest UI, objectives, etc.
end)
```

## Comparison: Traditional vs AI-Augmented Pipeline

| Aspect | Traditional Pipeline | **AI-Augmented (UzEngine)** |
|--------|----------------------|---------------------------|
| Level creation | Designer places entities in editor | LLM generates .uzlevel from text prompt |
| Dialogue authoring | Writer creates tree in specialized tool | LLM generates dialogue branches from character + story context |
| Localization | Manual translation after content locked | LLM generates translations + culture-specific variations |
| Quest design | Designer writes quest logic | LLM generates quest chains from story outline |
| Content validation | Manual testing / QA team | Automated validator + LLM-rerun-on-fail |
| Iteration speed | Days (design → build → test) | Hours (prompt → generate → validate → hot-reload) |
| Transparency | Opaque: data in binary format | Transparent: all JSON/YAML readable by human and LLM |

## Implementation Phase

| Phase | What Ships |
|-------|-----------|
| **Phase 2** | ContentGenerator (basic LLM API integration), ContentValidator (schema + constraint checks), hot-reload for .uzlevel / .uzdialogue / .uzquest |
| **Phase 3** | Dialogue system (branching, conditions, Lua integration), Quest system (objectives, stages, rewards), Character system (stats, inventory, AI behavior) |
| **Phase 4** | VFX/animation generation from descriptions, Advanced validator (automated playtesting), Multi-LLM support (OpenAI + Anthropic + local), Content moderation layer |
