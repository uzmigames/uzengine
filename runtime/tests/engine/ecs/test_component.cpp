/// Unit tests for uz::Component, uz::ActorComponent, and uz::SceneComponent.
///
/// Coverage targets:
///   - component.h          → 100% (inline accessors + virtual lifecycle)
///   - scene_component.h    → 100%
///   - scene_component.cpp  → 100%
///
/// No GPU, RHI, or World instance required. Tests operate directly on
/// component objects to validate transform math and hierarchy semantics.

#include "scene_component.h"

#include <gtest/gtest.h>

#include <cmath>

using namespace uz;

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

static constexpr float kEps = 1e-4f;

static bool NearlyEqual(float a, float b, float eps = kEps) {
    return std::fabs(a - b) < eps;
}

static bool NearlyEqualVec3(const Vec3& a, const Vec3& b, float eps = kEps) {
    return NearlyEqual(a.x, b.x, eps) &&
           NearlyEqual(a.y, b.y, eps) &&
           NearlyEqual(a.z, b.z, eps);
}

// ─────────────────────────────────────────────────────────────────────────────
// Concrete test component — exposes lifecycle call counts
// ─────────────────────────────────────────────────────────────────────────────

/// Concrete ActorComponent that records lifecycle invocations.
class LifecycleTracker : public ActorComponent {
public:
    int registerCount    = 0;
    int initCount        = 0;
    int beginPlayCount   = 0;
    int endPlayCount     = 0;
    int uninitCount      = 0;
    int unregisterCount  = 0;

    void OnRegister()    override { ++registerCount; }
    void OnInitialize()  override { ++initCount; }
    void OnBeginPlay()   override { ++beginPlayCount; }
    void OnEndPlay()     override { ++endPlayCount; }
    void OnUninitialize() override { ++uninitCount; }
    void OnUnregister()  override { ++unregisterCount; }
};

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: Component
// ─────────────────────────────────────────────────────────────────────────────

// 1. Default state
TEST(ComponentTest, DefaultStateIsUnregisteredAndEnabled) {
    LifecycleTracker c;
    EXPECT_EQ(c.GetState(), ComponentState::Unregistered);
    EXPECT_TRUE(c.IsEnabled());
    EXPECT_FALSE(c.GetEntity().IsValid());
    EXPECT_EQ(c.GetWorld(), nullptr);
}

// 2. SetEnabled toggles enabled flag
TEST(ComponentTest, SetEnabledTogglesFlag) {
    LifecycleTracker c;
    EXPECT_TRUE(c.IsEnabled());

    c.SetEnabled(false);
    EXPECT_FALSE(c.IsEnabled());

    c.SetEnabled(true);
    EXPECT_TRUE(c.IsEnabled());
}

// 3. ActorComponent is a Component
TEST(ComponentTest, ActorComponentIsAComponent) {
    LifecycleTracker tracker;
    Component* base = &tracker;
    EXPECT_NE(base, nullptr);
    EXPECT_EQ(base->GetState(), ComponentState::Unregistered);
}

// 4. Lifecycle callbacks fire when invoked directly
TEST(ComponentTest, LifecycleCallbacksAreInvokable) {
    LifecycleTracker c;
    c.OnRegister();
    c.OnInitialize();
    c.OnBeginPlay();
    c.OnEndPlay();
    c.OnUninitialize();
    c.OnUnregister();

    EXPECT_EQ(c.registerCount,   1);
    EXPECT_EQ(c.initCount,       1);
    EXPECT_EQ(c.beginPlayCount,  1);
    EXPECT_EQ(c.endPlayCount,    1);
    EXPECT_EQ(c.uninitCount,     1);
    EXPECT_EQ(c.unregisterCount, 1);
}

