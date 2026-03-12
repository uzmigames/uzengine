# Gameplay Ability System

> **Status**: Planned (Phase 4)
> **Source**: `engine/gameplay/abilities/`
> **References**: UE5 Gameplay Ability System (clean-room), UnrealEngine/Engine/Plugins/Gameplay/GameplayAbilities

## Purpose

Flexible ability system for game mechanics: spellcasting, combat abilities, item activation, emotes. Abilities have:
- **Activation** via input, NPC behavior, or triggers
- **Cooldowns** with configurable duration and shared cooldown groups
- **Costs** (mana, stamina, health drain, item consumption)
- **Tags** for categorization and ability interaction rules
- **Attribute modifiers** for temporary buffs/debuffs
- **Callbacks** for ability events (on activate, on complete, on interrupted)

Unlike Unreal's `FGameplayAbilitySpec` (heavyweight, per-instance), UzEngine abilities are **stateless definitions** loaded from `.uzability` files and executed per entity via an `AbilityComponent`.

## Ability Definition

### `.uzability` File Format (JSON)

```json
{
    "ability_id": "fireball",
    "display_name": "Fireball",
    "description": "Launch a ball of fire at target",
    "icon": "content/icons/fireball.png",
    "cooldown_group": "spell_cooldown",
    "cooldown_duration": 5.0,
    "cost": {
        "mana": 50,
        "health": 0
    },
    "tags": ["spell", "fire", "aoe"],
    "activation_type": "targeted",  // none, input, targeted, channel
    "range": 2000.0,
    "cast_time": 0.5,
    "channel_duration": 0.0,
    "modifiers": [
        {
            "attribute": "fire_resistance",
            "operation": "add",
            "value": 0.2,
            "duration": 10.0
        }
    ]
}
```

### AbilitySpec (C++ Definition)

```cpp
namespace uz {

/// Specifies an ability's definition, costs, cooldowns, and effects.
struct AbilitySpec {
    std::string ability_id;           ///< Unique identifier (e.g., "fireball")
    std::string display_name;         ///< Human-readable name
    std::string description;          ///< Tooltip description
    uz::AssetRef<uz::Texture> icon;   ///< UI icon

    float cooldown_duration = 0.0f;   ///< Cooldown in seconds (0 = no cooldown)
    std::string cooldown_group;       ///< Shared group (e.g., "spell_cooldown")

    struct Cost {
        float mana = 0.0f;            ///< Mana cost
        float stamina = 0.0f;         ///< Stamina cost
        float health = 0.0f;          ///< Health cost (damage to self)
    } cost;

    std::vector<std::string> tags;    ///< Categorization tags (spell, fire, aoe, etc.)

    enum ActivationType { None, Input, Targeted, Channel };
    ActivationType activation_type = ActivationType::None;

    float range = 0.0f;               ///< Max activation range (0 = self only)
    float cast_time = 0.0f;           ///< Cast duration in seconds
    float channel_duration = 0.0f;    ///< Duration if channeling (0 = instant)

    /// Temporary attribute modifiers applied when ability activates
    std::vector<AttributeModifier> modifiers;

    /// Load ability spec from .uzability file
    static AbilitySpec LoadFromFile(StringView path);
};

}  // namespace uz
```

## AbilityComponent

```cpp
namespace uz {

/// Manages abilities for an entity: activation, cooldowns, cost checks, tags.
class AbilityComponent : public ActorComponent {
public:
    UZ_COMPONENT(AbilityComponent);

    /// Register an ability on this entity
    void AddAbility(const AbilitySpec& spec);

    /// Activate ability by ID. Returns false if on cooldown or insufficient resources.
    /// `target` is optional (for targeted abilities).
    bool ActivateAbility(StringView ability_id, Entity target = {});

    /// Check if ability can activate (not on cooldown, enough resources)
    bool CanActivate(StringView ability_id, Entity target = {}) const;

    /// Get cooldown remaining (0 if available)
    float GetCooldownRemaining(StringView ability_id) const;

    /// Get all active abilities
    std::vector<std::string> GetAbilityIds() const;

    /// Check if entity has ability with tag
    bool HasAbilityWithTag(StringView tag) const;

    /// Fired when ability activates
    MulticastDelegate<void(StringView ability_id)> OnAbilityActivated;

    /// Fired when ability completes
    MulticastDelegate<void(StringView ability_id)> OnAbilityCompleted;

    /// Fired when ability interrupted (interrupted by another ability or external event)
    MulticastDelegate<void(StringView ability_id)> OnAbilityInterrupted;

private:
    std::unordered_map<std::string, AbilitySpec> abilities_;
    std::unordered_map<std::string, float> cooldown_timers_;
    std::set<std::string> active_abilities_;  ///< Currently executing abilities
};

}  // namespace uz
```

## Attribute Modifiers

Temporary stat changes (buffs/debuffs) applied when abilities activate.

