#include "input.h"
#include "camera_manager.h"
#include "fly_camera_controller.h"
#include <gtest/gtest.h>
#include <memory>

using namespace uz;

#ifdef UZ_TEST_WINDOWS_INPUT
#include "navigation_keys.generated.h"

TEST(EditorNavigation, NativeModifiersKeepTheirLeftAndRightIdentity) {
    EXPECT_EQ(ProductionWindowKey(VK_SHIFT, 0x002A0001), VK_LSHIFT);
    EXPECT_EQ(ProductionWindowKey(VK_SHIFT, 0x00360001), VK_RSHIFT);
    EXPECT_EQ(ProductionWindowKey(VK_CONTROL, 0x001D0001), VK_LCONTROL);
    EXPECT_EQ(ProductionWindowKey(VK_CONTROL, 0x011D0001), VK_RCONTROL);
    EXPECT_EQ(ProductionWindowKey(VK_MENU, 0x00380001), VK_LMENU);
    EXPECT_EQ(ProductionWindowKey(VK_MENU, 0x01380001), VK_RMENU);
    EXPECT_EQ(ProductionWindowKey(VK_F5, 0x003F0001), VK_F5);
}

TEST(EditorNavigation, InjectedModifiersLatchQuickTransitions) {
    InputSystem input;
    input.Init();
    input.SetModifierState(true, true, true, true);
    input.SetModifierState(false, false, false, false);
    input.PrepareFrame();
    for (Key key : {Key::LeftShift, Key::LeftCtrl, Key::LeftAlt}) {
        EXPECT_TRUE(input.IsKeyJustPressed(key));
        EXPECT_TRUE(input.IsKeyJustReleased(key));
        EXPECT_FALSE(input.IsKeyDown(key));
    }
    input.EndFrame();
    input.Shutdown();
}
#endif

class NavigationHarness {
public:
    std::unique_ptr<InputSystem> m_input = std::make_unique<InputSystem>();
    std::unique_ptr<CameraManager> m_cameraManager = std::make_unique<CameraManager>();
    std::unique_ptr<FlyCameraController> m_cameraCtrl = std::make_unique<FlyCameraController>();
    CameraId m_editorFreeCamId = kInvalidCameraId;
    NavigationHarness() { m_input->Init(); }
    ~NavigationHarness() { m_input->Shutdown(); }
    void Frame(float dt) {
        struct { float dt; } ctx{dt};
        bool cameraFromECS = false;
        bool vfxOverlayWantsMouse = false;
#include "navigation_frame.generated.h"
    }
};

TEST(EditorNavigation, PumpedF5ActivatesOnceAndRestoresCamera) {
    NavigationHarness h;
    auto& original = h.m_cameraManager->GetActiveCamera();
    original.SetPosition({10.f, 20.f, 30.f});
    original.LookAt({11.f, 22.f, 30.5f});
    const Vec3 direction = original.GetForward();
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    ASSERT_NE(h.m_editorFreeCamId, kInvalidCameraId);
    EXPECT_EQ(h.m_cameraManager->GetActiveCameraId(), h.m_editorFreeCamId);
    const auto freeId = h.m_editorFreeCamId;
    EXPECT_LT((h.m_cameraManager->GetActiveCamera().GetForward() - direction).length(), 0.0001f);
    h.Frame(0.f);
    EXPECT_EQ(h.m_editorFreeCamId, freeId);
    h.m_input->OnKeyUp(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    EXPECT_EQ(h.m_cameraManager->GetActiveCameraId(), 0u);
    EXPECT_EQ(h.m_editorFreeCamId, kInvalidCameraId);
    EXPECT_LT((h.m_cameraManager->GetActiveCamera().GetForward() - direction).length(), 0.0001f);
}

TEST(EditorNavigation, FreeCameraMovesAtSceneScaleAndMouseMovesSameFrame) {
    NavigationHarness h;
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    ASSERT_NE(h.m_editorFreeCamId, kInvalidCameraId);
    h.m_input->OnKeyUp(static_cast<uint16_t>(Key::F5));
    h.m_input->OnMouseButton(MouseButton::Right, true);
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::W));
    auto& camera = h.m_cameraManager->GetActiveCamera();
    const Vec3 position = camera.GetPosition();
    h.Frame(1.f);
    EXPECT_NEAR((camera.GetPosition() - position).length(), 500.f, 0.01f);
    const Vec3 direction = camera.GetForward();
    h.m_input->OnMouseMove(30.f, 10.f);
    h.Frame(0.f);
    EXPECT_GT((camera.GetForward() - direction).length(), 0.01f);
    const Vec3 after = camera.GetForward();
    h.Frame(0.f);
    EXPECT_LT((camera.GetForward() - after).length(), 0.0001f);
}

TEST(EditorNavigation, ScriptCameraRemainsOutsideFlyController) {
    NavigationHarness h;
    auto scripted = h.m_cameraManager->CreateCamera("ScriptCamera");
    h.m_cameraManager->SetActiveCamera(scripted);
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::W));
    const Vec3 position = h.m_cameraManager->GetActiveCamera().GetPosition();
    h.Frame(1.f);
    EXPECT_FLOAT_EQ((h.m_cameraManager->GetActiveCamera().GetPosition() - position).length(), 0.f);
}

