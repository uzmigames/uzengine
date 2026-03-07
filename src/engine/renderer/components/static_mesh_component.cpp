#include "engine/renderer/components/static_mesh_component.h"
#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/core/component_registry.h"

namespace uz {

ISceneProxy* StaticMeshComponent::create_proxy() {
    auto* proxy = new MeshProxy();
    proxy->mesh    = mesh;
    proxy->bounds_ = mesh ? mesh->bounds : Aabb::empty();

    // Set slot 0 material from the component.
    proxy->material_slots_.resize(1);
    proxy->material_slots_[0].material = material;

    proxy->cache_materials();
    return proxy;
}

// Auto-register the component with its proxy factory
UZENGINE_REGISTER_COMPONENT(StaticMeshComponent, []() -> ISceneProxy* {
    return new MeshProxy();
});

} // namespace uz
