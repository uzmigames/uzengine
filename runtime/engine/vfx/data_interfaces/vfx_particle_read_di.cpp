#include "vfx_particle_read_di.h"

namespace uz {

void VfxParticleReadSource::RebuildIdIndex() {
    idIndex.clear();
    idIndex.reserve(persistentIDs.size());
    for (uint32_t slot = 0; slot < persistentIDs.size(); ++slot) {
        const uint64_t id = persistentIDs[slot];
        // Persistent ID 0 is the dead-slot sentinel — skip so stale
        // entries don't resolve to the first live slot.
        if (id == 0u) continue;
        idIndex.emplace(id, slot);
    }
}

namespace {

/// Safe array read helper — returns `fallback` when `slot` is out of range.
template <typename T>
T SafeAt(const std::vector<T>& v, uint32_t slot, const T& fallback) {
    return (slot < v.size()) ? v[slot] : fallback;
}

} // namespace

VfxParticleReadSnapshot VfxParticleReadSource::SampleByID(
    uint64_t id, const VfxParticleReadDefaults& defaults) const {
    VfxParticleReadSnapshot out;
    out.position = defaults.position;
    out.velocity = defaults.velocity;
    out.color    = defaults.color;
    out.size     = defaults.size;
    out.age      = defaults.age;

    if (id == 0u) return out;  // sentinel
    const auto it = idIndex.find(id);
    if (it == idIndex.end()) return out;

    return SampleBySlot(it->second, defaults);
}

VfxParticleReadSnapshot VfxParticleReadSource::SampleBySlot(
    uint32_t slot, const VfxParticleReadDefaults& defaults) const {
    VfxParticleReadSnapshot out;
    out.position = defaults.position;
    out.velocity = defaults.velocity;
    out.color    = defaults.color;
    out.size     = defaults.size;
    out.age      = defaults.age;

    if (slot >= persistentIDs.size()) return out;

    out.position = SafeAt(positions,  slot, defaults.position);
    out.velocity = SafeAt(velocities, slot, defaults.velocity);
    out.color    = SafeAt(colors,     slot, defaults.color);
    out.size     = SafeAt(sizes,      slot, defaults.size);
    out.age      = SafeAt(ages,       slot, defaults.age);
    out.valid    = true;
    return out;
}

} // namespace uz
