// test_scene.cpp — Google Test coverage for scene_manager.cpp.
//
// Targets ≥95% line + branch coverage of:
//   scene_init, scene_clear, scene_create_node, scene_destroy_node,
//   scene_set_parent, scene_get_node (both overloads), scene_find_by_name,
//   scene_iter_children, scene_get_world_transform,
//   scene_update_world_transforms, scene_collect_visible,
//   scene_is_descendant_of.
//
// No GPU, no heap from the scene itself — scene is stack/unique_ptr allocated.
// All tests are self-contained: construct a fresh Scene, operate, assert.

#include <gtest/gtest.h>
#include <memory>
#include <cstring>
#include <cmath>
#include "engine/scene/scene.h"

using namespace uz;

// ============================================================================
// Helpers
// ============================================================================

/// Allocate a freshly-initialised Scene on the heap.
/// Scene is large (~8 MB for 65536 nodes), so never put it on the test-thread stack.
static std::unique_ptr<Scene> make_scene(const char* name = "TestScene") {
    auto s = std::make_unique<Scene>();
    scene_init(s.get(), name);
    return s;
}

/// Approximate float equality (1e-4 tolerance — enough for TRS round-trips).
static bool near_f(float a, float b, float tol = 1e-4f) {
    return std::abs(a - b) <= tol;
}

/// Check every element of two Mat4s is within tolerance.
static bool mat4_approx_eq(const Mat4& a, const Mat4& b, float tol = 1e-4f) {
    for (int c = 0; c < 4; ++c)
        for (int r = 0; r < 4; ++r)
            if (!near_f(a.m[c][r], b.m[c][r], tol))
                return false;
    return true;
}

/// Build a box frustum that accepts points with |x|<=half, |y|<=half, |z|<=half.
static Frustum make_box_frustum(float half = 10.0f) {
    Frustum f;
    // left  : x >= -half  →  1*x + 0*y + 0*z + half >= 0
    f.planes[0] = {1.0f, 0.0f, 0.0f, half};
    // right : x <=  half  → -1*x + 0*y + 0*z + half >= 0
    f.planes[1] = {-1.0f, 0.0f, 0.0f, half};
    // bottom: y >= -half
    f.planes[2] = {0.0f, 1.0f, 0.0f, half};
    // top   : y <=  half
    f.planes[3] = {0.0f, -1.0f, 0.0f, half};
    // near  : z >= -half
    f.planes[4] = {0.0f, 0.0f, 1.0f, half};
    // far   : z <=  half
    f.planes[5] = {0.0f, 0.0f, -1.0f, half};
    return f;
}

// Callback accumulator used across multiple tests.
struct ChildAccum {
    int count = 0;
    NodeId ids[64]{};
    bool stop_after_first = false;
};

static bool accum_fn(Scene* /*scene*/, SceneNode* node, void* user_data) {
    auto* acc = static_cast<ChildAccum*>(user_data);
    if (acc->count < 64) {
        acc->ids[acc->count] = node->id;
    }
    ++acc->count;
    return !acc->stop_after_first;
}

// ============================================================================
// scene_init
// ============================================================================

TEST(SceneTest, Init_SetsNameCorrectly) {
    auto s = make_scene("MyLevel");
    EXPECT_STREQ(s->name, "MyLevel");
}

TEST(SceneTest, Init_NullptrName_DoesNotCrash_NameIsEmpty) {
    auto s = std::make_unique<Scene>();
    // Must not crash. Name should be empty string (memset zeroes it, then no strncpy).
    scene_init(s.get(), nullptr);
    EXPECT_EQ(s->name[0], '\0');
}

TEST(SceneTest, Init_SetsVersionOne) {
    auto s = make_scene();
    EXPECT_EQ(s->version, 1u);
}

TEST(SceneTest, Init_SetsNodeCountZero) {
    auto s = make_scene();
    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, Init_SetsNextIdOne) {
    auto s = make_scene();
    EXPECT_EQ(s->next_id, 1u);
}

TEST(SceneTest, Init_LongName_TruncatedAt63Chars) {
    // Name longer than kNodeNameMaxLen-1 must be safely truncated.
    const char* long_name =
        "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"; // 64 'A's
    auto s = std::make_unique<Scene>();
    scene_init(s.get(), long_name);
    // Result must be null-terminated and at most 63 printable chars.
    EXPECT_EQ(std::strlen(s->name), kNodeNameMaxLen - 1u);
    EXPECT_EQ(s->name[kNodeNameMaxLen - 1], '\0');
}

// ============================================================================
// scene_clear
// ============================================================================

TEST(SceneTest, Clear_AfterAddingNodes_ResetsNodeCountToZero) {
    auto s = make_scene();
    scene_create_node(s.get(), "A", "Node3D");
    scene_create_node(s.get(), "B", "Node3D");
    ASSERT_EQ(s->node_count, 2u);

    scene_clear(s.get());

    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, Clear_ResetsNextIdToOne) {
    auto s = make_scene();
    scene_create_node(s.get(), "A", "Node3D");
    scene_create_node(s.get(), "B", "Node3D");

    scene_clear(s.get());

    EXPECT_EQ(s->next_id, 1u);
}

