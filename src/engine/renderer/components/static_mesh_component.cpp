#include "engine/renderer/components/static_mesh_component.h"
#include "engine/renderer/proxies/mesh_proxy.h"
#include "engine/core/component_registry.h"

namespace uz {

ISceneProxy* StaticMeshComponent::create_proxy() {
    auto* proxy = new MeshProxy();
    proxy->mesh      = mesh;
    proxy->material  = material;
    proxy->bounds_   = mesh ? mesh->bounds : Aabb::empty();
    
    // In a real scenario, we'd also pass the transform, but for now
    // the proxy's sync_from_component() would handle that during frame update.
    return proxy;
}

// Auto-register the component with its proxy factory
UZENGINE_REGISTER_COMPONENT(StaticMeshComponent, []() -> ISceneProxy* {
    return new MeshProxy();
});

} // namespace uz
