# Property Change Notification & Reactivity

> **Status**: Planned (Phase 3)
> **Source**: `engine/property/reactivity/`
> **References**: Qt property system notifications, Unreal property change delegates

## Purpose

Reactive property system — when a property changes, subscribed systems are notified. Enables:
- **Editor binding** — inspector updates when property changes (or vice versa)
- **UI reactivity** — health bars update when health property changes
- **Dirty flags** — mark components for serialization/network replication
- **Event propagation** — cascade updates (change health → trigger damage reaction)

## Change Callbacks

Each property can have `OnChanged` callbacks:

```cpp
namespace uz {

/// Fired when a property value changes
using PropertyChangedCallback = std::function<void(const PropertyChangedEvent&)>;

struct PropertyChangedEvent {
    const void* component;          ///< Component that changed
    const PropertyInfo* property;   ///< Which property changed
    const void* old_value;          ///< Previous value
    const void* new_value;          ///< New value
};

}  // namespace uz
```

## Reactive Property API

```cpp
namespace uz {

/// Component that tracks property changes
class ReactiveComponent {
public:
    /// Listen for property changes
    void OnPropertyChanged(
        StringView property_name,
        PropertyChangedCallback callback
    );

    /// Stop listening
    void UnsubscribePropertyChanged(
        StringView property_name,
        PropertyChangedCallback callback
    );

    /// Manually notify of change (for internal mutations)
    void NotifyPropertyChanged(const PropertyInfo& prop, const void* old_val);

    /// Mark component as modified (triggers dirty flag)
    void MarkModified();

    /// Check if component has pending changes
    bool IsDirty() const;

    /// Clear dirty flag
    void ClearDirty();

private:
    std::unordered_map<std::string, std::vector<PropertyChangedCallback>> listeners_;
    bool dirty_ = false;
};

}  // namespace uz
```

## Usage Example: Health System

```cpp
struct HealthComponent : public uz::ActorComponent {
    UZ_COMPONENT(HealthComponent);

    UZ_PROPERTY(float, health, 100.0f, "Current health");
    UZ_PROPERTY(float, max_health, 100.0f, "Maximum health");

    void TakeDamage(float damage) {
        float old_health = health;
        health = std::max(0.0f, health - damage);

        // Notify property system that health changed
        NotifyPropertyChanged(GetPropertyInfo("health"), &old_health);

        // If health is zero, fire event
        if (health == 0.0f) {
            OnDied.Broadcast();
        }
    }

    MulticastDelegate<void()> OnDied;
};

// In player controller:
class PlayerController : public Behavior {
    void OnBeginPlay() override {
        auto health_comp = entity_->Get<HealthComponent>();

        // React to health changes
        health_comp->OnPropertyChanged("health", [this](const PropertyChangedEvent& evt) {
            // Health changed — update UI
            float new_health = *static_cast<const float*>(evt.new_value);
            UpdateHealthBar(new_health);

            // If health dropped below 50%, play warning
            float old_health = *static_cast<const float*>(evt.old_value);
            if (old_health > 50.0f && new_health <= 50.0f) {
                PlayWarningSound();
            }
        });
    }
};
```

## Dirty Flags

Components track whether they've been modified since last serialization/replication:

```cpp
// After player modifies health property:
health_comp->MarkModified();

// Later, during save:
if (health_comp->IsDirty()) {
    SaveComponentToFile(health_comp);
    health_comp->ClearDirty();
}

// Or for network replication:
if (health_comp->IsDirty()) {
    ReplicateComponentChanges(health_comp);
    health_comp->ClearDirty();
}
```

## Lua Integration

```lua
-- React to property changes
player:health_component():on_property_changed("health", function(evt)
    print("Health changed from " .. evt.old_value .. " to " .. evt.new_value)

    -- Update UI
    ui_health_bar:set_value(evt.new_value)
end)

-- React to events triggered by property changes
player:health_component():on_died():connect(function()
    print("Player died!")
    scene:load_level("game_over")
end)
```

## Cascading Updates

Property changes can trigger other changes via callbacks:

```
Health property changes
  ├── Trigger OnPropertyChanged listeners
  │   └── UI updates health bar
  └── Trigger OnHealthChanged event
      ├── Damage reaction system activates
      ├── Audio plays hurt sound
      ├── Animation plays flinch
      └── If health == 0: trigger death sequence
```