TEST(SceneTest, Clear_MarksPreviouslyAliveSlotsDead) {
    auto s = make_scene();
    NodeId id1 = scene_create_node(s.get(), "A", "Node3D");
    NodeId id2 = scene_create_node(s.get(), "B", "Node3D");
    ASSERT_GT(id1, 0u);
    ASSERT_GT(id2, 0u);

    scene_clear(s.get());

    // Slots must no longer be alive.
    EXPECT_EQ(scene_get_node(s.get(), id1), nullptr);
    EXPECT_EQ(scene_get_node(s.get(), id2), nullptr);
}

TEST(SceneTest, Clear_EmptyScene_IsNoopAndDoesNotCrash) {
    auto s = make_scene();
    // next_id is 1 so the loop [1, 1) is empty — should not crash.
    scene_clear(s.get());
    EXPECT_EQ(s->node_count, 0u);
    EXPECT_EQ(s->next_id, 1u);
}

// ============================================================================
// scene_create_node
// ============================================================================

TEST(SceneTest, CreateNode_SingleRootNode_ReturnsValidId) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Root", "Node3D");
    EXPECT_GT(id, kNodeIdInvalid);
}

TEST(SceneTest, CreateNode_SetsNameCorrectly) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Mesh01", "StaticMesh");
    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_STREQ(n->name, "Mesh01");
}

TEST(SceneTest, CreateNode_SetsTypeCorrectly) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Cam", "Camera");
    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_STREQ(n->type_name, "Camera");
}

TEST(SceneTest, CreateNode_RootNode_HasInvalidParentId) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Root", "Node3D");
    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->parent_id, kNodeIdInvalid);
}

TEST(SceneTest, CreateNode_WithParent_SetsParentIdCorrectly) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child", "Node3D", parent);
    SceneNode* cn = scene_get_node(s.get(), child);
    ASSERT_NE(cn, nullptr);
    EXPECT_EQ(cn->parent_id, parent);
}

TEST(SceneTest, CreateNode_WithParent_AddsToParentChildList) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child", "Node3D", parent);

    SceneNode* pn = scene_get_node(s.get(), parent);
    ASSERT_NE(pn, nullptr);
    ASSERT_EQ(pn->child_count, 1u);
    EXPECT_EQ(pn->children[0], child);
}

TEST(SceneTest, CreateNode_HasIdentityLocalTransform) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);

    const Transform& lt = n->local_transform;
    EXPECT_NEAR(lt.position.x, 0.0f, 1e-6f);
    EXPECT_NEAR(lt.position.y, 0.0f, 1e-6f);
    EXPECT_NEAR(lt.position.z, 0.0f, 1e-6f);
    // Identity quaternion: (0, 0, 0, 1)
    EXPECT_NEAR(lt.rotation.x, 0.0f, 1e-6f);
    EXPECT_NEAR(lt.rotation.y, 0.0f, 1e-6f);
    EXPECT_NEAR(lt.rotation.z, 0.0f, 1e-6f);
    EXPECT_NEAR(lt.rotation.w, 1.0f, 1e-6f);
    // Unit scale
    EXPECT_NEAR(lt.scale.x, 1.0f, 1e-6f);
    EXPECT_NEAR(lt.scale.y, 1.0f, 1e-6f);
    EXPECT_NEAR(lt.scale.z, 1.0f, 1e-6f);
}

TEST(SceneTest, CreateNode_HasActiveAndVisibleFlags) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_TRUE(n->flags & NODE_FLAG_ACTIVE);
    EXPECT_TRUE(n->flags & NODE_FLAG_VISIBLE);
}

TEST(SceneTest, CreateNode_NodeCountIncrementsOnEachCreate) {
    auto s = make_scene();
    EXPECT_EQ(s->node_count, 0u);

    scene_create_node(s.get(), "A", "Node3D");
    EXPECT_EQ(s->node_count, 1u);

    scene_create_node(s.get(), "B", "Node3D");
    EXPECT_EQ(s->node_count, 2u);

    scene_create_node(s.get(), "C", "Node3D");
    EXPECT_EQ(s->node_count, 3u);
}

TEST(SceneTest, CreateNode_SceneFull_ReturnsInvalidId) {
    auto s = make_scene();
    // Saturate node_count and next_id so both capacity guards are reachable.
    // The first check is: node_count >= kSceneMaxNodes - 1
    s->node_count = kSceneMaxNodes - 1;
    s->next_id    = kSceneMaxNodes;

    NodeId id = scene_create_node(s.get(), "Overflow", "Node3D");
    EXPECT_EQ(id, kNodeIdInvalid);
}

TEST(SceneTest, CreateNode_SceneFullViaNodeCount_ReturnsInvalidId) {
    // Trigger the first guard: node_count >= kSceneMaxNodes - 1 even with dead slots.
    auto s = make_scene();
    s->node_count = kSceneMaxNodes - 1;
    // next_id < kSceneMaxNodes so the slot search runs, finds no dead slot,
    // then hits the next_id >= kSceneMaxNodes guard.
    s->next_id = kSceneMaxNodes;

    NodeId id = scene_create_node(s.get(), "X", "Node3D");
    EXPECT_EQ(id, kNodeIdInvalid);
}