// 5. Base Component virtual lifecycle methods are no-ops by default
TEST(ComponentTest, BaseVirtualLifecycleMethodsAreNoOps) {
    // Construct via a plain ActorComponent (uses base implementations).
    ActorComponent c;
    // None of the following should crash or do anything observable.
    EXPECT_NO_THROW(c.OnRegister());
    EXPECT_NO_THROW(c.OnInitialize());
    EXPECT_NO_THROW(c.OnBeginPlay());
    EXPECT_NO_THROW(c.OnEndPlay());
    EXPECT_NO_THROW(c.OnUninitialize());
    EXPECT_NO_THROW(c.OnUnregister());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — default transform
// ─────────────────────────────────────────────────────────────────────────────

// 6. Default local transform is identity (origin, no rotation, unit scale)
TEST(SceneComponentTest, DefaultTransformIsIdentity) {
    SceneComponent sc;

    EXPECT_TRUE(NearlyEqualVec3(sc.GetPosition(), {0.f, 0.f, 0.f}));
    EXPECT_TRUE(NearlyEqualVec3(sc.GetScale(),    {1.f, 1.f, 1.f}));

    const Quat& rot = sc.GetRotation();
    EXPECT_TRUE(NearlyEqual(rot.x, 0.f));
    EXPECT_TRUE(NearlyEqual(rot.y, 0.f));
    EXPECT_TRUE(NearlyEqual(rot.z, 0.f));
    EXPECT_TRUE(NearlyEqual(rot.w, 1.f));
}

// 7. Default state: dirty = true, no parent, no children
TEST(SceneComponentTest, DefaultHierarchyStateIsEmpty) {
    SceneComponent sc;
    EXPECT_TRUE(sc.IsDirty());
    EXPECT_EQ(sc.GetParent(), nullptr);
    EXPECT_TRUE(sc.GetChildren().empty());
}

// 8. SceneComponent is a Component
TEST(SceneComponentTest, IsAComponent) {
    SceneComponent sc;
    Component* base = &sc;
    EXPECT_NE(base, nullptr);
    EXPECT_EQ(base->GetState(), ComponentState::Unregistered);
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — setter dirty propagation
// ─────────────────────────────────────────────────────────────────────────────

// 9. SetPosition marks dirty
TEST(SceneComponentTest, SetPositionMarksDirty) {
    SceneComponent sc;
    // Force clean state by reading world matrix.
    (void)sc.GetWorldMatrix();
    EXPECT_FALSE(sc.IsDirty());

    sc.SetPosition({1.f, 2.f, 3.f});
    EXPECT_TRUE(sc.IsDirty());
    EXPECT_TRUE(NearlyEqualVec3(sc.GetPosition(), {1.f, 2.f, 3.f}));
}

// 10. SetRotation marks dirty
TEST(SceneComponentTest, SetRotationMarksDirty) {
    SceneComponent sc;
    (void)sc.GetWorldMatrix();
    EXPECT_FALSE(sc.IsDirty());

    sc.SetRotation(Quat::FromAxisAngle(Vec3::UnitY(), 1.0f));
    EXPECT_TRUE(sc.IsDirty());
}

// 11. SetScale marks dirty
TEST(SceneComponentTest, SetScaleMarksDirty) {
    SceneComponent sc;
    (void)sc.GetWorldMatrix();
    EXPECT_FALSE(sc.IsDirty());

    sc.SetScale({2.f, 2.f, 2.f});
    EXPECT_TRUE(sc.IsDirty());
    EXPECT_TRUE(NearlyEqualVec3(sc.GetScale(), {2.f, 2.f, 2.f}));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — world matrix computation
// ─────────────────────────────────────────────────────────────────────────────

// 12. GetWorldMatrix clears dirty flag
TEST(SceneComponentTest, GetWorldMatrixClearsDirtyFlag) {
    SceneComponent sc;
    EXPECT_TRUE(sc.IsDirty());
    (void)sc.GetWorldMatrix();
    EXPECT_FALSE(sc.IsDirty());
}

// 13. Root component with only translation: world matrix == local translation matrix
TEST(SceneComponentTest, RootTranslationOnlyMatchesTranslationMatrix) {
    SceneComponent sc;
    sc.SetPosition({3.f, 7.f, -2.f});

    const Mat4& world = sc.GetWorldMatrix();
    const Vec3 translated = world * Vec3{0.f, 0.f, 0.f};

    // Transform origin — should yield the translation.
    EXPECT_TRUE(NearlyEqual(translated.x, 3.f));
    EXPECT_TRUE(NearlyEqual(translated.y, 7.f));
    EXPECT_TRUE(NearlyEqual(translated.z, -2.f));
}

// 14. Root component with uniform scale: world matrix scales correctly
TEST(SceneComponentTest, RootUniformScaleIsCorrect) {
    SceneComponent sc;
    sc.SetScale({3.f, 3.f, 3.f});

    const Mat4& world = sc.GetWorldMatrix();
    // Transform a unit point along X — should be 3x scaled.
    const Vec3 result = world * Vec3{1.f, 0.f, 0.f};
    EXPECT_TRUE(NearlyEqual(result.x, 3.f));
    EXPECT_TRUE(NearlyEqual(result.y, 0.f));
    EXPECT_TRUE(NearlyEqual(result.z, 0.f));
}

// 15. GetWorldPosition for root component equals local position
TEST(SceneComponentTest, RootWorldPositionEqualsLocalPosition) {
    SceneComponent sc;
    sc.SetPosition({5.f, -1.f, 8.f});
    const Vec3 worldPos = sc.GetWorldPosition();
    EXPECT_TRUE(NearlyEqual(worldPos.x, 5.f));
    EXPECT_TRUE(NearlyEqual(worldPos.y, -1.f));
    EXPECT_TRUE(NearlyEqual(worldPos.z, 8.f));
}

// 16. Identity transform produces identity world matrix
TEST(SceneComponentTest, IdentityLocalProducesIdentityWorld) {
    SceneComponent sc;
    const Mat4& world = sc.GetWorldMatrix();
    const Mat4 identity = Mat4::Identity();

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_TRUE(NearlyEqual(world(row, col), identity(row, col)))
                << "Mismatch at [" << row << "][" << col << "]";
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — hierarchy attachment
// ─────────────────────────────────────────────────────────────────────────────

// 17. AttachTo sets parent pointer and child list
TEST(SceneComponentTest, AttachToSetsParentAndChild) {
    SceneComponent parent, child;
    child.AttachTo(&parent);

    EXPECT_EQ(child.GetParent(), &parent);
    ASSERT_EQ(parent.GetChildren().size(), 1u);
    EXPECT_EQ(parent.GetChildren()[0], &child);
}

// 18. AttachTo marks child dirty
TEST(SceneComponentTest, AttachToMarksDirty) {
    SceneComponent parent, child;
    (void)child.GetWorldMatrix(); // force clean
    EXPECT_FALSE(child.IsDirty());

    child.AttachTo(&parent);
    EXPECT_TRUE(child.IsDirty());
}

// 19. Child world position = parent position + child local position
TEST(SceneComponentTest, ChildWorldPositionAddsParentTranslation) {
    SceneComponent parent, child;
    parent.SetPosition({10.f, 0.f, 0.f});
    child.SetPosition({5.f, 0.f, 0.f});
    child.AttachTo(&parent);

    const Vec3 worldPos = child.GetWorldPosition();
    EXPECT_TRUE(NearlyEqual(worldPos.x, 15.f));
    EXPECT_TRUE(NearlyEqual(worldPos.y, 0.f));
    EXPECT_TRUE(NearlyEqual(worldPos.z, 0.f));
}

// 20. Child world matrix = local * parent world matrix
TEST(SceneComponentTest, ChildWorldMatrixEqualsLocalTimesParentWorld) {
    SceneComponent parent, child;
    parent.SetPosition({4.f, 0.f, 0.f});
    child.SetPosition({1.f, 0.f, 0.f});
    child.AttachTo(&parent);

    const Mat4& parentWorld = parent.GetWorldMatrix();
    const Mat4& childWorld  = child.GetWorldMatrix();

    // Build expected matrix manually.
    const Mat4 localScale    = Mat4::Scaling(child.GetScale());
    const Mat4 localRotation = child.GetRotation().toMatrix();
    const Mat4 localTranslate = Mat4::Translation(child.GetPosition());
    const Mat4 expectedLocal = localScale * localRotation * localTranslate;
    const Mat4 expectedWorld = expectedLocal * parentWorld;

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            EXPECT_TRUE(NearlyEqual(childWorld(row, col), expectedWorld(row, col)))
                << "Mismatch at [" << row << "][" << col << "]";
        }
    }
}

// 21. Three-level hierarchy: grandchild world position is additive
TEST(SceneComponentTest, ThreeLevelHierarchyWorldPositionIsAdditive) {
    SceneComponent grandparent, parent, child;
    grandparent.SetPosition({1.f, 0.f, 0.f});
    parent.SetPosition({2.f, 0.f, 0.f});
    child.SetPosition({3.f, 0.f, 0.f});

    parent.AttachTo(&grandparent);
    child.AttachTo(&parent);

    const Vec3 worldPos = child.GetWorldPosition();
    EXPECT_TRUE(NearlyEqual(worldPos.x, 6.f)); // 1+2+3
    EXPECT_TRUE(NearlyEqual(worldPos.y, 0.f));
    EXPECT_TRUE(NearlyEqual(worldPos.z, 0.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — dirty propagation
// ─────────────────────────────────────────────────────────────────────────────

// 22. Parent MarkDirty propagates to all children
TEST(SceneComponentTest, ParentMarkDirtyPropagatesRecursively) {
    SceneComponent grandparent, parent, child1, child2;
    parent.AttachTo(&grandparent);
    child1.AttachTo(&parent);
    child2.AttachTo(&parent);

    // Force all clean.
    (void)grandparent.GetWorldMatrix();
    (void)parent.GetWorldMatrix();
    (void)child1.GetWorldMatrix();
    (void)child2.GetWorldMatrix();

    EXPECT_FALSE(grandparent.IsDirty());
    EXPECT_FALSE(parent.IsDirty());
    EXPECT_FALSE(child1.IsDirty());
    EXPECT_FALSE(child2.IsDirty());

    grandparent.MarkDirty();

    EXPECT_TRUE(grandparent.IsDirty());
    EXPECT_TRUE(parent.IsDirty());
    EXPECT_TRUE(child1.IsDirty());
    EXPECT_TRUE(child2.IsDirty());
}

// 23. SetPosition on parent propagates dirty to children
TEST(SceneComponentTest, ParentSetPositionDirtiesChildren) {
    SceneComponent parent, child;
    child.AttachTo(&parent);

    (void)parent.GetWorldMatrix();
    (void)child.GetWorldMatrix();
    EXPECT_FALSE(child.IsDirty());

    parent.SetPosition({1.f, 0.f, 0.f});
    EXPECT_TRUE(child.IsDirty());
}

// 24. MarkDirty on already-dirty node does not re-traverse children redundantly
TEST(SceneComponentTest, MarkDirtyOnAlreadyDirtyIsSafe) {
    SceneComponent parent, child;
    child.AttachTo(&parent);
    // Both start dirty — calling MarkDirty again must not crash.
    EXPECT_NO_THROW(parent.MarkDirty());
    EXPECT_TRUE(parent.IsDirty());
    EXPECT_TRUE(child.IsDirty());
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — Detach
// ─────────────────────────────────────────────────────────────────────────────

// 25. Detach removes child from parent's children list
TEST(SceneComponentTest, DetachRemovesChildFromParent) {
    SceneComponent parent, child;
    child.AttachTo(&parent);
    EXPECT_EQ(parent.GetChildren().size(), 1u);

    child.Detach();

    EXPECT_EQ(parent.GetChildren().size(), 0u);
    EXPECT_EQ(child.GetParent(), nullptr);
}

// 26. Detach with no parent is a no-op
TEST(SceneComponentTest, DetachWithNoParentIsNoOp) {
    SceneComponent sc;
    EXPECT_NO_THROW(sc.Detach());
    EXPECT_EQ(sc.GetParent(), nullptr);
}

// 27. Detach then re-attach to different parent
TEST(SceneComponentTest, ReAttachMovesToNewParent) {
    SceneComponent parent1, parent2, child;
    child.AttachTo(&parent1);
    EXPECT_EQ(child.GetParent(), &parent1);
    EXPECT_EQ(parent1.GetChildren().size(), 1u);

    child.AttachTo(&parent2);
    EXPECT_EQ(child.GetParent(), &parent2);
    EXPECT_EQ(parent1.GetChildren().size(), 0u); // removed from old parent
    EXPECT_EQ(parent2.GetChildren().size(), 1u);
}

// 28. AttachTo with same parent is a no-op (no duplicate child entries)
TEST(SceneComponentTest, AttachToSameParentIsNoOp) {
    SceneComponent parent, child;
    child.AttachTo(&parent);
    child.AttachTo(&parent); // second call must not duplicate
    EXPECT_EQ(parent.GetChildren().size(), 1u);
}

TEST(SceneComponentTest, SelfParentingRootLeavesStateUnchanged) {
    SceneComponent root;
    root.SetPosition({3.f, 4.f, 5.f});
    const Vec3 before = root.GetWorldPosition();

    root.AttachTo(&root);

    EXPECT_EQ(root.GetParent(), nullptr);
    EXPECT_TRUE(root.GetChildren().empty());
    EXPECT_FALSE(root.IsDirty());
    EXPECT_TRUE(NearlyEqualVec3(root.GetPosition(), before));
    EXPECT_TRUE(NearlyEqualVec3(root.GetWorldPosition(), before));
}

TEST(SceneComponentTest, SelfParentingPreservesExistingParent) {
    SceneComponent parent, child;
    parent.SetPosition({10.f, 0.f, 0.f});
    child.SetPosition({2.f, 0.f, 0.f});
    child.AttachTo(&parent);
    const Vec3 before = child.GetWorldPosition();

    child.AttachTo(&child);

    EXPECT_EQ(child.GetParent(), &parent);
    ASSERT_EQ(parent.GetChildren().size(), 1u);
    EXPECT_EQ(parent.GetChildren()[0], &child);
    EXPECT_TRUE(child.GetChildren().empty());
    EXPECT_FALSE(parent.IsDirty());
    EXPECT_FALSE(child.IsDirty());
    EXPECT_TRUE(NearlyEqualVec3(child.GetWorldPosition(), before));
}

TEST(SceneComponentTest, AncestorCyclePreservesHierarchyAndTransforms) {
    SceneComponent root, parent, child, descendant;
    root.SetPosition({10.f, 0.f, 0.f});
    parent.SetPosition({2.f, 0.f, 0.f});
    child.SetPosition({3.f, 0.f, 0.f});
    descendant.SetPosition({4.f, 0.f, 0.f});
    parent.AttachTo(&root);
    child.AttachTo(&parent);
    descendant.AttachTo(&child);
    const Vec3 before = descendant.GetWorldPosition();

    parent.AttachTo(&descendant);

    EXPECT_EQ(parent.GetParent(), &root);
    EXPECT_EQ(child.GetParent(), &parent);
    EXPECT_EQ(descendant.GetParent(), &child);
    ASSERT_EQ(root.GetChildren().size(), 1u);
    EXPECT_EQ(root.GetChildren()[0], &parent);
    ASSERT_EQ(parent.GetChildren().size(), 1u);
    EXPECT_EQ(parent.GetChildren()[0], &child);
    ASSERT_EQ(child.GetChildren().size(), 1u);
    EXPECT_EQ(child.GetChildren()[0], &descendant);
    EXPECT_TRUE(descendant.GetChildren().empty());
    EXPECT_FALSE(root.IsDirty());
    EXPECT_FALSE(parent.IsDirty());
    EXPECT_FALSE(child.IsDirty());
    EXPECT_FALSE(descendant.IsDirty());
    EXPECT_TRUE(NearlyEqual(parent.GetPosition().x, 2.f));
    EXPECT_TRUE(NearlyEqualVec3(descendant.GetWorldPosition(), before));
}

TEST(SceneComponentTest, ReparentAndNullDetachPreserveLocalTransform) {
    SceneComponent first, second, child;
    first.SetPosition({10.f, 0.f, 0.f});
    second.SetPosition({20.f, 0.f, 0.f});
    child.SetPosition({2.f, 0.f, 0.f});
    child.AttachTo(&first);
    EXPECT_TRUE(NearlyEqual(child.GetWorldPosition().x, 12.f));

    child.AttachTo(&second);

    EXPECT_TRUE(first.GetChildren().empty());
    EXPECT_EQ(child.GetParent(), &second);
    ASSERT_EQ(second.GetChildren().size(), 1u);
    EXPECT_EQ(second.GetChildren()[0], &child);
    EXPECT_TRUE(NearlyEqual(child.GetPosition().x, 2.f));
    EXPECT_TRUE(NearlyEqual(child.GetWorldPosition().x, 22.f));

    child.AttachTo(nullptr);

    EXPECT_EQ(child.GetParent(), nullptr);
    EXPECT_TRUE(second.GetChildren().empty());
    EXPECT_TRUE(NearlyEqual(child.GetPosition().x, 2.f));
    EXPECT_TRUE(NearlyEqual(child.GetWorldPosition().x, 2.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — Destructor
// ─────────────────────────────────────────────────────────────────────────────

// 29. Destroying a child detaches it from parent
TEST(SceneComponentTest, DestroyChildDetachesFromParent) {
    SceneComponent parent;
    {
        SceneComponent child;
        child.AttachTo(&parent);
        EXPECT_EQ(parent.GetChildren().size(), 1u);
    } // child destructor called here

    EXPECT_EQ(parent.GetChildren().size(), 0u);
}

// 30. Destroying a parent orphans children (children not destroyed)
TEST(SceneComponentTest, DestroyParentOrphansChildren) {
    SceneComponent child1, child2;
    {
        SceneComponent parent;
        child1.AttachTo(&parent);
        child2.AttachTo(&parent);
        EXPECT_EQ(child1.GetParent(), &parent);
        EXPECT_EQ(child2.GetParent(), &parent);
    } // parent destructor called here

    EXPECT_EQ(child1.GetParent(), nullptr);
    EXPECT_EQ(child2.GetParent(), nullptr);
    // Children are dirty because their world must be recomputed without parent.
    EXPECT_TRUE(child1.IsDirty());
    EXPECT_TRUE(child2.IsDirty());
}

// 31. Orphaned children can still compute their own world matrix
TEST(SceneComponentTest, OrphanedChildCanComputeWorldMatrix) {
    SceneComponent child;
    {
        SceneComponent parent;
        parent.SetPosition({10.f, 0.f, 0.f});
        child.SetPosition({5.f, 0.f, 0.f});
        child.AttachTo(&parent);
        // While attached, world position is 15.
        EXPECT_TRUE(NearlyEqual(child.GetWorldPosition().x, 15.f));
    }
    // Parent is gone; child should fall back to local-only world matrix.
    const Vec3 orphanPos = child.GetWorldPosition();
    EXPECT_TRUE(NearlyEqual(orphanPos.x, 5.f));
}

// ─────────────────────────────────────────────────────────────────────────────
// Test Suite: SceneComponent — GetWorldPosition edge cases
// ─────────────────────────────────────────────────────────────────────────────

// 32. GetWorldPosition for identity component is origin
TEST(SceneComponentTest, IdentityWorldPositionIsOrigin) {
    SceneComponent sc;
    const Vec3 pos = sc.GetWorldPosition();
    EXPECT_TRUE(NearlyEqual(pos.x, 0.f));
    EXPECT_TRUE(NearlyEqual(pos.y, 0.f));
    EXPECT_TRUE(NearlyEqual(pos.z, 0.f));
}

// 33. Multiple GetWorldMatrix calls return same cached result
TEST(SceneComponentTest, GetWorldMatrixReturnsCachedResult) {
    SceneComponent sc;
    sc.SetPosition({2.f, 3.f, 4.f});
    const Mat4& first  = sc.GetWorldMatrix();
    const Mat4& second = sc.GetWorldMatrix();
    // Must be the exact same object (same address, no recomputation).
    EXPECT_EQ(&first, &second);
}