TEST(EditorNavigation, PumpedEventsSurvivePreparationUntilConsumersFinish) {
    InputSystem input;
    input.Init();
    input.OnKeyDown(static_cast<uint16_t>(Key::F5));
    input.OnMouseButton(MouseButton::Left, true);
    input.OnMouseScroll(2.f);
    input.OnMouseMove(10.f, 20.f);
    input.PrepareFrame();
    EXPECT_TRUE(input.IsKeyJustPressed(Key::F5));
    EXPECT_TRUE(input.IsMouseJustPressed(MouseButton::Left));
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 2.f);
    EXPECT_FLOAT_EQ(input.GetMouseDeltaX(), 10.f);
    EXPECT_FLOAT_EQ(input.GetMouseDeltaY(), 20.f);
    input.EndFrame();
    input.PrepareFrame();
    EXPECT_TRUE(input.IsKeyDown(Key::F5));
    EXPECT_FALSE(input.IsKeyJustPressed(Key::F5));
    EXPECT_FALSE(input.IsMouseJustPressed(MouseButton::Left));
    EXPECT_FLOAT_EQ(input.GetScrollDelta(), 0.f);
    EXPECT_FLOAT_EQ(input.GetMouseDeltaX(), 0.f);
    input.EndFrame();
    input.OnKeyUp(static_cast<uint16_t>(Key::F5));
    input.OnMouseButton(MouseButton::Left, false);
    input.PrepareFrame();
    EXPECT_TRUE(input.IsKeyJustReleased(Key::F5));
    EXPECT_TRUE(input.IsMouseJustReleased(MouseButton::Left));
    input.EndFrame();
    input.Shutdown();
}

TEST(EditorNavigation, QuickF5TapBeforeFrameTogglesOnce) {
    NavigationHarness h;
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.m_input->OnKeyUp(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    ASSERT_NE(h.m_editorFreeCamId, kInvalidCameraId);
    const auto freeId = h.m_editorFreeCamId;
    h.Frame(0.f);
    EXPECT_EQ(h.m_editorFreeCamId, freeId);
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    EXPECT_EQ(h.m_editorFreeCamId, kInvalidCameraId);
    h.m_input->OnKeyDown(static_cast<uint16_t>(Key::F5));
    h.Frame(0.f);
    EXPECT_EQ(h.m_editorFreeCamId, kInvalidCameraId);
}

TEST(EditorNavigation, QuickKeyboardAndMouseEdgesSurviveUntilEndFrame) {
    InputSystem input;
    input.Init();
    input.BeginFrame();
    input.OnKeyDown(static_cast<uint16_t>(Key::F5));
    input.OnKeyUp(static_cast<uint16_t>(Key::F5));
    input.OnMouseButton(MouseButton::Left, true);
    input.OnMouseButton(MouseButton::Left, false);
    input.PrepareFrame();
    EXPECT_TRUE(input.IsKeyJustPressed(Key::F5));
    EXPECT_TRUE(input.IsKeyJustReleased(Key::F5));
    EXPECT_FALSE(input.IsKeyDown(Key::F5));
    EXPECT_TRUE(input.IsMouseJustPressed(MouseButton::Left));
    EXPECT_TRUE(input.IsMouseJustReleased(MouseButton::Left));
    EXPECT_FALSE(input.IsMouseDown(MouseButton::Left));
    input.EndFrame();
    input.PrepareFrame();
    EXPECT_FALSE(input.IsKeyJustPressed(Key::F5));
    EXPECT_FALSE(input.IsKeyJustReleased(Key::F5));
    EXPECT_FALSE(input.IsMouseJustPressed(MouseButton::Left));
    EXPECT_FALSE(input.IsMouseJustReleased(MouseButton::Left));
    input.OnMouseButton(MouseButton::Left, true);
    EXPECT_TRUE(input.IsMouseJustPressed(MouseButton::Left));
    input.BeginFrame();
    input.OnMouseButton(MouseButton::Left, true);
    EXPECT_FALSE(input.IsMouseJustPressed(MouseButton::Left));
    input.Shutdown();
}

TEST(EditorNavigation, BothShiftAndControlKeysDriveCamera) {
    for (bool rightSide : {false, true}) {
        InputSystem input;
        input.Init();
        Camera camera;
        FlyCameraController controller;
        controller.SetMoveSpeed(500.f);
        controller.InitFromCamera(camera);
        const auto shift = static_cast<uint16_t>(rightSide ? Key::RightShift : Key::LeftShift);
        const auto control = static_cast<uint16_t>(rightSide ? Key::RightCtrl : Key::LeftCtrl);
        input.OnKeyDown(static_cast<uint16_t>(Key::W));
        input.OnKeyDown(shift);
        const Vec3 start = camera.GetPosition();
        controller.Update(camera, input, 1.f);
        EXPECT_NEAR((camera.GetPosition() - start).length(), 1500.f, .01f);
        input.OnKeyUp(static_cast<uint16_t>(Key::W));
        input.OnKeyUp(shift);
        input.OnKeyDown(control);
        const float startZ = camera.GetPosition().z;
        controller.Update(camera, input, 1.f);
        EXPECT_NEAR(camera.GetPosition().z, startZ - 500.f, .01f);
        input.Shutdown();
    }
}