TEST(SceneTest, CreateNode_SlotReuseAfterDestroy) {
    auto s = make_scene();
    NodeId id1 = scene_create_node(s.get(), "A", "Node3D");
    ASSERT_GT(id1, kNodeIdInvalid);

    scene_destroy_node(s.get(), id1);
    EXPECT_EQ(s->node_count, 0u);

    // Create a second node — it should reclaim the dead slot at id1.
    NodeId id2 = scene_create_node(s.get(), "B", "Node3D");
    EXPECT_EQ(id2, id1) << "Expected slot reuse: id2 should equal id1";
    EXPECT_EQ(s->node_count, 1u);
}

// ============================================================================
// scene_destroy_node
// ============================================================================

TEST(SceneTest, DestroyNode_DecrementsNodeCount) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    ASSERT_EQ(s->node_count, 1u);

    scene_destroy_node(s.get(), id);

    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, DestroyNode_MarksSlotDead) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);

    EXPECT_EQ(scene_get_node(s.get(), id), nullptr);
}

TEST(SceneTest, DestroyNode_RemovesFromParentChildList) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child",  "Node3D", parent);

    {
        SceneNode* pn = scene_get_node(s.get(), parent);
        ASSERT_NE(pn, nullptr);
        ASSERT_EQ(pn->child_count, 1u);
        ASSERT_EQ(pn->children[0], child);
    }

    scene_destroy_node(s.get(), child);

    SceneNode* pn = scene_get_node(s.get(), parent);
    ASSERT_NE(pn, nullptr);
    EXPECT_EQ(pn->child_count, 0u);
}

TEST(SceneTest, DestroyNode_RecursivelyDestroysChildren) {
    auto s = make_scene();
    NodeId root   = scene_create_node(s.get(), "Root",  "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child", "Node3D", root);
    NodeId grchild= scene_create_node(s.get(), "GC",    "Node3D", child);
    ASSERT_EQ(s->node_count, 3u);

    scene_destroy_node(s.get(), root);

    EXPECT_EQ(s->node_count, 0u);
    EXPECT_EQ(scene_get_node(s.get(), root),    nullptr);
    EXPECT_EQ(scene_get_node(s.get(), child),   nullptr);
    EXPECT_EQ(scene_get_node(s.get(), grchild), nullptr);
}

TEST(SceneTest, DestroyNode_InvalidId_DoesNotCrash) {
    auto s = make_scene();
    // kNodeIdInvalid (0) must be silently ignored.
    scene_destroy_node(s.get(), kNodeIdInvalid);
    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, DestroyNode_IdOutOfBounds_DoesNotCrash) {
    auto s = make_scene();
    // ID >= kSceneMaxNodes must be silently ignored.
    scene_destroy_node(s.get(), kSceneMaxNodes);
    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, DestroyNode_AlreadyDeadSlot_DoesNotCrash_CountUnchanged) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);
    ASSERT_EQ(s->node_count, 0u);

    // Second destroy of the same (now dead) slot must be a no-op.
    scene_destroy_node(s.get(), id);
    EXPECT_EQ(s->node_count, 0u);
}

TEST(SceneTest, DestroyNode_MultipleChildren_AllRemovedFromParentList) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    NodeId c1     = scene_create_node(s.get(), "C1", "Node3D", parent);
    NodeId c2     = scene_create_node(s.get(), "C2", "Node3D", parent);
    NodeId c3     = scene_create_node(s.get(), "C3", "Node3D", parent);
    ASSERT_EQ(s->node_count, 4u);

    // Destroy middle child c2 individually.
    scene_destroy_node(s.get(), c2);
    EXPECT_EQ(s->node_count, 3u);
    SceneNode* pn = scene_get_node(s.get(), parent);
    ASSERT_NE(pn, nullptr);
    EXPECT_EQ(pn->child_count, 2u);
    // Remaining children must be c1 and c3 (order is maintained via shift).
    EXPECT_EQ(pn->children[0], c1);
    EXPECT_EQ(pn->children[1], c3);
}

// ============================================================================
// scene_set_parent
// ============================================================================

TEST(SceneTest, SetParent_ReparentsBetweenTwoParents) {
    auto s = make_scene();
    NodeId pa = scene_create_node(s.get(), "ParentA", "Node3D");
    NodeId pb = scene_create_node(s.get(), "ParentB", "Node3D");
    NodeId ch = scene_create_node(s.get(), "Child",   "Node3D", pa);

    {
        SceneNode* pan = scene_get_node(s.get(), pa);
        ASSERT_EQ(pan->child_count, 1u);
    }

    bool ok = scene_set_parent(s.get(), ch, pb);
    EXPECT_TRUE(ok);

    SceneNode* pan = scene_get_node(s.get(), pa);
    SceneNode* pbn = scene_get_node(s.get(), pb);
    SceneNode* chn = scene_get_node(s.get(), ch);
    ASSERT_NE(pan, nullptr); ASSERT_NE(pbn, nullptr); ASSERT_NE(chn, nullptr);

    EXPECT_EQ(pan->child_count, 0u) << "Old parent should have no children";
    EXPECT_EQ(pbn->child_count, 1u) << "New parent should have 1 child";
    EXPECT_EQ(pbn->children[0], ch);
    EXPECT_EQ(chn->parent_id, pb);
}