## Editor Binding

Inspector listens to property changes and reflects them in real-time:

```cpp
// In VSCode editor extension
Editor::OnPropertyChanged += [](const PropertyChangedEvent& evt) {
    // Property on entity changed
    // Update inspector UI to show new value
    UpdateInspectorUI(evt.component, evt.property->name, evt.new_value);
};
```

Vice versa — when user edits inspector field:

```cpp
// In VSCode editor extension
UserEditedInspectorField += [](Entity entity, StringView prop_name, const void* new_value) {
    // User changed value in inspector
    // Apply to entity
    component->SetPropertyValue(prop_name, new_value);
    // This triggers OnPropertyChanged → syncs everywhere
};
```

## Network Replication Trigger

Dirty-flag system can queue network updates:

```cpp
// When property changes on client
health_comp->OnPropertyChanged("health", [](const PropertyChangedEvent& evt) {
    // Mark for replication
    health_comp->MarkNetworkDirty();
});

// During network tick
void ReplicateComponents() {
    world.ForEachComponent<ReactiveComponent>([](ReactiveComponent* comp) {
        if (comp->IsNetworkDirty()) {
            SendComponentUpdateToServer(comp);
            comp->ClearNetworkDirty();
        }
    });
}
```

## Change History (Undo/Redo)

Reactive system can log property changes for undo/redo:

```cpp
// In editor undo system
undo_system.OnPropertyChanged += [](const PropertyChangedEvent& evt) {
    // Record this change
    undo_system.RecordPropertyChange(
        evt.component,
        evt.property->name,
        evt.old_value,
        evt.new_value
    );
};

// Undo:
undo_system.Undo();  // Restores old_value, triggers OnPropertyChanged
```

## Performance Considerations

### Deferred Notifications

Instead of firing callbacks immediately, batch them:

```cpp
// Deferred callback firing
component->PushNotificationContext();

// These don't fire yet
component->SetProperty("x", 10.0f);
component->SetProperty("y", 20.0f);
component->SetProperty("z", 30.0f);

component->PopNotificationContext();  // All three fire callbacks now
```

### Listener Limits

Avoid excessive listeners on frequently-changing properties:

```cpp
// GOOD: Listen once per component
health_comp->OnPropertyChanged("health", UpdateUI);

// BAD: Many small listeners (one per health tick)
for (int i = 0; i < 1000; ++i) {
    health_comp->OnPropertyChanged("health", [i]() { ... });
}
```

## Reference Implementation

```cpp
namespace uz {

class ReactiveComponent : public uz::Component {
public:
    void OnPropertyChanged(StringView prop_name, PropertyChangedCallback cb) {
        listeners_[std::string(prop_name)].push_back(cb);
    }

    void NotifyPropertyChanged(const PropertyInfo& prop, const void* old_val) {
        auto it = listeners_.find(prop.name);
        if (it == listeners_.end()) return;

        PropertyChangedEvent evt{
            this,
            &prop,
            old_val,
            reinterpret_cast<const uint8_t*>(this) + prop.offset
        };

        for (auto& callback : it->second) {
            callback(evt);
        }

        MarkModified();  // Any change marks component dirty
    }

    void MarkModified() { dirty_ = true; }
    bool IsDirty() const { return dirty_; }
    void ClearDirty() { dirty_ = false; }

private:
    std::unordered_map<std::string, std::vector<PropertyChangedCallback>> listeners_;
    bool dirty_ = false;
};

}  // namespace uz
```

## Comparison

| Feature | Qt property system | C# PropertyChanged event | Unreal property change | **UzEngine** |
|---------|-------------------|--------------------------|----------------------|--------------|
| Change callbacks | Yes (automatic) | Manual (INotifyPropertyChanged) | MulticastDelegate | OnPropertyChanged |
| Dirty tracking | No | Manual | No | Automatic |
| Undo/redo support | Declarative | Manual | Yes (undo framework) | Via history integration |
| Performance | Optimized | Varies | Good | Deferred notifications |
| Lua support | No | No | No | Yes |
| Network integration | No | No | Yes | Dirty-flag trigger |

## Implementation Phase

- **Phase 3**: OnPropertyChanged callbacks, dirty flags, basic undo/redo integration
- **Phase 4**: Deferred notifications, performance optimizations, editor binding
- **Phase 5**: Constraint-based reactivity (if A changes, update B), network replication integration