```cpp
namespace uz {

/// Temporary modification to an attribute (e.g., +10% fire resistance, -50% speed)
struct AttributeModifier {
    std::string attribute;            ///< Attribute name (e.g., "fire_resistance")
    float value;                      ///< Modification value

    enum Operation {
        Add,           ///< attribute += value
        Multiply,      ///< attribute *= (1.0 + value)  [value is %]
        Set            ///< attribute = value
    } operation = Operation::Add;

    float duration = 0.0f;            ///< Duration in seconds (0 = permanent)
};

}  // namespace uz
```

## Lua Binding

```lua
-- Activate fireball on target
local enemy = scene:find_entity_by_name("Goblin")
player:ability_component():activate_ability("fireball", enemy)

-- Check cooldown before showing UI button
local cooldown = player:ability_component():get_cooldown_remaining("fireball")
if cooldown > 0 then
    ui_button.label = "Fireball (" .. math.ceil(cooldown) .. "s)"
else
    ui_button.label = "Fireball"
end

-- React to ability activation
player:ability_component():on_ability_activated():connect(function(ability_id)
    print("Activated: " .. ability_id)
end)
```

## Ability Activation Lifecycle

```
1. Player input or script calls ActivateAbility("ability_id")
   ↓
2. Component checks:
   - Ability exists
   - Not on cooldown
   - Has required resources (mana, stamina, health)
   - Target is valid (if targeted ability)
   ↓
3. If checks pass:
   - Deduct costs (mana, stamina, health)
   - Apply modifiers to caster
   - Fire OnAbilityActivated callback
   - Execute ability script (Lua or native handler)
   ↓
4. If ability has cast_time > 0:
   - Play casting animation
   - Lock movement/other actions
   ↓
5. On completion:
   - Start cooldown timer
   - Fire OnAbilityCompleted callback
   - If modifiers have duration, schedule removal
   ↓
6. If interrupted:
   - Cancel any ongoing cast
   - Remove active modifiers
   - Fire OnAbilityInterrupted callback
   - Refund costs (optional)
```

## Tag System

Abilities are categorized with tags for interaction rules:

```cpp
// Prevent simultaneous casting of multiple spells
if (entity->HasAbilityWithTag("spell") && entity->HasActiveAbility("spell")) {
    return false;  // can't cast another spell while one is active
}

// Only interrupt casting spells, not physical attacks
if (HasAbilityWithTag("channel") && other_ability->HasTag("instant")) {
    this->InterruptAbility();
}
```

## Cost System

Before activation, all costs are checked and deducted:

```cpp
struct AbilityCost {
    float mana = 0.0f;
    float stamina = 0.0f;
    float health = 0.0f;
};

bool CheckCosts(const AbilityCost& cost) const {
    auto attrs = entity_->Get<AttributeComponent>();
    return (attrs->GetAttribute("mana") >= cost.mana) &&
           (attrs->GetAttribute("stamina") >= cost.stamina) &&
           (attrs->GetAttribute("health") > cost.health);
}

void DeductCosts(const AbilityCost& cost) {
    auto attrs = entity_->Get<AttributeComponent>();
    attrs->ModifyAttribute("mana", -cost.mana);
    attrs->ModifyAttribute("stamina", -cost.stamina);
    attrs->ModifyAttribute("health", -cost.health);
}
```

## Cooldown System

Shared cooldown groups allow multiple abilities to share a single cooldown:

```cpp
// Both "fireball" and "ice_blast" share group "spell_cooldown"
// Casting one puts both on cooldown
std::unordered_map<std::string, float> cooldown_timers_;  // group → remaining seconds

float GetCooldownRemaining(StringView ability_id) const {
    auto it = abilities_.find(ability_id);
    if (it == abilities_.end()) return 0.0f;

    auto timer_it = cooldown_timers_.find(it->second.cooldown_group);
    return (timer_it != cooldown_timers_.end()) ? timer_it->second : 0.0f;
}

void StartCooldown(StringView ability_id) {
    auto it = abilities_.find(ability_id);
    if (it == abilities_.end()) return;

    cooldown_timers_[it->second.cooldown_group] = it->second.cooldown_duration;
}
```

## Usage Example

```cpp
// In a player controller
class PlayerController : public uz::Behavior {
    void OnFireButtonPressed() {
        auto abilities = entity_->Get<AbilityComponent>();

        // Cast fireball at mouse target
        if (auto target = GetMouseTarget()) {
            abilities->ActivateAbility("fireball", target);
        }
    }
};
```

## Comparison

| Feature | UE5 Gameplay Ability System | Unity PlayMaker | **UzEngine** |
|---------|---------------------------|-----------------|--------------|
| Activation types | Input, passive, triggered | Triggered | Input, targeted, channel |
| Cooldowns | Per-ability + shared | Manual FSM | Shared groups |
| Cost system | Attribute-based (GameplayEffect) | Manual | Simple (mana, stamina, health) |
| Modifiers | GameplayEffect stacking | Manual | Temporary attribute changes |
| Tag system | Ability + Effect tags | None | String-based categorization |
| Activation rule | Montage-driven | FSM states | Lua callbacks |

## Implementation Phase

- **Phase 4**: AbilityComponent, cost checking, cooldowns, simple modifiers, tag system
- **Phase 5**: Shared cooldown groups, modifier stacking rules, advanced attribute interactions