TEST(SceneTest, SetParent_ToInvalid_MakesNodeRoot) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child",  "Node3D", parent);

    bool ok = scene_set_parent(s.get(), child, kNodeIdInvalid);
    EXPECT_TRUE(ok);

    SceneNode* pn = scene_get_node(s.get(), parent);
    SceneNode* cn = scene_get_node(s.get(), child);
    ASSERT_NE(pn, nullptr); ASSERT_NE(cn, nullptr);
    EXPECT_EQ(pn->child_count, 0u);
    EXPECT_EQ(cn->parent_id, kNodeIdInvalid);
}

TEST(SceneTest, SetParent_InvalidNodeId_ReturnsFalse) {
    auto s = make_scene();
    bool ok = scene_set_parent(s.get(), kNodeIdInvalid, kNodeIdInvalid);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_IdOutOfBounds_ReturnsFalse) {
    auto s = make_scene();
    bool ok = scene_set_parent(s.get(), kSceneMaxNodes, kNodeIdInvalid);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_DeadNode_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);

    bool ok = scene_set_parent(s.get(), id, kNodeIdInvalid);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_InvalidNewParent_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    // A non-zero new_parent that is out of bounds must fail.
    bool ok = scene_set_parent(s.get(), id, kSceneMaxNodes);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_DeadNewParent_ReturnsFalse) {
    auto s = make_scene();
    NodeId id      = scene_create_node(s.get(), "Node",  "Node3D");
    NodeId dead_p  = scene_create_node(s.get(), "DeadP", "Node3D");
    scene_destroy_node(s.get(), dead_p);

    bool ok = scene_set_parent(s.get(), id, dead_p);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_SelfParent_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    bool ok = scene_set_parent(s.get(), id, id);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_WouldCreateCycle_ReturnsFalse) {
    // parent -> child -> grandchild; try to make parent a child of grandchild.
    auto s = make_scene();
    NodeId root = scene_create_node(s.get(), "Root", "Node3D");
    NodeId mid  = scene_create_node(s.get(), "Mid",  "Node3D", root);
    NodeId leaf = scene_create_node(s.get(), "Leaf", "Node3D", mid);

    // Setting root's parent to leaf would make root a descendant of leaf,
    // which is already a descendant of root — a cycle.
    bool ok = scene_set_parent(s.get(), root, leaf);
    EXPECT_FALSE(ok);
}

TEST(SceneTest, SetParent_SameParent_ReturnsTrueIsNoop) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    NodeId child  = scene_create_node(s.get(), "C", "Node3D", parent);

    // Reparenting to the same parent is a documented no-op that returns true.
    bool ok = scene_set_parent(s.get(), child, parent);
    EXPECT_TRUE(ok);

    SceneNode* pn = scene_get_node(s.get(), parent);
    ASSERT_NE(pn, nullptr);
    // Child list must be unchanged (still 1 child).
    EXPECT_EQ(pn->child_count, 1u);
    EXPECT_EQ(pn->children[0], child);
}

// ============================================================================
// scene_get_node
// ============================================================================

TEST(SceneTest, GetNode_ValidAliveNode_ReturnsNonNull) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->id, id);
}

TEST(SceneTest, GetNode_IdZero_ReturnsNull) {
    auto s = make_scene();
    EXPECT_EQ(scene_get_node(s.get(), kNodeIdInvalid), nullptr);
}

TEST(SceneTest, GetNode_IdBeyondNextId_ReturnsNull) {
    auto s = make_scene();
    // next_id == 1 after init; slot 1 is not yet allocated.
    EXPECT_EQ(scene_get_node(s.get(), 1u), nullptr);
}

TEST(SceneTest, GetNode_IdAtOrAboveMaxNodes_ReturnsNull) {
    auto s = make_scene();
    EXPECT_EQ(scene_get_node(s.get(), kSceneMaxNodes),     nullptr);
    EXPECT_EQ(scene_get_node(s.get(), kSceneMaxNodes + 1), nullptr);
}

TEST(SceneTest, GetNode_DeadSlot_ReturnsNull) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);

    EXPECT_EQ(scene_get_node(s.get(), id), nullptr);
}

TEST(SceneTest, GetNode_ConstOverload_ValidNode_ReturnsNonNull) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    const Scene* cs = s.get();
    const SceneNode* n = scene_get_node(cs, id);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(n->id, id);
    EXPECT_STREQ(n->name, "Node");
}

TEST(SceneTest, GetNode_ConstOverload_InvalidId_ReturnsNull) {
    auto s = make_scene();
    const Scene* cs = s.get();
    EXPECT_EQ(scene_get_node(cs, kNodeIdInvalid), nullptr);
    EXPECT_EQ(scene_get_node(cs, kSceneMaxNodes), nullptr);
}

TEST(SceneTest, GetNode_ConstOverload_DeadSlot_ReturnsNull) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);

    const Scene* cs = s.get();
    EXPECT_EQ(scene_get_node(cs, id), nullptr);
}

// ============================================================================
// scene_find_by_name
// ============================================================================

TEST(SceneTest, FindByName_ExistingNode_ReturnsItsId) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "PlayerSpawn", "Node3D");

    NodeId found = scene_find_by_name(s.get(), "PlayerSpawn");
    EXPECT_EQ(found, id);
}

TEST(SceneTest, FindByName_NonexistentName_ReturnsInvalid) {
    auto s = make_scene();
    scene_create_node(s.get(), "Alpha", "Node3D");

    NodeId found = scene_find_by_name(s.get(), "Beta");
    EXPECT_EQ(found, kNodeIdInvalid);
}

