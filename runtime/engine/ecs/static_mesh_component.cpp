/// StaticMeshComponent implementation — mesh and material proxy data sync.

#include "static_mesh_component.h"

#include "log.h"

UZ_DEFINE_LOG_CATEGORY(StaticMesh, Trace);

namespace uz {

// ─────────────────────────────────────────────────────────────────────────────
// StaticMeshComponent
// ─────────────────────────────────────────────────────────────────────────────

void StaticMeshComponent::OnProxyCreated(RenderWorld& renderWorld,
                                          RenderProxyHandle handle) {
    RenderProxy* proxy = renderWorld.GetProxy(handle);
    if (!proxy) {
        return;
    }
    proxy->staticMesh.meshId     = meshId;
    proxy->staticMesh.materialId = materialId;

    UZ_LOG(StaticMesh, Trace, "StaticMesh proxy created: meshId={}, materialId={}",
           meshId, materialId);
}

} // namespace uz