TEST(SceneTest, FindByName_EmptyScene_ReturnsInvalid) {
    auto s = make_scene();
    EXPECT_EQ(scene_find_by_name(s.get(), "Anything"), kNodeIdInvalid);
}

TEST(SceneTest, FindByName_NullName_ReturnsInvalid) {
    auto s = make_scene();
    scene_create_node(s.get(), "Node", "Node3D");
    EXPECT_EQ(scene_find_by_name(s.get(), nullptr), kNodeIdInvalid);
}

TEST(SceneTest, FindByName_MultipleNodesShareName_DoesNotCrash_ReturnsOneOfThem) {
    auto s = make_scene();
    NodeId id1 = scene_create_node(s.get(), "Duplicate", "Node3D");
    NodeId id2 = scene_create_node(s.get(), "Duplicate", "Node3D");
    (void)id2; // Both exist; we just verify no crash and that one is returned.

    NodeId found = scene_find_by_name(s.get(), "Duplicate");
    // Must return the first match (slot order).
    EXPECT_EQ(found, id1);
}

TEST(SceneTest, FindByName_AfterDestroy_DoesNotFindDeadNode) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Temp", "Node3D");
    scene_destroy_node(s.get(), id);

    EXPECT_EQ(scene_find_by_name(s.get(), "Temp"), kNodeIdInvalid);
}

// ============================================================================
// scene_iter_children
// ============================================================================

TEST(SceneTest, IterChildren_IteratesDirectChildrenOnly_NotGrandchildren) {
    auto s = make_scene();
    NodeId root  = scene_create_node(s.get(), "Root",  "Node3D");
    NodeId child = scene_create_node(s.get(), "Child", "Node3D", root);
    /* NodeId gc = */ scene_create_node(s.get(), "GC", "Node3D", child);

    ChildAccum acc;
    scene_iter_children(s.get(), root, accum_fn, &acc);

    EXPECT_EQ(acc.count, 1) << "Only direct children should be visited";
    EXPECT_EQ(acc.ids[0], child);
}

TEST(SceneTest, IterChildren_ParentInvalid_IteratesRootLevelNodes) {
    auto s = make_scene();
    NodeId r1 = scene_create_node(s.get(), "R1", "Node3D");
    NodeId r2 = scene_create_node(s.get(), "R2", "Node3D");
    // Child under r1 should NOT appear.
    scene_create_node(s.get(), "R1Child", "Node3D", r1);

    ChildAccum acc;
    scene_iter_children(s.get(), kNodeIdInvalid, accum_fn, &acc);

    EXPECT_EQ(acc.count, 2);
    // Both root-level nodes must be present.
    bool found_r1 = false, found_r2 = false;
    for (int i = 0; i < acc.count; ++i) {
        if (acc.ids[i] == r1) found_r1 = true;
        if (acc.ids[i] == r2) found_r2 = true;
    }
    EXPECT_TRUE(found_r1);
    EXPECT_TRUE(found_r2);
}

TEST(SceneTest, IterChildren_CallsFnForEachChild) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    NodeId c1 = scene_create_node(s.get(), "C1", "Node3D", parent);
    NodeId c2 = scene_create_node(s.get(), "C2", "Node3D", parent);
    NodeId c3 = scene_create_node(s.get(), "C3", "Node3D", parent);

    ChildAccum acc;
    scene_iter_children(s.get(), parent, accum_fn, &acc);

    EXPECT_EQ(acc.count, 3);
    bool found[3] = {false, false, false};
    for (int i = 0; i < acc.count; ++i) {
        if (acc.ids[i] == c1) found[0] = true;
        if (acc.ids[i] == c2) found[1] = true;
        if (acc.ids[i] == c3) found[2] = true;
    }
    EXPECT_TRUE(found[0]);
    EXPECT_TRUE(found[1]);
    EXPECT_TRUE(found[2]);
}

TEST(SceneTest, IterChildren_StopsEarlyWhenFnReturnsFalse) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    scene_create_node(s.get(), "C1", "Node3D", parent);
    scene_create_node(s.get(), "C2", "Node3D", parent);
    scene_create_node(s.get(), "C3", "Node3D", parent);

    ChildAccum acc;
    acc.stop_after_first = true;
    scene_iter_children(s.get(), parent, accum_fn, &acc);

    EXPECT_EQ(acc.count, 1) << "Iteration should have stopped after the first child";
}

TEST(SceneTest, IterChildren_NullFn_DoesNotCrash) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    scene_create_node(s.get(), "C", "Node3D", parent);
    // Passing null fn must be a silent no-op.
    scene_iter_children(s.get(), parent, nullptr, nullptr);
    EXPECT_EQ(s->node_count, 2u);
}

TEST(SceneTest, IterChildren_EmptyScene_NoCrash_ZeroCallbacks) {
    auto s = make_scene();
    ChildAccum acc;
    scene_iter_children(s.get(), kNodeIdInvalid, accum_fn, &acc);
    EXPECT_EQ(acc.count, 0);
}

// ============================================================================
// scene_get_world_transform
// ============================================================================

TEST(SceneTest, GetWorldTransform_IdentityNode_ReturnsIdentityMatrix) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    Mat4 wt = scene_get_world_transform(s.get(), id);
    Mat4 expected = Mat4::identity();
    // Identity * identity = identity (position 0, rotation identity, scale 1).
    EXPECT_TRUE(mat4_approx_eq(wt, expected))
        << "World transform of an identity node must be the identity matrix";
}

TEST(SceneTest, GetWorldTransform_NodeWithTranslation_TranslationAppearsInMatrix) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Translated", "Node3D");

    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    n->local_transform.position = {5.0f, 3.0f, -2.0f, 0.0f};

    Mat4 wt = scene_get_world_transform(s.get(), id);

    // Column-major: translation is in m[3][0], m[3][1], m[3][2].
    EXPECT_NEAR(wt.m[3][0], 5.0f,  1e-4f);
    EXPECT_NEAR(wt.m[3][1], 3.0f,  1e-4f);
    EXPECT_NEAR(wt.m[3][2], -2.0f, 1e-4f);
}

TEST(SceneTest, GetWorldTransform_ChildWorldTransform_IncludesParentTranslation) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child",  "Node3D", parent);

    {
        SceneNode* pn = scene_get_node(s.get(), parent);
        pn->local_transform.position = {10.0f, 0.0f, 0.0f, 0.0f};
    }
    {
        SceneNode* cn = scene_get_node(s.get(), child);
        cn->local_transform.position = {3.0f, 0.0f, 0.0f, 0.0f};
    }

    Mat4 child_world = scene_get_world_transform(s.get(), child);

    // Combined translation: 10 + 3 = 13 along X.
    EXPECT_NEAR(child_world.m[3][0], 13.0f, 1e-4f);
    EXPECT_NEAR(child_world.m[3][1], 0.0f,  1e-4f);
    EXPECT_NEAR(child_world.m[3][2], 0.0f,  1e-4f);
}

TEST(SceneTest, GetWorldTransform_InvalidId_ReturnsIdentity) {
    auto s = make_scene();
    Mat4 wt = scene_get_world_transform(s.get(), kNodeIdInvalid);
    EXPECT_TRUE(mat4_approx_eq(wt, Mat4::identity()));
}

TEST(SceneTest, GetWorldTransform_IdBeyondNextId_ReturnsIdentity) {
    auto s = make_scene();
    // next_id == 1; slot 5 is dead/unallocated.
    Mat4 wt = scene_get_world_transform(s.get(), 5u);
    EXPECT_TRUE(mat4_approx_eq(wt, Mat4::identity()));
}

TEST(SceneTest, GetWorldTransform_DeadSlot_ReturnsIdentity) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");
    scene_destroy_node(s.get(), id);

    Mat4 wt = scene_get_world_transform(s.get(), id);
    EXPECT_TRUE(mat4_approx_eq(wt, Mat4::identity()));
}

// ============================================================================
// scene_update_world_transforms
// ============================================================================

TEST(SceneTest, UpdateWorldTransforms_AliveRootNode_WorldEqualsLocalMat) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Root", "Node3D");

    SceneNode* n = scene_get_node(s.get(), id);
    ASSERT_NE(n, nullptr);
    n->local_transform.position = {7.0f, -2.0f, 4.0f, 0.0f};

    scene_update_world_transforms(s.get());

    Mat4 expected = transform_to_mat4(n->local_transform);
    EXPECT_TRUE(mat4_approx_eq(n->world_transform, expected));
}

TEST(SceneTest, UpdateWorldTransforms_ChildWorldEqualsParentWorldTimesLocalMat) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "Parent", "Node3D");
    NodeId child  = scene_create_node(s.get(), "Child",  "Node3D", parent);

    {
        SceneNode* pn = scene_get_node(s.get(), parent);
        pn->local_transform.position = {5.0f, 0.0f, 0.0f, 0.0f};
    }
    {
        SceneNode* cn = scene_get_node(s.get(), child);
        cn->local_transform.position = {0.0f, 3.0f, 0.0f, 0.0f};
    }

    scene_update_world_transforms(s.get());

    SceneNode* pn = scene_get_node(s.get(), parent);
    SceneNode* cn = scene_get_node(s.get(), child);
    ASSERT_NE(pn, nullptr); ASSERT_NE(cn, nullptr);

    Mat4 expected_child = pn->world_transform * transform_to_mat4(cn->local_transform);
    EXPECT_TRUE(mat4_approx_eq(cn->world_transform, expected_child));

    // Numerically: child world position should be (5, 3, 0).
    EXPECT_NEAR(cn->world_transform.m[3][0], 5.0f, 1e-4f);
    EXPECT_NEAR(cn->world_transform.m[3][1], 3.0f, 1e-4f);
    EXPECT_NEAR(cn->world_transform.m[3][2], 0.0f, 1e-4f);
}

TEST(SceneTest, UpdateWorldTransforms_ThreeLevelsDeep_GrandchildCorrect) {
    auto s = make_scene();
    NodeId grand = scene_create_node(s.get(), "GP",    "Node3D");
    NodeId mid   = scene_create_node(s.get(), "Mid",   "Node3D", grand);
    NodeId leaf  = scene_create_node(s.get(), "Leaf",  "Node3D", mid);

    scene_get_node(s.get(), grand)->local_transform.position = {1.0f, 0.0f, 0.0f, 0.0f};
    scene_get_node(s.get(), mid  )->local_transform.position = {2.0f, 0.0f, 0.0f, 0.0f};
    scene_get_node(s.get(), leaf )->local_transform.position = {3.0f, 0.0f, 0.0f, 0.0f};

    scene_update_world_transforms(s.get());

    SceneNode* ln = scene_get_node(s.get(), leaf);
    ASSERT_NE(ln, nullptr);
    // 1 + 2 + 3 = 6 along X.
    EXPECT_NEAR(ln->world_transform.m[3][0], 6.0f, 1e-4f);
}

TEST(SceneTest, UpdateWorldTransforms_EmptyScene_DoesNotCrash) {
    auto s = make_scene();
    scene_update_world_transforms(s.get());
    EXPECT_EQ(s->node_count, 0u);
}

// ============================================================================
// scene_collect_visible
// ============================================================================

TEST(SceneTest, CollectVisible_NodeAtOrigin_IsInsideBoxFrustum) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Origin", "Node3D");
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[8];
    uint32_t count = scene_collect_visible(s.get(), f, out, 8);

    EXPECT_EQ(count, 1u);
    EXPECT_EQ(out[0], id);
}

TEST(SceneTest, CollectVisible_NodeFarOutside_IsNotCollected) {
    auto s = make_scene();
    NodeId in_node  = scene_create_node(s.get(), "Inside",  "Node3D");
    NodeId out_node = scene_create_node(s.get(), "Outside", "Node3D");

    // Move 'out_node' far outside the frustum.
    scene_get_node(s.get(), out_node)->local_transform.position = {100.0f, 0.0f, 0.0f, 0.0f};
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[8];
    uint32_t count = scene_collect_visible(s.get(), f, out, 8);

    EXPECT_EQ(count, 1u);
    EXPECT_EQ(out[0], in_node);
}

TEST(SceneTest, CollectVisible_RespectsMaxCountLimit) {
    auto s = make_scene();
    for (int i = 0; i < 5; ++i) {
        scene_create_node(s.get(), "N", "Node3D");
    }
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[2];
    uint32_t count = scene_collect_visible(s.get(), f, out, 2u);

    // Must write at most 2 results even though 5 are inside.
    EXPECT_EQ(count, 2u);
}

TEST(SceneTest, CollectVisible_InvisibleNode_IsNotCollected) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Hidden", "Node3D");
    // Clear the VISIBLE flag.
    scene_get_node(s.get(), id)->flags &= ~static_cast<uint16_t>(NODE_FLAG_VISIBLE);
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[8];
    uint32_t count = scene_collect_visible(s.get(), f, out, 8);

    EXPECT_EQ(count, 0u);
}

TEST(SceneTest, CollectVisible_EmptyScene_ReturnsZero) {
    auto s = make_scene();
    Frustum f = make_box_frustum(10.0f);
    NodeId out[8];
    uint32_t count = scene_collect_visible(s.get(), f, out, 8);
    EXPECT_EQ(count, 0u);
}

TEST(SceneTest, CollectVisible_NullOutNodes_ReturnsZero_NoCrash) {
    auto s = make_scene();
    scene_create_node(s.get(), "Node", "Node3D");
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    uint32_t count = scene_collect_visible(s.get(), f, nullptr, 8);
    EXPECT_EQ(count, 0u);
}

TEST(SceneTest, CollectVisible_MaxCountZero_ReturnsZero_NoCrash) {
    auto s = make_scene();
    scene_create_node(s.get(), "Node", "Node3D");
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[4];
    uint32_t count = scene_collect_visible(s.get(), f, out, 0u);
    EXPECT_EQ(count, 0u);
}

TEST(SceneTest, CollectVisible_NodeOnFrustumBoundary_IsInside) {
    // Node at exactly x=10 — the plane equation: 1*10 + 10 = 20 >= 0 (inside).
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Boundary", "Node3D");
    scene_get_node(s.get(), id)->local_transform.position = {10.0f, 0.0f, 0.0f, 0.0f};
    scene_update_world_transforms(s.get());

    Frustum f = make_box_frustum(10.0f);
    NodeId out[4];
    uint32_t count = scene_collect_visible(s.get(), f, out, 4u);

    EXPECT_EQ(count, 1u);
    EXPECT_EQ(out[0], id);
}

// ============================================================================
// scene_is_descendant_of
// ============================================================================

TEST(SceneTest, IsDescendantOf_DirectChild_IsDescendantOfParent) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    NodeId child  = scene_create_node(s.get(), "C", "Node3D", parent);

    EXPECT_TRUE(scene_is_descendant_of(s.get(), child, parent));
}

TEST(SceneTest, IsDescendantOf_Grandchild_IsDescendantOfGrandparent) {
    auto s = make_scene();
    NodeId gp   = scene_create_node(s.get(), "GP",    "Node3D");
    NodeId mid  = scene_create_node(s.get(), "Mid",   "Node3D", gp);
    NodeId leaf = scene_create_node(s.get(), "Leaf",  "Node3D", mid);

    EXPECT_TRUE(scene_is_descendant_of(s.get(), leaf, gp));
}

TEST(SceneTest, IsDescendantOf_NodeIsNotDescendantOfItself) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), id, id));
}

TEST(SceneTest, IsDescendantOf_UnrelatedNodes_ReturnsFalse) {
    auto s = make_scene();
    NodeId a = scene_create_node(s.get(), "A", "Node3D");
    NodeId b = scene_create_node(s.get(), "B", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), a, b));
    EXPECT_FALSE(scene_is_descendant_of(s.get(), b, a));
}

TEST(SceneTest, IsDescendantOf_ParentIsNotDescendantOfChild) {
    auto s = make_scene();
    NodeId parent = scene_create_node(s.get(), "P", "Node3D");
    NodeId child  = scene_create_node(s.get(), "C", "Node3D", parent);

    EXPECT_FALSE(scene_is_descendant_of(s.get(), parent, child));
}

TEST(SceneTest, IsDescendantOf_InvalidDescendantId_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), kNodeIdInvalid, id));
}

TEST(SceneTest, IsDescendantOf_InvalidAncestorId_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), id, kNodeIdInvalid));
}

TEST(SceneTest, IsDescendantOf_BothInvalid_ReturnsFalse) {
    auto s = make_scene();
    EXPECT_FALSE(scene_is_descendant_of(s.get(), kNodeIdInvalid, kNodeIdInvalid));
}

TEST(SceneTest, IsDescendantOf_AncestorOutOfBounds_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), id, kSceneMaxNodes));
}

TEST(SceneTest, IsDescendantOf_DescendantOutOfBounds_ReturnsFalse) {
    auto s = make_scene();
    NodeId id = scene_create_node(s.get(), "Node", "Node3D");

    EXPECT_FALSE(scene_is_descendant_of(s.get(), kSceneMaxNodes, id));
}

TEST(SceneTest, IsDescendantOf_DeadAncestor_ReturnsFalse) {
    auto s = make_scene();
    NodeId ancestor   = scene_create_node(s.get(), "A", "Node3D");
    NodeId descendant = scene_create_node(s.get(), "D", "Node3D", ancestor);
    scene_destroy_node(s.get(), ancestor);

    // ancestor is dead, descendant is also dead (recursive destroy).
    EXPECT_FALSE(scene_is_descendant_of(s.get(), descendant, ancestor));
}

// ============================================================================
// Integration: mixed operations
// ============================================================================

TEST(SceneTest, Integration_CreateDestroyCreateReusesSlot_FindByNameWorks) {
    auto s = make_scene();
    NodeId first = scene_create_node(s.get(), "Temp", "Node3D");
    ASSERT_GT(first, kNodeIdInvalid);
    scene_destroy_node(s.get(), first);

    // Slot should be reused.
    NodeId second = scene_create_node(s.get(), "Reborn", "Node3D");
    EXPECT_EQ(second, first);
    EXPECT_EQ(scene_find_by_name(s.get(), "Reborn"), second);
    EXPECT_EQ(scene_find_by_name(s.get(), "Temp"),   kNodeIdInvalid);
}

TEST(SceneTest, Integration_SetParentAndGetWorldTransform_Consistent) {
    auto s = make_scene();
    NodeId pa = scene_create_node(s.get(), "PA", "Node3D");
    NodeId pb = scene_create_node(s.get(), "PB", "Node3D");
    NodeId ch = scene_create_node(s.get(), "Ch", "Node3D", pa);

    // Give PA a non-trivial translation.
    scene_get_node(s.get(), pa)->local_transform.position = {4.0f, 0.0f, 0.0f, 0.0f};
    // Give PB a different translation.
    scene_get_node(s.get(), pb)->local_transform.position = {0.0f, 8.0f, 0.0f, 0.0f};
    // Ch has its own translation.
    scene_get_node(s.get(), ch)->local_transform.position = {1.0f, 0.0f, 0.0f, 0.0f};

    scene_update_world_transforms(s.get());
    // While under PA: world X = 4 + 1 = 5.
    {
        SceneNode* cn = scene_get_node(s.get(), ch);
        EXPECT_NEAR(cn->world_transform.m[3][0], 5.0f, 1e-4f);
    }

    // Re-parent Ch from PA to PB.
    bool ok = scene_set_parent(s.get(), ch, pb);
    ASSERT_TRUE(ok);
    scene_update_world_transforms(s.get());

    // Now under PB: world Y = 8 + 0 = 8, world X = 0 + 1 = 1.
    {
        SceneNode* cn = scene_get_node(s.get(), ch);
        EXPECT_NEAR(cn->world_transform.m[3][0], 1.0f, 1e-4f);
        EXPECT_NEAR(cn->world_transform.m[3][1], 8.0f, 1e-4f);
    }
}

TEST(SceneTest, Integration_ClearThenRecreateNodesFromScratch) {
    auto s = make_scene("Level01");
    NodeId id1 = scene_create_node(s.get(), "Node1", "Node3D");
    NodeId id2 = scene_create_node(s.get(), "Node2", "Node3D", id1);
    (void)id2;
    ASSERT_EQ(s->node_count, 2u);

    scene_clear(s.get());
    EXPECT_EQ(s->node_count, 0u);
    EXPECT_EQ(s->next_id,    1u);

    // After clear the scene is reusable.
    NodeId fresh = scene_create_node(s.get(), "Fresh", "Camera");
    EXPECT_EQ(fresh, 1u) << "First node after clear should be at slot 1";
    EXPECT_EQ(s->node_count, 1u);
    SceneNode* fn = scene_get_node(s.get(), fresh);
    ASSERT_NE(fn, nullptr);
    EXPECT_STREQ(fn->type_name, "Camera");
    EXPECT_STREQ(fn->name, "Fresh");
}
